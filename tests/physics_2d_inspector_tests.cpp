#include "component_inspector.hpp"
#include "property_drawer.hpp"
#include <forge/authoring.hpp>
#include <forge/physics.hpp>
#include <fstream>
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
    Json one_way_field;
    for (const auto& field : schema.at("fields"))
        if (field.at("id") == "one_way")
            one_way_field = field;
    require(!one_way_field.is_null() && one_way_field.at("display_name") == "One-way collision",
            "Inspector did not discover one-way response");
    const auto edit_field = [&](const Json& field, const char* component, const char* member,
                                const char* text = nullptr) {
        Json value =
            scene.effective_document().at("entities")[0].at("components").at(component).at(member);
        ImVec2 checkbox{};
        const auto frame = [&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({700, 500}, ImGuiCond_Always);
            ImGui::Begin("Planar property Inspector");
            if (property_field(project, field, value))
                authoring_command(scene, "property.set",
                                  {{"entity", entity},
                                   {"component", component},
                                   {"field", member},
                                   {"value", value}});
            const auto position = ImGui::GetItemRectMin();
            checkbox = {position.x + ImGui::GetFrameHeight() * .5f,
                        position.y + ImGui::GetFrameHeight() * .5f};
            ImGui::End();
            ImGui::Render();
        };
        frame();
        frame();
        io.AddMousePosEvent(checkbox.x, checkbox.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        if (text) {
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            io.AddKeyEvent(ImGuiKey_A, true);
            frame();
            io.AddKeyEvent(ImGuiKey_A, false);
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            frame();
            io.AddInputCharactersUTF8(text);
            frame();
            io.AddKeyEvent(ImGuiKey_Enter, true);
            frame();
            io.AddKeyEvent(ImGuiKey_Enter, false);
            frame();
        }
    };
    edit_field(one_way_field, "forge.physics_body_2d", "one_way");
    require(scene.entity(entity).get<PhysicsBody2D>().one_way && scene.undo() &&
                !scene.entity(entity).get<PhysicsBody2D>().one_way && scene.redo() &&
                scene.entity(entity).get<PhysicsBody2D>().one_way,
            "One-way checkbox did not share typed Inspector history");
    authoring_command(scene, "component.add",
                      {{"entity", entity}, {"component", "forge.character_controller_2d"}});
    Json carry_field, slope_field, ceiling_field, step_field;
    const auto controller_schema = scene.schema();
    for (const auto& component : controller_schema.at("components"))
        if (component.at("id") == "forge.character_controller_2d")
            for (const auto& field : component.at("fields"))
                if (field.at("id") == "platform_carry")
                    carry_field = field;
                else if (field.at("id") == "floor_stop_on_slope")
                    slope_field = field;
                else if (field.at("id") == "slide_on_ceiling")
                    ceiling_field = field;
                else if (field.at("id") == "step_height")
                    step_field = field;
    require(carry_field.at("display_name") == "Follow moving platforms",
            "Inspector did not discover the platform carry setting");
    require(slope_field.at("display_name") == "Stop on slopes" &&
                ceiling_field.at("display_name") == "Slide on ceilings",
            "Inspector did not discover surface response settings");
    require(step_field.at("display_name") == "Step Height" && step_field.at("unit") == "m" &&
                step_field.at("maximum") == 1,
            "Inspector did not discover bounded world-space step height");
    edit_field(step_field, "forge.character_controller_2d", "step_height", "0.3");
    require(scene.entity(entity).get<CharacterController2D>().step_height == .3f && scene.undo() &&
                scene.entity(entity).get<CharacterController2D>().step_height == 0 &&
                scene.redo() &&
                scene.entity(entity).get<CharacterController2D>().step_height == .3f,
            "Actual Step Height numeric edit lost typed native value or history");
    edit_field(slope_field, "forge.character_controller_2d", "floor_stop_on_slope");
    require(scene.entity(entity).get<CharacterController2D>().floor_stop_on_slope && scene.undo() &&
                !scene.entity(entity).get<CharacterController2D>().floor_stop_on_slope &&
                scene.redo(),
            "Slope checkbox lost shared typed history");
    edit_field(ceiling_field, "forge.character_controller_2d", "slide_on_ceiling");
    require(!scene.entity(entity).get<CharacterController2D>().slide_on_ceiling && scene.undo() &&
                scene.entity(entity).get<CharacterController2D>().slide_on_ceiling && scene.redo(),
            "Ceiling checkbox lost shared typed history");
    edit_field(carry_field, "forge.character_controller_2d", "platform_carry");
    require(scene.entity(entity).get<CharacterController2D>().platform_carry && scene.undo() &&
                !scene.entity(entity).get<CharacterController2D>().platform_carry && scene.redo() &&
                scene.entity(entity).get<CharacterController2D>().platform_carry,
            "Moving platform checkbox did not preserve typed Inspector history");
    const auto captured = project / ("stance-inspector-" + std::to_string(scale) + ".log");
    std::filesystem::remove(captured);
    ImGui::NewFrame();
    ImGui::Begin("Copied character Inspector");
    ImGui::LogToFile(0, captured.string().c_str());
    ComponentInspector inspector;
    inspector.draw_character_state({{"domain", "2d"},
                                    {"ground", 3},
                                    {"velocity", {0, 0}},
                                    {"crouched", true},
                                    {"shape_blocked", true},
                                    {"stance_blocked", true},
                                    {"placement_blocked", false}});
    ImGui::LogFinish();
    ImGui::End();
    ImGui::Render();
    std::ifstream reading(captured);
    const std::string text{std::istreambuf_iterator<char>(reading), {}};
    require(text.find("Crouched") != std::string::npos &&
                text.find("Standing blocked by solid geometry.") != std::string::npos &&
                text.find("Placement blocked") == std::string::npos,
            "Actual Inspector confused a blocked stand with a blocked placement");
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
