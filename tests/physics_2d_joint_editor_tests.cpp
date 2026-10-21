#include "component_inspector.hpp"
#include "physics_overlay.hpp"
#include <forge/authoring.hpp>
#include <imgui_internal.h>
#include <iostream>

using namespace forge;
namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void near(Double3 value, Double3 expected, const char* message) {
    for (unsigned i = 0; i < 3; ++i)
        require(std::abs(value[i] - expected[i]) < 1e-5, message);
}
void exercise(const std::filesystem::path& root, float scale) {
    EngineContext engine{WorldRole::Authoring};
    Scene scene(engine.world());
    const auto project_root = root / AssetId::generate().str();
    SceneDocument::create_project(project_root, "Connected 2D Inspector");
    SceneDocument project(scene);
    project.open_project(project_root);
    const auto p = EntityId::generate().str(), a = EntityId::generate().str(),
               b = EntityId::generate().str(), j = EntityId::generate().str(),
               spatial = EntityId::generate().str();
    const auto rotation = [](float angle) {
        const auto q = rotation_about_axis({0, 0, 1}, angle);
        return Json{{"x", q.x}, {"y", q.y}, {"z", q.z}, {"w", q.w}};
    };
    Json parent{{"id", p}, {"name", "Reflected flat parent"}, {"spatial", {{"mode", "world"}}}};
    parent["components"] = {{"forge.local_translation", {{"x", 10}, {"y", 20}, {"z", 7}}},
                            {"forge.local_rotation", rotation(90)},
                            {"forge.local_scale", {{"x", -2}, {"y", 3}, {"z", 0}}}};
    Json first{{"id", a}, {"name", "Planar A"}, {"parent", p}};
    first["components"] = {{"forge.local_translation", {{"x", 1}, {"y", 2}, {"z", 5}}},
                           {"forge.local_scale", {{"x", -1}, {"y", 1}, {"z", 1}}},
                           {"forge.physics_body_2d", {{"motion", 0}}},
                           {"forge.box_collider_2d", {{"x", 1}, {"y", 1}}}};
    Json second{{"id", b}, {"name", "Planar B"}, {"spatial", {{"mode", "world"}}}};
    second["components"] = {{"forge.local_translation", {{"x", 2}, {"y", 3}, {"z", 9}}},
                            {"forge.local_rotation", rotation(-90)},
                            {"forge.local_scale", {{"x", -1}, {"y", 2}, {"z", 0}}},
                            {"forge.physics_body_2d", {{"motion", 2}}},
                            {"forge.box_collider_2d", {{"x", 1}, {"y", 1}}}};
    const Json joint_row{{"id", j}, {"name", "Planar connection"}, {"components", Json::object()}};
    const Json spatial_row{
        {"id", spatial}, {"name", "Spatial-only body"}, {"components", Json::object()}};
    scene.reset({{"version", 5},
                 {"asset_id", AssetId::generate()},
                 {"entities", Json::array({parent, first, second, joint_row, spatial_row})}});
    scene.entity(spatial).set<PhysicsBody>({});
    authoring_command(scene, "component.add",
                      {{"entity", j}, {"component", "forge.physics_joint_2d"}});
    Json schema;
    const auto catalog = scene.schema();
    for (const auto& type : catalog.at("components"))
        if (type.at("id") == "forge.physics_joint_2d")
            schema = type;
    require(!schema.is_null(), "Joint2D Inspector schema unavailable");
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1200, 900};
    io.DeltaTime = 1.f / 60;
    io.FontGlobalScale = scale;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::interface_scale = scale;
    ui::EditorUiContext context;
    context.scene = &scene;
    ui::ContextScope scope(context);
    test::observe_ui = true;
    Json field, value;
    ImVec2 minimum{}, maximum{};
    float height_per_item = 0;
    bool committed = false;
    auto begin_field = [&](const char* name) {
        if (!ImGui::GetCurrentContext()->OpenPopupStack.empty())
            ImGui::ClosePopupToLevel(0, true);
        field = nullptr;
        for (const auto& entry : schema.at("fields"))
            if (entry.at("id") == name)
                field = entry;
        require(!field.is_null(), "Missing Joint2D field");
        const auto document = scene.effective_document();
        for (const auto& row : document.at("entities"))
            if (row.at("id") == j)
                value = row.at("components").at("forge.physics_joint_2d").at(name);
        committed = false;
    };
    auto frame = [&] {
        test::ui_targets.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({1000, 780}, ImGuiCond_Always);
        ImGui::Begin("2D Joint Inspector", nullptr, ImGuiWindowFlags_NoResize);
        if (property_field(root, field, value)) {
            authoring_command(scene, "property.set",
                              {{"entity", j},
                               {"component", "forge.physics_joint_2d"},
                               {"field", field.at("id")},
                               {"value", value}});
            committed = true;
        }
        minimum = ImGui::GetItemRectMin();
        maximum = ImGui::GetItemRectMax();
        height_per_item = ImGui::GetTextLineHeightWithSpacing();
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
    for (const auto& [name, id] : {std::pair{"body_a", a}, std::pair{"body_b", b}}) {
        begin_field(name);
        frame();
        frame();
        click({(minimum.x + maximum.x) * .5f, (minimum.y + maximum.y) * .5f});
        // The real popup's virtualized list needs its measured auto-fit frame.
        frame();
        frame();
        if (!test::ui_targets.contains("entity-picker-row:" + a) ||
            !test::ui_targets.contains("entity-picker-row:" + b) ||
            test::ui_targets.contains("entity-picker-row:" + spatial)) {
            std::cerr << "Picker field=" << field.dump()
                      << " popups=" << ImGui::GetCurrentContext()->OpenPopupStack.size()
                      << " body type=" << scene.world().id<PhysicsBody2D>()
                      << " A.has=" << scene.entity(a).has<PhysicsBody2D>()
                      << " B.has=" << scene.entity(b).has<PhysicsBody2D>() << '\n';
            for (const auto& [key, target] : test::ui_targets)
                std::cerr << "observed " << key << '\n';
        }
        require(test::ui_targets.contains("entity-picker-row:" + a) &&
                    test::ui_targets.contains("entity-picker-row:" + b) &&
                    !test::ui_targets.contains("entity-picker-row:" + spatial) &&
                    !test::ui_targets.contains("entity-picker-row:" + p) &&
                    !test::ui_targets.contains("entity-picker-row:" + j),
                "Actual2D joint picker did not filter other component domains");
        const auto target = test::ui_targets.at("entity-picker-row:" + id);
        click({(target.minimum.x + target.maximum.x) * .5f,
               (target.minimum.y + target.maximum.y) * .5f});
        require(committed && value == Json(scene.reference(id)),
                "Body picker lost typed canonical reference");
    }
    begin_field("kind");
    frame();
    frame();
    click({(minimum.x + maximum.x) * .5f, (minimum.y + maximum.y) * .5f});
    const auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
    require(!popups.empty() && popups.back().Window, "Joint kind selector did not open");
    auto* popup = popups.back().Window;
    click({popup->Pos.x + ImGui::GetStyle().WindowPadding.x + 24,
           popup->DC.CursorStartPos.y + 2.5f * height_per_item});
    require(committed && scene.entity(j).get<PhysicsJoint2D>().kind == 2,
            "Slider selector did not commit distinct2D kind2");
    require(scene.undo() && scene.entity(j).get<PhysicsJoint2D>().kind == 0 && scene.redo() &&
                scene.entity(j).get<PhysicsJoint2D>().kind == 2,
            "Joint kind selector did not preserve typed Undo/Redo");
    ComponentInspector inspector;
    inspector.fixture_focus_component = "forge.physics_joint_2d";
    for (unsigned kind : {0u, 1u, 2u}) {
        authoring_command(scene, "property.set",
                          {{"entity", j},
                           {"component", "forge.physics_joint_2d"},
                           {"field", "kind"},
                           {"value", kind}});
        for (unsigned settle = 0; settle < 3; ++settle) {
            test::ui_targets.clear();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({1000, 780}, ImGuiCond_Always);
            ImGui::Begin("Full 2D Joint Inspector", nullptr, ImGuiWindowFlags_NoResize);
            inspector.draw(scene, project, j);
            ImGui::End();
            ImGui::Render();
        }
        const auto visible = [&](const char* name) {
            return test::ui_targets.contains(
                std::string("component-field:forge.physics_joint_2d:") + name);
        };
        require(visible("body_a") && visible("body_b") && visible("anchor_a_x") &&
                    visible("anchor_b_y") && visible("collide_connected"),
                "Full Inspector lost shared 2D joint references/anchors/collision controls");
        require(visible("distance") == (kind == 0) && visible("spring_frequency") == (kind == 0) &&
                    visible("spring_damping") == (kind == 0) &&
                    visible("frame_a_angle") == (kind != 0) &&
                    visible("limits_enabled") == (kind != 0) &&
                    visible("motor_enabled") == (kind != 0) &&
                    visible("min_angle") == (kind == 1) &&
                    visible("hinge_motor_speed") == (kind == 1) &&
                    visible("max_motor_torque") == (kind == 1) &&
                    visible("min_travel") == (kind == 2) &&
                    visible("slider_motor_speed") == (kind == 2) &&
                    visible("max_motor_force") == (kind == 2),
                "Full ComponentInspector displayed the wrong joint-kind fields");
    }
    for (const auto& [name, x] :
         {std::pair{"anchor_a_x", .5}, std::pair{"anchor_a_y", -.5}, std::pair{"anchor_b_x", 1.},
          std::pair{"anchor_b_y", 2.}, std::pair{"frame_b_angle", 90.}})
        authoring_command(scene, "property.set",
                          {{"entity", j},
                           {"component", "forge.physics_joint_2d"},
                           {"field", name},
                           {"value", x}});
    const auto before = scene.document();
    auto document = scene.effective_document();
    auto guide = ui::prepare_physics_joint_2d_guide(document, j);
    require(guide && guide->valid && guide->planar && guide->kind == 4,
            "2D Slider guide unavailable");
    // Independent T(10,20,7) Rz90 S(-2,3,0), then child T(1,2,5) S(-1,1,1).
    near(guide->bodies[0].anchor, {5.5, 19, 7}, "Flat reflected ancestry changed anchor A");
    near(guide->bodies[1].anchor, {6, 4, 9}, "Reflected rotated body changed anchor B");
    near(guide->bodies[0].axes[0], {0, 1, 0}, "Joint frame A included signed scale");
    near(guide->bodies[1].axes[0], {1, 0, 0}, "Joint frame B ignored its authored angle");
    ui::PhysicsOverlay overlay;
    EditorCamera camera;
    camera.target = {5, 10, 8};
    camera.distance = 35;
    auto draw = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({1200, 900}, ImGuiCond_Always);
        ImGui::Begin("2D Joint Scene", nullptr, ImGuiWindowFlags_NoScrollbar);
        const auto count = ImGui::GetWindowDrawList()->VtxBuffer.Size;
        const auto command = ImGui::GetWindowDrawList()->CmdBuffer.Size;
        const auto active = ImGui::GetCurrentContext()->ActiveId;
        overlay.draw(document, j, camera, {20, 40}, {1160, 820}, {}, {});
        require(active == ImGui::GetCurrentContext()->ActiveId, "Joint guide captured UI input");
        const auto added = ImGui::GetWindowDrawList()->VtxBuffer.Size - count;
        for (int i = command; i < ImGui::GetWindowDrawList()->CmdBuffer.Size; ++i) {
            const auto& cmd = ImGui::GetWindowDrawList()->CmdBuffer[i];
            if (cmd.ElemCount)
                require(cmd.ClipRect.x >= 20 && cmd.ClipRect.y >= 40 && cmd.ClipRect.z <= 1180 &&
                            cmd.ClipRect.w <= 860,
                        "2D joint guide escaped Scene clipping");
        }
        for (const auto& vertex : ImGui::GetWindowDrawList()->VtxBuffer)
            require(std::isfinite(vertex.pos.x) && std::isfinite(vertex.pos.y),
                    "Nonfinite joint guide vertex");
        ImGui::End();
        ImGui::Render();
        return added;
    };
    require(draw() > 0 && overlay.status().starts_with("Authored 2D Slider joint") &&
                overlay.status().find("anchor gap 15.008 m") != std::string::npos &&
                !overlay.ready(),
            "2D guide/status used visual depth as a physical gap or claimed a realized "
            "collision");
    for (auto& row : document["entities"])
        if (row.at("id") == j) {
            row["components"]["forge.physics_joint_2d"]["kind"] = 1;
            row["components"]["forge.physics_joint_2d"]["motor_enabled"] = true;
        }
    const auto limited_vertices = draw();
    require(overlay.status().find("limits [-90.000, 90.000] deg") != std::string::npos,
            "Hinge limits missing from authored guide status");
    for (auto& row : document["entities"])
        if (row.at("id") == j)
            row["components"]["forge.physics_joint_2d"]["limits_enabled"] = false;
    require(draw() < limited_vertices && overlay.status().find("limits off") != std::string::npos,
            "Disabled hinge limits still draw an authored stop arc");
    overlay.joint_visible = false;
    require(draw() == 0 && overlay.status().empty(), "Joint visibility left stale geometry/status");
    require(scene.document() == before && !engine.services().available(Capability::Physics),
            "Copied joint guides changed authored data or instantiated a solver");
    test::observe_ui = false;
    ImGui::DestroyContext();
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected build-owned Joint2D editor scratch");
        const auto root = std::filesystem::path(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        for (float scale : {1.f, 1.5f, 2.f})
            exercise(root, scale);
        std::filesystem::remove_all(root);
        std::cout << "Actual2D joint selectors/reference filtering/history and copied "
                     "reflected/flat "
                     "Scene guides/clipping at100/150/200% passed; no GPU acceptance\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
