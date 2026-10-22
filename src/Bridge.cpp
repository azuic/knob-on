#include "Bridge.hpp"
#include <GLFW/glfw3.h>
#include <blendish.h>

Plugin* pluginInstance;

// The wordmark is outlined in Bridge.svg; other panel text uses Rack's font API.
struct BridgePanelText : widget::TransparentWidget {
    void draw(const DrawArgs& args) override {
        auto font = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
        nvgFillColor(args.vg, nvgRGB(103, 107, 114));
        nvgFontSize(args.vg, 10);
        nvgText(args.vg, 75, 46, "bridge / 16", nullptr);
        nvgFontSize(args.vg, 9);
        nvgText(args.vg, 75, 274, "learn, then touch", nullptr);
        nvgText(args.vg, 75, 289, "a Rack parameter", nullptr);
        nvgText(args.vg, 75, 340, "two-way control", nullptr);
    }
};

struct BridgeDisplay : widget::Widget {
    Bridge* module = nullptr;
    void draw(const DrawArgs& args) override {
        auto font = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
        auto line = [&](const std::string& text, float y, float size, NVGcolor color) {
            nvgFontSize(args.vg, size);
            float width = nvgTextBounds(args.vg, 0, 0, text.c_str(), nullptr, nullptr);
            if (width > box.size.x - 10) nvgFontSize(args.vg, size * (box.size.x - 10) / width);
            nvgFillColor(args.vg, color);
            nvgText(args.vg, box.size.x / 2, y, text.c_str(), nullptr);
        };
        auto ink = nvgRGB(28, 29, 32);
        auto grey = nvgRGB(103, 107, 114);
        std::string status = "ready to connect";
        if (module) status = module->started ? (module->server.connected() ? "connected" : "ready to connect") : module->server.status;
        line(status, 0, 10, nvgRGB(75, 152, 89));
        line(module && !module->discovery.advertised ? "connect by address" : "find in app", 22, 9, grey);
        line(module ? module->discovery.name : "knob-…", 37, 16, ink);
        line("enter address manually", 61, 8, grey);
        line(module ? (module->discovery.address.empty() ? "check network settings" : module->discovery.address)
                    : "computer address", 75, 11, ink);
        std::string text = "0 / 16 connected";
        if (module) {
            if (module->learning) text = Bridge::controlName(module->learningControl) + ": touch in Rack";
            else if (!module->message.empty()) text = module->message;
            else {
                int mapped = 0;
                for (int i = 0; i < Bridge::controlCount; ++i) if (module->quantity(i)) ++mapped;
                text = std::to_string(mapped) + " / 16 connected";
            }
        }
        line(text, 103, 9, grey);
    }
};

struct LearnButton : ui::Button {
    Bridge* module = nullptr;
    void draw(const DrawArgs& args) override {
        auto font = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
        if (!font) { ui::Button::draw(args); return; }
        auto label = text;
        text.clear();
        ui::Button::draw(args);
        text = label;
        nvgSave(args.vg);
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 12);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, bndGetTheme()->regularTheme.textColor);
        nvgText(args.vg, box.size.x / 2, box.size.y / 2, label.c_str(), nullptr);
        nvgRestore(args.vg);
    }
    void onAction(const ActionEvent&) override {
        if (!module) return;
        if (module->learning) { module->learning = false; module->dirty = true; }
        else {
            int slot = 0;
            while (slot < Bridge::controlCount - 1 && module->quantity(slot)) ++slot;
            module->beginLearn(slot);
        }
    }
    void step() override {
        text = module && module->learning ? "Cancel" : "Add Control";
        ui::Button::step();
    }
};

struct BridgeWidget : app::ModuleWidget {
    explicit BridgeWidget(Bridge* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Bridge.svg")));
        auto* lettering = new BridgePanelText;
        lettering->box.size = box.size;
        addChild(lettering);
        addChild(createWidget<ScrewSilver>(Vec(0, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 15, box.size.y - 15)));
        auto* display = new BridgeDisplay;
        display->module = module;
        display->box.pos = Vec(5, 84);
        display->box.size = Vec(140, 120);
        addChild(display);
        auto* learn = new LearnButton;
        learn->module = module;
        learn->box.pos = Vec(15, 212);
        learn->box.size = Vec(120, 28);
        addChild(learn);
    }

    void appendContextMenu(ui::Menu* menu) override {
        auto* bridge = getModule<Bridge>();
        if (!bridge) return;
        menu->addChild(new ui::MenuSeparator);
        auto address = bridge->discovery.address;
        menu->addChild(createMenuItem("Copy connection address", "", [address] {
            glfwSetClipboardString(APP->window->win, address.c_str());
        }, address.empty()));
    }

    void step() override {
        if (auto* bridge = getModule<Bridge>()) bridge->uiStep();
        app::ModuleWidget::step();
    }
};

Model* modelBridge = createModel<Bridge, BridgeWidget>("Bridge");

void init(Plugin* plugin) {
#ifdef _WIN32
    INFO("Knob On Bridge Windows test2: init; MSVCRT / Jansson allocator correction");
#endif
    pluginInstance = plugin;
    plugin->addModel(modelBridge);
}
