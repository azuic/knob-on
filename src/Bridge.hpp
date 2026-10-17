#pragma once
#include <rack.hpp>
#include "TcpServer.hpp"
#include "Discovery.hpp"
#include <cmath>
#include <limits>
#include <array>

using namespace rack;
extern Plugin* pluginInstance;

struct Bridge : engine::Module {
    static constexpr int controlCount = 16;
    std::array<engine::ParamHandle, controlCount> targets;
    std::array<std::string, controlCount> mappings;
    std::array<int64_t, controlCount> sequences{};
    // Slot zero aliases preserve the original patch/client contract.
    engine::ParamHandle& target = targets[0];
    std::string& mapping = mappings[0];
    int64_t& lastSequence = sequences[0];
    int learningControl = 0;
    int bankControls = 0;
    knob::TcpServer server;
    knob::Discovery discovery;
    bool learning = false;
    bool handshake = false;
    bool started = false;
    bool dirty = true;
    std::string lastSnapshot;
    std::string message;
    double lastPoll = 0;
    double nextListen = 0;

    static std::string controlName(int slot) {
        if (slot < 8) return "Dial " + std::to_string(slot + 1);
        if (slot < 12) return "Slider " + std::to_string(slot - 7);
        return "Dial " + std::to_string(slot - 3);
    }

    Bridge() {
        config(0, 0, 0, 0);
        for (int i = 0; i < controlCount; ++i) {
            targets[i].color = nvgRGB(75, 152, 89);
            targets[i].text = "Knob On / " + controlName(i);
            APP->engine->addParamHandle(&targets[i]);
            newMapping(i);
        }
        server.onConnect = [this] {
            handshake = false; bankControls = 0; sequences.fill(0);
            dirty = true; lastSnapshot.clear();
        };
        server.onDisconnect = [this] { handshake = false; learning = false; };
        server.onLine = [this](const std::string& line) { command(line); };
    }

    ~Bridge() override {
        server.stop();
        for (auto& handle : targets) APP->engine->removeParamHandle(&handle);
    }

    void newMapping(int control = 0) {
        mappings[control] = std::to_string(random::u64());
        sequences[control] = 0;
        dirty = true;
    }

    engine::ParamQuantity* quantity(int control = 0) {
        auto& target = targets[control];
        if (!target.module || target.paramId < 0 || target.paramId >= static_cast<int>(target.module->paramQuantities.size())) return nullptr;
        auto* q = target.module->paramQuantities[target.paramId];
        if (!q || !q->isBounded() || !std::isfinite(q->getMinValue()) || !std::isfinite(q->getMaxValue()) || q->getMaxValue() <= q->getMinValue()) return nullptr;
        return q;
    }

    void beginLearn(int control = 0) {
        if (APP->scene) APP->scene->rack->touchedParam = nullptr;
        learningControl = control;
        learning = true;
        message.clear();
        dirty = true;
    }

    void unmap(int control) {
        APP->engine->updateParamHandle(&targets[control], -1, 0, true);
        if (learning && learningControl == control) learning = false;
        newMapping(control);
    }

    void map(int64_t moduleId, int paramId, int control = 0) {
        // One Rack parameter belongs to one touch control at a time.
        for (int i = 0; i < controlCount; ++i) {
            if (i == control || (targets[i].moduleId == moduleId && targets[i].paramId == paramId)) unmap(i);
        }
        APP->engine->updateParamHandle(&targets[control], moduleId, paramId, true);
        learning = false;
        newMapping(control);
    }

    void sendJSON(json_t* object) {
        char* raw = json_dumps(object, JSON_COMPACT | JSON_ENCODE_ANY);
        if (raw) { server.sendLine(raw); free(raw); }
        json_decref(object);
    }

    void error(const char* text) {
        sendJSON(json_pack("{s:s,s:s}", "type", "error", "message", text));
    }

    std::string snapshot(int control = 0) {
        auto& target = targets[control];
        auto* q = quantity(control);
        double value = q ? q->getScaledValue() : 0;
        if (!std::isfinite(value)) { q = nullptr; value = 0; }
        std::string moduleName;
        if (q && target.module->model) moduleName = target.module->model->name;
        json_t* state = json_pack("{s:s,s:i,s:b,s:b,s:s,s:s,s:s,s:f,s:f,s:f,s:s,s:s,s:b,s:I}",
            "type", "state", "version", 1,
            "mapped", q != nullptr, "learning", learning && learningControl == control,
            "mapping", mappings[control].c_str(), "name", q ? q->getLabel().c_str() : "",
            "module", moduleName.c_str(), "value", std::max(0.0, std::min(1.0, value)),
            "minimum", q ? static_cast<double>(q->getMinValue()) : 0.0,
            "maximum", q ? static_cast<double>(q->getMaxValue()) : 1.0,
            "display", q ? q->getDisplayValueString().c_str() : "",
            "unit", q ? q->getUnit().c_str() : "",
            "stepped", q ? q->snapEnabled : false,
            "ack", static_cast<json_int_t>(sequences[control]));
        json_object_set_new(state, "control", json_integer(control));
        char* raw = json_dumps(state, JSON_COMPACT);
        std::string result = raw ? raw : "";
        free(raw);
        json_decref(state);
        return result;
    }

    std::string bankSnapshot() {
        std::string result = "{\"type\":\"bank\",\"version\":1,\"controls\":[";
        for (int i = 0; i < bankControls; ++i) {
            if (i) result += ",";
            result += snapshot(i);
        }
        return result + "]}";
    }

    void command(const std::string& line) {
        json_error_t parseError;
        json_t* object = json_loads(line.c_str(), JSON_REJECT_DUPLICATES, &parseError);
        if (!json_is_object(object)) { if (object) json_decref(object); error("Expected a JSON object."); return; }
        const char* type = json_string_value(json_object_get(object, "type"));
        auto* version = json_object_get(object, "version");
        if (!type || !json_is_integer(version) || json_integer_value(version) != 1) {
            error("Unsupported protocol version."); json_decref(object); return;
        }
        std::string kind(type);
        int control = 0;
        auto* controlJ = json_object_get(object, "control");
        if (controlJ) {
            if (!json_is_integer(controlJ) || json_integer_value(controlJ) < 0 || json_integer_value(controlJ) >= controlCount) {
                error("Invalid control index."); json_decref(object); return;
            }
            control = static_cast<int>(json_integer_value(controlJ));
        }
        if (kind == "hello") {
            auto requested = json_integer_value(json_object_get(object, "controls"));
            bankControls = requested == controlCount || requested == 12 ? static_cast<int>(requested) : 0;
            handshake = true; dirty = true;
        }
        else if (!handshake) error("Send hello first.");
        else if (kind == "ping") sendJSON(json_pack("{s:s,s:i}", "type", "pong", "version", 1));
        else if (kind == "sync") dirty = true;
        else if (kind == "learn") beginLearn(control);
        else if (kind == "cancelLearn") { learning = false; dirty = true; }
        else if (kind == "unmap") {
            const char* token = json_string_value(json_object_get(object, "mapping"));
            if (!token || mappings[control] != token) error("Mapping changed. Refreshing the control.");
            else unmap(control);
            dirty = true;
        }
        else if (kind == "set") {
            auto* valueJ = json_object_get(object, "value");
            auto* seqJ = json_object_get(object, "seq");
            const char* token = json_string_value(json_object_get(object, "mapping"));
            double value = json_number_value(valueJ);
            auto* q = quantity(control);
            if (!q || !token || mappings[control] != token) { error("Mapping changed. Refreshing the dial."); dirty = true; }
            else if (!json_is_number(valueJ) || !std::isfinite(value) || value < 0 || value > 1 ||
                     !json_is_integer(seqJ) || json_integer_value(seqJ) <= 0 || json_integer_value(seqJ) > 9007199254740991LL) {
                error("Invalid value or sequence.");
            } else if (json_integer_value(seqJ) > sequences[control]) {
                float raw = q->fromScaled(static_cast<float>(value));
                if (q->snapEnabled) raw = std::round(raw);
                q->setValue(raw);
                sequences[control] = json_integer_value(seqJ);
                dirty = true;
            }
        }
        else error("Unknown command.");
        json_decref(object);
    }

    // Network, JSON, learning, and feedback all run on the UI thread.
    void uiStep() {
        const double time = knob::now();
        if (!started && time >= nextListen) {
            started = server.start();
            nextListen = time + 2;
        }
        server.step();
        discovery.step(started, time);
        if (learning && APP->scene) {
            auto* touched = APP->scene->rack->getTouchedParam();
            if (touched && touched->module && touched->module != this) {
                auto* q = touched->getParamQuantity();
                if (q && q->isBounded() && q->getMaxValue() > q->getMinValue()) {
                    map(touched->module->id, touched->paramId, learningControl);
                } else { message = "Unbounded target"; learning = false; dirty = true; }
                APP->scene->rack->touchedParam = nullptr;
            }
        }
        if (handshake && time - lastPoll >= 1.0 / 30.0) {
            lastPoll = time;
            auto current = bankControls ? bankSnapshot() : snapshot();
            if (dirty || current != lastSnapshot) {
                server.sendLine(current);
                lastSnapshot = current;
                dirty = false;
            }
        }
    }

    json_t* dataToJson() override {
        auto* object = json_object();
        auto* slots = json_array();
        for (auto& handle : targets) {
            json_array_append_new(slots, json_pack("{s:I,s:i}", "moduleId", static_cast<json_int_t>(handle.moduleId), "paramId", handle.paramId));
        }
        json_object_set_new(object, "controls", slots);
        return object;
    }

    void dataFromJson(json_t* object) override {
        auto* slots = json_object_get(object, "controls");
        // The engine owns its write lock while restoring a patch.
        for (auto& handle : targets) APP->engine->updateParamHandle_NoLock(&handle, -1, 0, true);
        for (int i = 0; i < controlCount; ++i) {
            auto* slot = json_is_array(slots) ? json_array_get(slots, i) : (i == 0 ? object : nullptr);
            auto* moduleId = slot ? json_object_get(slot, "moduleId") : nullptr;
            auto* paramId = slot ? json_object_get(slot, "paramId") : nullptr;
            APP->engine->updateParamHandle_NoLock(&targets[i],
                json_is_integer(moduleId) ? json_integer_value(moduleId) : -1,
                json_is_integer(paramId) ? static_cast<int>(json_integer_value(paramId)) : 0, false);
            newMapping(i);
        }
        learning = false;
    }

    void onReset(const ResetEvent&) override {
        for (int i = 0; i < controlCount; ++i) {
            APP->engine->updateParamHandle_NoLock(&targets[i], -1, 0, true);
            newMapping(i);
        }
        learning = false;
    }
};
