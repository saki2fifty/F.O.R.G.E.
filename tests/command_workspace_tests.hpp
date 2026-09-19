#pragma once
#include "command_workspace.hpp"
void require(bool condition, const char* message);
inline void test_command_workspace(float scale) {
    ImGui::CreateContext();
    forge::ui::style(scale);
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1100, 700};
    io.DeltaTime = 1.0f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    forge::EngineContext scene_engine;
    forge::Scene scene(scene_engine.world());
    scene.reset({{"version", 1}, {"entities", forge::Json::array()}});
    forge::ui::CommandWorkspace workspace;
    std::string selected, message;
    forge::ui::EditorActions actions;
    workspace.actions = &actions;
    auto frame = [&] {
        actions.entries.clear();
        for (auto entry : forge::ui::palette_entries(selected, 1, {0, 0, 0}, false))
            if (entry.operation == "entity.create")
                actions.entries.push_back({entry.label,
                                           entry.label,
                                           "",
                                           entry.help,
                                           true,
                                           [&, entry] {
                                               selected =
                                                   forge::authoring_command(scene, entry.operation,
                                                                            entry.arguments)
                                                       .at("selected")
                                                       .get<std::string>();
                                           },
                                           {}});
        ImGui::NewFrame();
        workspace.draw(scene, selected, message, 1, {0, 0, 0}, false, false);
        ImGui::Render();
    };
    frame();
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiMod_Shift, true);
    io.AddKeyEvent(ImGuiKey_P, true);
    frame();
    io.AddKeyEvent(ImGuiKey_P, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    io.AddKeyEvent(ImGuiMod_Shift, false);
    frame();
    frame();
    io.AddInputCharactersUTF8("Create / Sphere");
    frame();
    frame();
    io.AddKeyEvent(ImGuiKey_Enter, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Enter, false);
    frame();
    require(scene.entity_count() == 1 &&
                scene.document()["entities"][0]["components"]["forge.primitive"]["kind"] == 1,
            "Palette keyboard search/execute failed");
    require(scene.undo() && scene.entity_count() == 0, "Palette command bypassed undo");
    workspace.diagnostics_open = true;
    workspace.schema_open = true;
    frame();
    frame();
    require(forge::ui::command_matches("Transform / Reset rotation", "RESET"),
            "Palette case-insensitive filtering failed");
    const auto entries = forge::ui::palette_entries("", 1, {0, 0, 0}, false);
    for (const auto& e : entries)
        if (e.operation == "entity.delete")
            require(!e.available, "Delete available without selection");
    ImGui::DestroyContext();
}

// Exercise the palette's shared-action dispatcher, not a separate mutation fallback.
inline void test_palette_action_dispatch() {
    ImGui::CreateContext();
    forge::ui::style(1);
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1100, 700};
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    forge::EngineContext engine;
    forge::Scene scene(engine.world());
    forge::ui::EditorActions actions;
    forge::ui::CommandWorkspace workspace;
    workspace.actions = &actions;
    std::string selected, message;
    int invoked = -1;
    const char* labels[] = {"Save / Test document",
                            "Undo / Test document",
                            "Redo / Test document",
                            "Transform / Rotate",
                            "Play",
                            "Pause",
                            "Step",
                            "Stop",
                            "Add Component",
                            "Inspect / Component schema"};
    for (int n = 0; n < 10; ++n)
        actions.entries.push_back(
            {std::to_string(n), labels[n], "", "Shared action", true, [&, n] { invoked = n; }, {}});
    auto frame = [&] {
        ImGui::NewFrame();
        workspace.draw(scene, selected, message, 1, {0, 0, 0}, false, false);
        ImGui::Render();
    };
    auto key = [&](ImGuiKey k) {
        io.AddKeyEvent(k, true);
        frame();
        io.AddKeyEvent(k, false);
        frame();
    };
    auto search = [&](const char* filter) {
        workspace.open_palette();
        frame();
        frame();
        io.AddInputCharactersUTF8(filter);
        frame();
        frame();
    };
    frame();
    for (int n = 0; n < 10; ++n) {
        search(labels[n]);
        key(ImGuiKey_Enter);
        require(invoked == n, "Palette failed to route shared task/runtime/component action");
    }
    actions.entries[0].available = false;
    actions.entries[0].unavailable_reason = "The active document is closed.";
    require(actions.entries[0].help_text().find("document is closed") != std::string::npos,
            "Disabled reason missing");
    invoked = -1;
    search(labels[0]);
    key(ImGuiKey_Enter);
    require(invoked == -1, "Disabled palette action executed");
    key(ImGuiKey_Escape);
    workspace.open_palette();
    frame();
    frame();
    key(ImGuiKey_DownArrow);
    key(ImGuiKey_DownArrow);
    key(ImGuiKey_UpArrow);
    key(ImGuiKey_Enter);
    require(invoked == 1, "Palette arrow navigation did not select expected action");
    search("no such command exists");
    invoked = -1;
    key(ImGuiKey_Enter);
    require(invoked == -1, "Empty palette search executed an action");
    key(ImGuiKey_Escape);
    ImGui::DestroyContext();
}
