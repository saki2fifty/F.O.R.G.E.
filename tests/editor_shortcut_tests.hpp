#pragma once
#include "shortcuts.hpp"
#include <forge/scene.hpp>
#include <fstream>
#include <imgui_internal.h>
void require(bool condition, const char* message);
inline void test_editor_shortcuts() {
    using namespace forge;
    using namespace forge::ui;
    const auto root = std::filesystem::current_path() / ("shortcuts-" + AssetId::generate().str());
    std::filesystem::create_directories(root);
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() { std::filesystem::remove_all(root); }
    } cleanup{root};
    for (float scale : {1.f, 1.5f, 2.f}) {
        ImGui::CreateContext();
        style(scale);
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = scale == 2 ? ImVec2{960, 640} : ImVec2{1280, 900};
        io.DeltaTime = 1.f / 60;
        io.ConfigInputTrickleEventQueue = false;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        EditorActions actions;
        int saves = 0, undos = 0, redos = 0, exports = 0, opens = 0;
        actions.entries = {
            {"save",
             "Save active task",
             "",
             "Save through the active owner",
             true,
             [&] { ++saves; },
             {},
             true,
             {ImGuiMod_Ctrl | ImGuiKey_S, 0}},
            {"undo",
             "Undo active task",
             "",
             "Undo through the active owner",
             true,
             [&] { ++undos; },
             {},
             true,
             {ImGuiMod_Ctrl | ImGuiKey_Z, 0}},
            {"redo",
             "Redo active task",
             "",
             "Redo through the active owner",
             true,
             [&] { ++redos; },
             {},
             true,
             {ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z}},
            {"export",
             "Export game",
             "",
             "Export saved content",
             true,
             [&] { ++exports; },
             {},
             true,
             {}},
            {"open",
             "Open scene",
             "",
             "Choose a project Scene",
             true,
             [&] { ++opens; },
             {},
             true,
             {ImGuiMod_Ctrl | ImGuiKey_O, 0}}};
        EditorShortcuts shortcuts;
        shortcuts.open = true;
        bool allow = true, editable = true, fail_save = false;
        const auto settings = root / "settings.json";
        const auto save = [&] {
            if (fail_save)
                throw std::runtime_error("Deliberate preference write failure");
            atomic_write(settings, shortcuts.settings().dump(2));
        };
        test::observe_ui = true;
        const auto frame = [&] {
            test::ui_targets.clear();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
            ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
            shortcuts.apply(actions);
            shortcuts.draw(actions, save, editable);
            shortcuts.dispatch(actions, allow);
            ImGui::Render();
        };
        const auto click = [&](const std::string& name) {
            frame();
            require(test::ui_targets.contains(name), "Shortcut control is missing");
            const auto target = test::ui_targets.at(name);
            const ImVec2 point{(target.minimum.x + target.maximum.x) / 2,
                               (target.minimum.y + target.maximum.y) / 2};
            require(target.enabled && point.x >= target.clip_minimum.x &&
                        point.x <= target.clip_maximum.x && point.y >= target.clip_minimum.y &&
                        point.y <= target.clip_maximum.y,
                    "Shortcut control is disabled or clipped");
            io.AddMousePosEvent(point.x, point.y);
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
        };
        const auto key = [&](ImGuiKey primary, int mods = 0) {
            for (const auto flag : {ImGuiMod_Ctrl, ImGuiMod_Shift, ImGuiMod_Alt, ImGuiMod_Super})
                io.AddKeyEvent(flag, bool(mods & flag));
            io.AddKeyEvent(primary, true);
            frame();
            io.AddKeyEvent(primary, false);
            for (const auto flag : {ImGuiMod_Ctrl, ImGuiMod_Shift, ImGuiMod_Alt, ImGuiMod_Super})
                io.AddKeyEvent(flag, false);
            frame();
            frame();
        };
        frame();
        frame();
        click("shortcuts:record:save:0");
        key(ImGuiKey_O, ImGuiMod_Ctrl);
        require(shortcuts.capturing() && saves == 0 && opens == 0 &&
                    shortcuts.status().find("assigned") != std::string::npos,
                "Conflicting recording executed a command or replaced the saved binding");
        key(ImGuiKey_F6);
        require(shortcuts.capturing() && shortcuts.status().find("reserved") != std::string::npos,
                "Recording accepted a gameplay safety key");
        key(ImGuiKey_K, ImGuiMod_Ctrl | ImGuiMod_Alt);
        require(!shortcuts.capturing() && saves == 0,
                "Recorded shortcut was replayed into its command");
        require(actions.find("save")->shortcut == "Ctrl+Alt+K",
                "Effective menu label stayed stale");
        key(ImGuiKey_S, ImGuiMod_Ctrl);
        require(saves == 0, "Replaced shortcut still executed");
        key(ImGuiKey_K, ImGuiMod_Ctrl);
        require(saves == 0, "Extra or missing modifiers matched a shortcut");
        key(ImGuiKey_K, ImGuiMod_Ctrl | ImGuiMod_Alt);
        require(saves == 1, "Recorded shortcut did not invoke the shared command once");
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiMod_Alt, true);
        io.AddKeyEvent(ImGuiKey_K, true);
        frame();
        for (unsigned i = 0; i < 50; ++i)
            frame();
        require(saves == 2, "Held shortcut repeated the command");
        io.AddKeyEvent(ImGuiKey_K, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        io.AddKeyEvent(ImGuiMod_Alt, false);
        frame();
        actions.entries[0].available = false;
        key(ImGuiKey_K, ImGuiMod_Ctrl | ImGuiMod_Alt);
        require(saves == 2, "Disabled action executed from its shortcut");
        actions.entries[0].available = true;
        allow = false;
        key(ImGuiKey_K, ImGuiMod_Ctrl | ImGuiMod_Alt);
        require(saves == 2, "Mode/gesture/captured-input gate was bypassed");
        allow = true;
        io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
        frame();
        key(ImGuiKey_K, ImGuiMod_Ctrl | ImGuiMod_Alt);
        require(saves == 2, "Global shortcut executed during a pointer gesture");
        io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
        frame();
        click("shortcuts:search");
        key(ImGuiKey_K, ImGuiMod_Ctrl | ImGuiMod_Alt);
        require(saves == 2, "Global shortcut stole input from a text field");
        key(ImGuiKey_Escape);
        click("shortcuts:record:save:0");
        key(ImGuiKey_Escape);
        require(!shortcuts.capturing() && saves == 2,
                "Recording cancel replayed input or retained capture");
        key(ImGuiKey_Y, ImGuiMod_Ctrl);
        key(ImGuiKey_Z, ImGuiMod_Ctrl | ImGuiMod_Shift);
        require(redos == 2 && undos == 0, "Alternative Redo defaults or exact modifiers broke");
        const auto saved = shortcuts.settings();
        fail_save = true;
        click("shortcuts:clear:save:0");
        require(shortcuts.settings() == saved &&
                    shortcuts.status().find("failure") != std::string::npos,
                "Failed preference save replaced the last-good binding");
        fail_save = false;
        click("shortcuts:clear:save:0");
        key(ImGuiKey_K, ImGuiMod_Ctrl | ImGuiMod_Alt);
        require(saves == 2 && actions.find("save")->shortcut.empty(), "Clear retained a shortcut");
        click("shortcuts:reset:save");
        key(ImGuiKey_S, ImGuiMod_Ctrl);
        require(saves == 3, "Reset did not restore the command default");
        require(shortcuts.assign(actions, "export", {ImGuiMod_Ctrl | ImGuiKey_E, 0}, save),
                "Unassigned global command could not receive a shortcut");
        key(ImGuiKey_E, ImGuiMod_Ctrl);
        require(exports == 1, "Newly bound global command did not execute");
        Json stored;
        std::ifstream(settings) >> stored;
        stored["bindings"]["future.command"] = {{"opaque", 42}};
        stored["bindings"]["undo"] = Json::array({{{"key", "future key"}}});
        EditorShortcuts reopened;
        reopened.load(stored);
        require(reopened.settings() == stored &&
                    reopened.effective(*actions.find("undo"))[0] == (ImGuiMod_Ctrl | ImGuiKey_Z),
                "Unknown/malformed override damaged settings or disabled defaults");
        require(reopened.effective(*actions.find("export"))[0] == (ImGuiMod_Ctrl | ImGuiKey_E),
                "Reopened personal shortcut did not survive persistence");
        shortcuts.load(stored);
        require(shortcuts.assign(actions, "save", {ImGuiMod_Ctrl | ImGuiKey_Q, 0}, save) &&
                    shortcuts.settings()["bindings"]["future.command"] ==
                        stored["bindings"]["future.command"],
                "Editing one action replaced unknown command settings");
        auto conflicting = shortcuts.settings();
        conflicting["bindings"]["undo"] = conflicting["bindings"]["save"];
        shortcuts.load(conflicting);
        key(ImGuiKey_Q, ImGuiMod_Ctrl);
        require(saves == 3 && undos == 0 && shortcuts.unique_keys(actions, "save")[0] == 0,
                "Hand-edited conflict guessed a command priority or bypassed text Save guards");
        shortcuts.load({{"version", 99}, {"future", true}});
        require(!shortcuts.assign(actions, "save", {}, save) &&
                    shortcuts.settings().at("version") == 99,
                "Unknown settings version was silently rewritten");
        click("shortcuts:reset_all");
        require(shortcuts.settings()["bindings"].empty(), "Reset all left personal overrides");
        require(shortcuts.assign(actions, "save",
                                 {ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiMod_Alt | ImGuiMod_Super |
                                      ImGuiKey_RightBracket,
                                  0},
                                 save),
                "Supported long shortcut was rejected");
        // Long key names wrap independently of the reachable Change/Clear controls.
        click("shortcuts:record:save:0");
        key(ImGuiKey_Escape);
        click("shortcuts:clear:save:0");
        editable = false;
        frame();
        require(!test::ui_targets.at("shortcuts:record:save:0").enabled,
                "Shortcut recording stayed editable in a locked mode");
        test::observe_ui = false;
        ImGui::DestroyContext();
    }
}
