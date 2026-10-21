#include "property_drawer.hpp"
#include <forge/authoring.hpp>
#include <forge/physics.hpp>
#include <imgui_internal.h>
#include <iostream>

using namespace forge;
namespace {
void require(bool condition, const char* text) {
    if (!condition)
        throw std::runtime_error(text);
}
void exercise(const std::filesystem::path& project, float scale) {
    ProjectSettings settings(project);
    auto configuration = settings.document();
    auto physics = settings.physics();
    physics.layers[1] = "World";
    physics.layers[2] = "Sensors";
    configuration["physics"] = {
        {"version", 1}, {"gravity", physics.gravity}, {"layers", physics.layers}};
    settings.save(configuration);
    EngineContext engine{WorldRole::Authoring, false, {physics_schema_module()}};
    Scene scene(engine.world());
    const auto entity = authoring_command(scene, "entity.create").at("selected").get<std::string>();
    authoring_command(scene, "component.add",
                      {{"entity", entity}, {"component", "forge.physics_body_2d"}});
    Json schema;
    const auto catalog = scene.schema();
    for (const auto& component : catalog.at("components"))
        if (component.at("id") == "forge.physics_body_2d")
            schema = component;
    require(schema.at("display_name") == "Physics Body 2D",
            "Component picker label lost 2D spelling");
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800, 600};
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    io.FontGlobalScale = scale;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::interface_scale = scale;
    auto choose = [&](const char* name, unsigned index) {
        if (!ImGui::GetCurrentContext()->OpenPopupStack.empty())
            ImGui::ClosePopupToLevel(0, true);
        Json field;
        for (const auto& entry : schema.at("fields"))
            if (entry.at("id") == name)
                field = entry;
        require(!field.is_null(), "Missing Body2D property field");
        Json value = scene.effective_document()
                         .at("entities")[0]
                         .at("components")
                         .at("forge.physics_body_2d")
                         .at(name);
        ImVec2 minimum{}, maximum{};
        float item_height = 0;
        bool committed = false;
        auto frame = [&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({700, 500}, ImGuiCond_Always);
            ImGui::Begin("Planar Inspector");
            if (property_field(project, field, value)) {
                authoring_command(scene, "property.set",
                                  {{"entity", entity},
                                   {"component", "forge.physics_body_2d"},
                                   {"field", name},
                                   {"value", value}});
                committed = true;
            }
            minimum = ImGui::GetItemRectMin();
            maximum = ImGui::GetItemRectMax();
            item_height = ImGui::GetTextLineHeightWithSpacing();
            ImGui::End();
            ImGui::Render();
        };
        auto click = [&](ImVec2 at) {
            io.AddMousePosEvent(at.x, at.y);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
        };
        frame();
        frame();
        click({(minimum.x + maximum.x) * .5f, (minimum.y + maximum.y) * .5f});
        const auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
        require(!popups.empty() && popups.back().Window,
                "2D Inspector field did not open a selector");
        auto* popup = popups.back().Window;
        const auto& style = ImGui::GetStyle();
        const ImVec2 item{popup->Pos.x + style.WindowPadding.x + 24,
                          popup->DC.CursorStartPos.y + (index + .5f) * item_height};
        click(item);
        require(committed && value.is_number_unsigned(), "2D selector failed typed history commit");
        return value.get<std::uint32_t>();
    };
    require(choose("motion", 2) == 2 && scene.entity(entity).get<PhysicsBody2D>().motion == 2,
            "Dynamic motion selector did not commit numeric2");
    require(choose("layer", 2) == 2 && scene.entity(entity).get<PhysicsBody2D>().layer == 2,
            "Named layer selector did not use stable project slot");
    require(choose("mask", 1) == 0, "No layers did not clear the mask");
    require(choose("mask", 3) == 2, "World layer did not map to mask bit1");
    require(choose("mask", 4) == 6, "Sensors layer did not map to mask bit2");
    require(scene.undo() && scene.entity(entity).get<PhysicsBody2D>().mask == 2 && scene.redo() &&
                scene.entity(entity).get<PhysicsBody2D>().mask == 6,
            "Mask selector Undo/Redo lost typed intent");
    require(!engine.services().available(Capability::Physics), "Inspector created a solver");
    ImGui::DestroyContext();
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Provide build-owned scratch");
        const auto project = std::filesystem::path(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(project);
        for (float scale : {1.f, 1.5f, 2.f})
            exercise(project, scale);
        std::filesystem::remove_all(project);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "Actual Body2D Inspector selectors: named motion/layers/masks, typed Undo/Redo "
                 "at100/150/200% passed; no native pixel acceptance\n";
}
