#pragma once
#include "property_drawer.hpp"
#include <forge/authoring.hpp>
void require(bool condition, const char* message);
inline void test_typed_ui_layer() {
    forge::EngineContext engine;
    forge::Scene scene(engine.world());
    const std::string entity = forge::authoring_command(scene, "entity.create").at("selected");
    forge::authoring_command(scene, "component.add",
                             {{"entity", entity}, {"component", "forge.ui_document"}});
    forge::Json field;
    const auto schema = scene.schema();
    for (const auto& c : schema.at("components"))
        if (c.at("id") == "forge.ui_document")
            for (const auto& f : c.at("fields"))
                if (f.at("id") == "layer")
                    field = f;
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800, 600};
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    forge::Json value = 0u;
    ImVec2 target{};
    bool committed = false;
    auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({600, 300});
        ImGui::Begin("Typed field test");
        if (forge::property_field({}, field, value)) {
            forge::authoring_command(scene, "property.set",
                                     {{"entity", entity},
                                      {"component", "forge.ui_document"},
                                      {"field", "layer"},
                                      {"value", value}});
            committed = true;
        }
        target = ImGui::GetItemRectMin();
        target.x += 20;
        target.y += 8;
        ImGui::End();
        ImGui::Render();
    };
    frame();
    frame();
    io.AddMousePosEvent(target.x, target.y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_A, true);
    frame();
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    frame();
    io.AddInputCharactersUTF8("7");
    frame();
    io.AddKeyEvent(ImGuiKey_Enter, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Enter, false);
    frame();
    require(committed && value.is_number_unsigned() && value == 7u,
            "Actual UI layer input did not commit an unsigned integer");
    forge::authoring_history(scene, false);
    require(scene.effective_document()
                    .at("entities")[0]
                    .at("components")
                    .at("forge.ui_document")
                    .at("layer") == 0u,
            "Layer edit did not undo");
    ImGui::DestroyContext();
}
