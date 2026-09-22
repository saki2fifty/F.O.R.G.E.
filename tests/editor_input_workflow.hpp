#pragma once
#include "ui_probe.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <forge/scene.hpp>
#include <functional>
#include <stdexcept>

namespace forge::test {
// The only writes here are input events and evidence. Scene state is observed,
// never edited through commands or model APIs by this driver.
class EditorInputWorkflow {
    enum class Kind { Click, Hover, Text, Key, Check, Capture };
    struct Step {
        Kind kind;
        std::string value;
        ImGuiKey key = ImGuiKey_None;
        bool control = false;
    };
    std::vector<Step> steps_;
    std::size_t index_ = 0;
    unsigned frame_ = 0;
    Uint64 since_ = 0;
    ImVec2 pointer_{-FLT_MAX, -FLT_MAX};
    std::string failure_, last_check_, cube_;
    std::uint64_t paused_tick_ = 0;
    Json saved_, trace_ = Json::array();
    void click(std::string target, bool ctrl = false) {
        steps_.push_back({Kind::Click, std::move(target), ImGuiKey_None, ctrl});
    }
    void hover(std::string target) { steps_.push_back({Kind::Hover, std::move(target)}); }
    void check(std::string what) { steps_.push_back({Kind::Check, std::move(what)}); }
    void capture(std::string name) { steps_.push_back({Kind::Capture, std::move(name)}); }
    void key(ImGuiKey key, bool ctrl = false) { steps_.push_back({Kind::Key, {}, key, ctrl}); }
    void text(std::string target, std::string value, bool ctrl = false) {
        click(std::move(target), ctrl);
        steps_.push_back({Kind::Text, std::move(value)});
    }
    void create(const std::string& category, const std::string& recipe) {
        click("menu:Entity");
        hover("menu:Create");
        hover("category:" + category);
        capture("create-" + recipe + "-menu");
        click("action:Create / " + recipe);
    }
    static void require(bool ok, const char* reason) {
        if (!ok)
            throw std::runtime_error(reason);
    }
    void verify(const std::string& what, const Json& state) {
        const auto& doc = state.at("scene");
        const auto& entities = doc.at("entities");
        if (what == "empty")
            require(entities.empty(), "Workflow must start in an empty scene");
        else if (what == "cube") {
            require(entities.size() == 1 && !state.at("selected").get<std::string>().empty(),
                    "Menu did not create and select a cube");
            cube_ = state.at("selected");
        } else if (what == "rename")
            require(entities.at(0).at("name") == "Workflow cube", "Name field did not commit");
        else if (what == "position")
            require(std::abs(state.at("selected_preview")
                                 .at("components")
                                 .at("forge.position")
                                 .at("x")
                                 .get<double>() -
                             1.5) < .0001,
                    "Position field did not commit 1.5");
        else if (what == "scale" || what == "undo-scale") {
            const auto& c = state.at("selected_preview").at("components");
            const double y =
                c.contains("forge.scale") ? c.at("forge.scale").at("y").get<double>() : 1;
            require(std::abs(y - (what == "scale" ? 1.75 : 1)) < .0001,
                    "Scale field/history did not produce the expected value");
        } else if (what == "saved") {
            require(!state.at("dirty").get<bool>(), "Ctrl+S did not save the scene");
            saved_ = doc;
            require(state.at("disk") == doc, "Saved disk data differs from authored scene");
        } else if (what == "unsaved" || what == "cancelled-reload") {
            require(state.at("dirty").get<bool>() && doc != saved_ && state.at("disk") == saved_,
                    "Unsaved rename must differ from unchanged disk data");
            require(doc.at("entities").at(0).at("name") == "Unsaved rename",
                    "Cancelled reload must preserve the unsaved name");
            if (what == "cancelled-reload")
                require(!ui_targets.contains("unsaved:discard"), "Cancel did not close the guard");
        } else if (what == "unsaved-guard") {
            require(ui_targets.contains("unsaved:discard") && ui_targets.contains("unsaved:cancel"),
                    "Reload of a dirty scene must ask before discarding it");
        } else if (what == "reload") {
            require(doc == saved_ && !state.at("dirty").get<bool>(),
                    "Reload from disk did not preserve the saved scene");
        } else if (what == "deleted")
            require(entities.empty(), "Delete menu did not delete selected cube");
        else if (what == "restored")
            require(doc == saved_, "Undo did not restore complete cube state");
        else if (what == "camera" || what == "light") {
            const auto& e = state.at("selected_preview");
            require(e.at("components").contains(what == "camera" ? "forge.camera" : "forge.light"),
                    "Rendering menu did not create/select the requested component");
            require(entities.size() == (what == "camera" ? 2u : 3u),
                    "Rendering menu created an unexpected entity count");
        } else if (what == "playing")
            require(state.at("playing").get<bool>() && state.at("control_ready").get<bool>() &&
                        !state.at("paused").get<bool>() && state.at("cameras").get<unsigned>() == 1,
                    "Play has not produced the authored game camera");
        else if (what == "paused") {
            require(state.at("paused").get<bool>() && state.at("control_ready").get<bool>(),
                    "Pause did not suspend Play");
            paused_tick_ = state.at("tick");
        } else if (what == "stepped") {
            require(state.at("paused").get<bool>() && state.at("control_ready").get<bool>() &&
                        state.at("tick").get<std::uint64_t>() == paused_tick_ + 1,
                    "Step must advance exactly one tick and remain paused");
        } else if (what == "zoom") {
            require(std::abs(state.at("ui_scale").get<double>() - 1.5) < .01,
                    "Ctrl+Plus did not produce 150% UI scale");
        } else if (what == "no-domain-errors") {
            for (const auto& problem : state.at("problems")) {
                const auto severity = problem.at("severity").get<std::string>();
                if (severity == "error" || severity == "fatal" || severity == "Error" ||
                    severity == "Fatal")
                    throw std::runtime_error("Unexpected editor diagnostic: " +
                                             problem.at("text").get<std::string>());
            }
            const auto* panel = ImGui::FindWindowByName("###Problems");
            require(panel && panel->Active && !panel->Hidden && panel->DockTabIsVisible,
                    "Click did not reveal the Problems panel");
        } else if (what == "stopped")
            require(!state.at("playing").get<bool>(), "Stop did not return to authoring");
    }

  public:
    explicit EditorInputWorkflow(bool enabled) {
        observe_ui = enabled;
        if (!enabled)
            return;
        key(ImGuiKey_0, true);
        check("empty");
        capture("empty-scene");
        create("3D Primitive", "Cube");
        check("cube");
        capture("created-cube");
        text("inspector:name", "Workflow cube");
        check("rename");
        text("transform:forge.position:0", "1.5", true);
        check("position");
        capture("edited-position");
        text("transform:forge.scale:1", "1.75", true);
        check("scale");
        capture("edited-scale");
        key(ImGuiKey_Z, true);
        check("undo-scale");
        capture("undo-scale");
        key(ImGuiKey_Y, true);
        check("scale");
        key(ImGuiKey_S, true);
        check("saved");
        capture("saved-scene");
        click("menu:Entity");
        click("action:Entity / Delete subtree");
        check("deleted");
        key(ImGuiKey_Z, true);
        check("restored");
        click("saved-cube-row");
        text("inspector:name", "Unsaved rename");
        check("unsaved");
        click("menu:File");
        capture("file-reload-menu");
        click("file:Reload from disk");
        check("unsaved-guard");
        capture("unsaved-reload-guard");
        click("unsaved:cancel");
        check("cancelled-reload");
        click("menu:File");
        click("file:Reload from disk");
        check("unsaved-guard");
        click("unsaved:discard");
        check("reload");
        capture("reloaded-scene");
        create("Rendering", "Camera");
        check("camera");
        text("transform:forge.position:2", "-5", true);
        capture("camera-scene-and-inspector");
        create("Rendering", "Light");
        check("light");
        text("transform:forge.position:1", "3", true);
        capture("light-scene-and-inspector");
        for (int i = 0; i < 5; ++i)
            key(ImGuiKey_Equal, true);
        check("zoom");
        capture("light-ui-150");
        key(ImGuiKey_0, true);
        click("icon:play");
        check("playing");
        capture("game-camera");
        click("tab:Problems");
        check("no-domain-errors");
        capture("runtime-problems");
        key(ImGuiKey_F6);
        check("paused");
        capture("game-paused");
        key(ImGuiKey_F7);
        check("stepped");
        capture("game-stepped");
        click("icon:stop");
        check("stopped");
        capture("returned-to-edit");
    }
    bool done() const { return index_ == steps_.size(); }
    void platform_input(SDL_WindowID window) {
        if (done())
            return;
        const auto& step = steps_[index_];
        // Interface zoom is handled by the production SDL event loop, before ImGui.
        if (step.kind != Kind::Key || !step.control ||
            (step.key != ImGuiKey_0 && step.key != ImGuiKey_Equal) || frame_ > 1)
            return;
        SDL_Event event{};
        event.type = frame_ == 0 ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.windowID = window;
        event.key.key = step.key == ImGuiKey_0 ? SDLK_0 : SDLK_EQUALS;
        event.key.scancode = step.key == ImGuiKey_0 ? SDL_SCANCODE_0 : SDL_SCANCODE_EQUALS;
        event.key.mod = SDL_KMOD_CTRL;
        event.key.down = frame_ == 0;
        if (!SDL_PushEvent(&event))
            throw std::runtime_error(SDL_GetError());
    }
    void input() {
        auto& io = ImGui::GetIO();
        io.ConfigInputTrickleEventQueue = false;
        io.AddFocusEvent(true);
        if (done())
            return;
        if (!since_)
            since_ = SDL_GetTicks();
        const auto& step = steps_[index_];
        if (SDL_GetTicks() - since_ > 12000)
            failure_ = "Timed out at step " + std::to_string(index_) + ": " + step.value + " " +
                       last_check_;
        if ((step.kind == Kind::Click || step.kind == Kind::Hover) && frame_ == 0) {
            const auto target = step.value == "saved-cube-row" ? "entity:" + cube_ : step.value;
            const auto it = ui_targets.find(target);
            if (it == ui_targets.end() || !it->second.enabled) {
                io.AddMousePosEvent(pointer_.x, pointer_.y);
                ui_targets.clear();
                return;
            }
            const auto& t = it->second;
            pointer_ = {(t.minimum.x + t.maximum.x) * .5f, (t.minimum.y + t.maximum.y) * .5f};
            trace_.push_back({{"step", index_},
                              {"target", target},
                              {"rect", {t.minimum.x, t.minimum.y, t.maximum.x, t.maximum.y}},
                              {"route", "ImGui queued mouse/key input"}});
        }
        io.AddMousePosEvent(pointer_.x, pointer_.y);
        if (step.kind == Kind::Click) {
            if (frame_ == 2) {
                io.AddKeyEvent(ImGuiMod_Ctrl, step.control);
                io.AddMouseButtonEvent(0, true);
            }
            if (frame_ == 3)
                io.AddMouseButtonEvent(0, false);
            if (frame_ == 4)
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
        } else if (step.kind == Kind::Text) {
            if (frame_ == 0) {
                io.AddKeyEvent(ImGuiMod_Ctrl, true);
                io.AddKeyEvent(ImGuiKey_A, true);
            }
            if (frame_ == 1) {
                io.AddKeyEvent(ImGuiKey_A, false);
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
            }
            if (frame_ == 2)
                io.AddInputCharactersUTF8(step.value.c_str());
            if (frame_ == 3)
                io.AddKeyEvent(ImGuiKey_Enter, true);
            if (frame_ == 4)
                io.AddKeyEvent(ImGuiKey_Enter, false);
        } else if (step.kind == Kind::Key && frame_ < 2) {
            io.AddKeyEvent(ImGuiMod_Ctrl, frame_ == 0 && step.control);
            io.AddKeyEvent(step.key, frame_ == 0);
        }
        ++frame_;
        ui_targets.clear();
    }
    void finish(const Json& state, const std::function<void(const std::string&)>& image,
                const std::function<void(const Json&)>& record) {
        if (done())
            return;
        if (!failure_.empty()) {
            image("FAILED-" + std::to_string(index_));
            Json available = Json::object();
            for (const auto& [name, target] : ui_targets)
                available[name] = {
                    {"enabled", target.enabled},
                    {"rect",
                     {target.minimum.x, target.minimum.y, target.maximum.x, target.maximum.y}}};
            record({{"ok", false},
                    {"error", failure_},
                    {"trace", trace_},
                    {"state", state},
                    {"available_controls", available}});
            throw std::runtime_error(failure_);
        }
        if (frame_ < 8 || SDL_GetTicks() - since_ < 100)
            return;
        const auto& step = steps_[index_];
        if (step.kind == Kind::Check) {
            try {
                verify(step.value, state);
            } catch (const std::exception& e) {
                last_check_ = e.what();
                return;
            }
        }
        if (step.kind == Kind::Capture) {
            const auto name = std::to_string(index_) + "-" + step.value;
            image(name);
            trace_.push_back({{"step", index_},
                              {"image", "editor-" + name + ".ppm"},
                              {"ui_scale", state.at("ui_scale")}});
        }
        constexpr const char* names[] = {"click", "hover", "type", "shortcut", "assert", "capture"};
        trace_.push_back({{"step", index_},
                          {"operation", names[int(step.kind)]},
                          {"value", step.value},
                          {"key", step.key == ImGuiKey_None ? "" : ImGui::GetKeyName(step.key)},
                          {"duration_ms", SDL_GetTicks() - since_},
                          {"control", step.control},
                          {"ok", true}});
        ++index_;
        frame_ = 0;
        since_ = 0;
        last_check_.clear();
        record({{"ok", done()},
                {"completed_steps", index_},
                {"total_steps", steps_.size()},
                {"trace", trace_},
                {"state", state}});
    }
};
} // namespace forge::test
