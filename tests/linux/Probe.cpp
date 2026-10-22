// Test equipment only; never included in the KnobOn package.
#include <rack.hpp>
#include <patch.hpp>
#include <cmath>
#include <cstdio>
using namespace rack;
Plugin* pluginInstance;

struct Probe : engine::Module {
    json_int_t lastCommand = -1;
    double lastPoll = 0;
    Probe() {
        config(16, 0, 0, 0);
        for (int i = 0; i < 16; ++i)
            if (i != 1) configParam(i, -10, 10, 0, "Test " + std::to_string(i));
        configSwitch(1, 0, 2, 1, "Stepped", {"Low", "Middle", "High"});
    }
    void stepValidation() {
        if (system::getTime() - lastPoll < 0.02) return;
        lastPoll = system::getTime();
        auto commandPath = asset::user("linux-probe-command.json");
        json_t* command = json_load_file(commandPath.c_str(), JSON_REJECT_DUPLICATES, nullptr);
        if (!json_is_object(command)) { if (command) json_decref(command); return; }
        auto* id = json_object_get(command, "id");
        if (!json_is_integer(id) || json_integer_value(id) == lastCommand) { json_decref(command); return; }
        lastCommand = json_integer_value(id);
        auto* report = json_pack("{s:I,s:[]}", "id", lastCommand, "values");
        auto* values = json_object_get(report, "values");
        auto* targets = json_object_get(command, "targets");
        for (size_t i = 0; i < json_array_size(targets); ++i) {
            auto* target = json_array_get(targets, i);
            auto* moduleId = json_object_get(target, "moduleId");
            auto* paramId = json_object_get(target, "paramId");
            auto* module = json_is_integer(moduleId) ? APP->engine->getModule(json_integer_value(moduleId)) : nullptr;
            int index = json_is_integer(paramId) ? json_integer_value(paramId) : -1;
            auto* quantity = module && index >= 0 && index < (int) module->paramQuantities.size() ? module->paramQuantities[index] : nullptr;
            if (!quantity || !quantity->isBounded()) { json_array_append_new(values, json_null()); continue; }
            auto* value = json_object_get(target, "value");
            if (json_is_number(value)) {
                double scaled = json_number_value(value);
                if (std::isfinite(scaled) && scaled >= 0 && scaled <= 1) quantity->setValue(quantity->fromScaled(scaled));
            }
            json_array_append_new(values, json_real(quantity->getScaledValue()));
        }
        if (json_is_true(json_object_get(command, "save"))) APP->patch->save(asset::user("probe-saved.vcv"));
        auto path = asset::user("linux-probe-result.json");
        auto temporary = path + ".tmp";
        if (json_dump_file(report, temporary.c_str(), JSON_INDENT(2)) == 0) std::rename(temporary.c_str(), path.c_str());
        bool close = json_is_true(json_object_get(command, "close"));
        json_decref(report);
        json_decref(command);
        if (close) APP->window->close();
    }
};

struct ProbeWidget : app::ModuleWidget {
    explicit ProbeWidget(Probe* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Bridge.svg")));
    }
    void step() override {
        if (auto* probe = getModule<Probe>()) probe->stepValidation();
        app::ModuleWidget::step();
    }
};

void init(Plugin* plugin) {
    pluginInstance = plugin;
    plugin->addModel(createModel<Probe, ProbeWidget>("Probe"));
}
