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
    auto frame = [&] {
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
