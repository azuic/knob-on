#include "Socket.hpp"
// Only this standalone test executable constructs Rack's internal engine.
// Include its declaration before rack.hpp marks internal APIs unavailable to plugins.
#include <engine/Engine.hpp>
#undef PRIVATE
#include "Bridge.hpp"
#include <cassert>
#include <iostream>
#include <thread>
#include <sstream>
#include <unordered_set>

Plugin* pluginInstance = nullptr;

static json_malloc_t originalJsonAllocate = nullptr;
static json_free_t originalJsonFree = nullptr;
static std::unordered_set<void*> jsonAllocations;
static void* trackedJsonAllocate(size_t size) {
    void* result = originalJsonAllocate(size);
    if (result) jsonAllocations.insert(result);
    return result;
}
static void trackedJsonFree(void* pointer) {
    if (pointer) {
        assert(jsonAllocations.erase(pointer) == 1);
        originalJsonFree(pointer);
    }
}
static void checkJsonOwnership(Bridge& bridge) {
    // No other JSON work is in flight in this standalone test process.
    // A direct free() on a json_dumps buffer bypasses our callback.
    json_get_alloc_funcs(&originalJsonAllocate, &originalJsonFree);
    json_set_alloc_funcs(trackedJsonAllocate, trackedJsonFree);
    assert(!bridge.snapshot().empty());
    assert(jsonAllocations.empty());
    bridge.sendJSON(json_pack("{s:s}", "type", "ping"));
    assert(jsonAllocations.empty());
    assert(!bridge.bankSnapshot().empty());
    assert(jsonAllocations.empty());
    json_set_alloc_funcs(originalJsonAllocate, originalJsonFree);
}

struct TestVCO : engine::Module {
    TestVCO() {
        config(16, 0, 0, 0);
        configParam(0, -54, 54, 0, "Frequency", " Hz", std::pow(2.f, 1.f / 12.f), dsp::FREQ_C4, 0);
        configSwitch(1, 0, 2, 1, "Octave", {"Low", "Middle", "High"});
        for (int i = 2; i < Bridge::controlCount; ++i) configParam(i, 0, 1, 0.5, "Parameter " + std::to_string(i + 1));
    }
};

static void set(Bridge& bridge, double value, int sequence, const std::string& token) {
    auto* object = json_pack("{s:s,s:i,s:s,s:f,s:i}", "type", "set", "version", 1,
        "mapping", token.c_str(), "value", value, "seq", sequence);
    char* line = json_dumps(object, JSON_COMPACT);
    bridge.command(line);
    free(line); json_decref(object);
}

int main(int argc, char** argv) {
    random::init();
    auto* context = new Context;
    contextSet(context);
    context->engine = new engine::Engine;
    Model model;
    model.name = "Test VCO";
    {
        TestVCO vco;
        vco.model = &model;
        context->engine->addModule(&vco);
        {
            Bridge bridge;
            context->engine->addModule(&bridge);
            bridge.map(vco.id, 0);
            if (argc > 1 && std::string(argv[1]) == "--self-test") {
                checkJsonOwnership(bridge);
                bridge.map(vco.id, 0);
                assert(bridge.quantity() != nullptr);
                bridge.command("{\"type\":\"hello\",\"version\":1}");
                set(bridge, 0.1234567, 1, bridge.mapping);
                assert(std::fabs(vco.paramQuantities[0]->getScaledValue() - 0.1234567) < 1e-6);
                set(bridge, 0.9, 1, bridge.mapping); // duplicate, ignored
                assert(std::fabs(vco.paramQuantities[0]->getScaledValue() - 0.1234567) < 1e-6);
                set(bridge, 1.2, 2, bridge.mapping); // invalid, ignored
                assert(bridge.lastSequence == 1);
                auto oldToken = bridge.mapping;
                auto* saved = bridge.dataToJson();
                bridge.map(vco.id, 1);
                set(bridge, 0.9, 2, oldToken); // stale mapping, ignored
                assert(vco.paramQuantities[1]->getValue() == 1);
                set(bridge, 0.9, 2, bridge.mapping); // snapped switch
                assert(vco.paramQuantities[1]->getValue() == 2);
                bridge.dataFromJson(saved);
                json_decref(saved);
                assert(bridge.target.moduleId == vco.id && bridge.target.paramId == 0);
                assert(bridge.mapping != oldToken);
                auto* snapshot = json_loads(bridge.snapshot().c_str(), 0, nullptr);
                assert(json_is_true(json_object_get(snapshot, "mapped")));
                assert(json_real_value(json_object_get(snapshot, "value")) > 0.1234);
                json_decref(snapshot);
                // Sixteen independent handles survive a save/reload, including sliders.
                for (int i = 0; i < Bridge::controlCount; ++i) bridge.map(vco.id, i, i);
                saved = bridge.dataToJson();
                for (int i = 0; i < Bridge::controlCount; ++i) bridge.unmap(i);
                bridge.dataFromJson(saved);
                json_decref(saved);
                for (int i = 0; i < Bridge::controlCount; ++i) assert(bridge.quantity(i) == vco.paramQuantities[i]);
                auto previous = bridge.mappings[9];
                bridge.map(vco.id, 9, 4); // Moving a target retires the old slot and token.
                assert(!bridge.quantity(9) && bridge.quantity(4) == vco.paramQuantities[9]);
                assert(previous != bridge.mappings[9]);
                bridge.unmap(4);
                assert(!bridge.quantity(4) && bridge.quantity(8));
                context->engine->removeModule(&vco);
                assert(bridge.quantity() == nullptr);
                std::cout << "Rack parameter, precision, sequence, remapping, stepped, persistence, deletion, JSON allocator ownership: passed\n";
            } else {
                // A real Rack Engine and ParamQuantity, with no graphics/audio device.
                // TCP tests drive the exact same Bridge code as the loadable plugin.
                bridge.listenPort = 18058;
#ifdef _WIN32
                bridge.discovery.name = "knob-test-" + std::to_string(GetCurrentProcessId());
#else
                bridge.discovery.name = "knob-test-" + std::to_string(getpid());
#endif
                bridge.started = bridge.server.start(bridge.listenPort);
                if (!bridge.started) {
                    std::cerr << bridge.server.status << '\n';
                    return 1;
                }
#ifndef _WIN32
                fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
#endif
                std::cout << "ready\n" << std::flush;
                std::string input;
                bool running = true;
                while (running) {
                    bridge.uiStep();
                    char buffer[1024];
#ifdef _WIN32
                    // Python launches this harness with a pipe. Console input is
                    // deliberately unsupported; never block the UI/network loop.
                    DWORD available = 0, count = 0;
                    HANDLE pipe = GetStdHandle(STD_INPUT_HANDLE);
                    if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) running = false;
                    else if (available && !ReadFile(pipe, buffer,
                        std::min<DWORD>(available, sizeof(buffer)), &count, nullptr)) running = false;
#else
                    auto count = read(STDIN_FILENO, buffer, sizeof(buffer));
#endif
                    if (count > 0) input.append(buffer, count);
#ifndef _WIN32
                    else if (count == 0) running = false;
#endif
                    auto end = input.find('\n');
                    if (end != std::string::npos) {
                        auto command = input.substr(0, end);
                        input.erase(0, end + 1);
                        if (command == "quit") running = false;
                        else if (command == "drop") bridge.server.drop();
                        else if (command == "bankmap") {
                            for (int i = 0; i < Bridge::controlCount; ++i) bridge.map(vco.id, i, i);
                        }
                        else if (command.rfind("map ", 0) == 0) {
                            std::istringstream args(command.substr(4));
                            int slot, param; args >> slot >> param;
                            if (slot >= 0 && slot < Bridge::controlCount && param >= 0 && param < Bridge::controlCount) bridge.map(vco.id, param, slot);
                        }
                        else if (command.rfind("value ", 0) == 0) {
                            std::istringstream args(command.substr(6));
                            int slot; float value; args >> slot >> value;
                            if (slot >= 0 && slot < Bridge::controlCount && bridge.quantity(slot)) bridge.quantity(slot)->setValue(value);
                        }
                        else if (command == "remap") bridge.map(vco.id, 1);
                        else if (command == "reload") {
                            auto* saved = bridge.dataToJson();
                            bridge.dataFromJson(saved);
                            json_decref(saved);
                        }
                        else vco.paramQuantities[bridge.target.paramId]->setValue(std::stof(command));
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                context->engine->removeModule(&vco);
            }
            context->engine->removeModule(&bridge);
        }
    }
    delete context;
    contextSet(nullptr);
}
