#include "editor/foliage_placement.hpp"
#include "imgui_input_driver.hpp"
#include "imgui_software_capture.hpp"
#include "terrain_authoring.hpp"
#include <chrono>
#include <imgui_internal.h>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
void check(bool v, const char* why) {
    if (!v)
        throw std::runtime_error(why);
}
std::string cube(Scene& scene) {
    return authoring_command(scene, "entity.create",
                             {{"recipe", "primitive.0"}, {"name", "Fern template"}})
        .at("selected");
}
void xy_controls(float scale, bool compact, const std::filesystem::path& scratch) {
    EngineContext engine;
    Scene scene(engine.world());
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root / "Assets");
    SceneDocument project(scene);
    project.open_project(root, true);
    const auto source = cube(scene), obstacle = cube(scene), obstacle3d = cube(scene);
    for (auto type : {"forge.physics_body_2d", "forge.box_collider_2d"})
        authoring_command(scene, "component.add", {{"entity", obstacle}, {"component", type}});
    authoring_command(scene, "transform.position",
                      {{"entity", obstacle}, {"value", {{"x", 0}, {"y", .5}, {"z", 0}}}});
    for (auto type : {"forge.physics_body", "forge.box_collider"})
        authoring_command(scene, "component.add", {{"entity", obstacle3d}, {"component", type}});
    authoring_command(scene, "property.set",
                      {{"entity", obstacle3d},
                       {"component", "forge.physics_body"},
                       {"field", "motion"},
                       {"value", 0}});
    authoring_command(scene, "transform.position",
                      {{"entity", obstacle3d}, {"value", {{"x", 0}, {"y", .5}, {"z", 0}}}});
    const auto before = scene.document();
    std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context(ImGui::CreateContext(),
                                                                            ImGui::DestroyContext);
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    io.DisplaySize = {compact ? 460 * scale : 1000 * scale, 900 * scale};
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::style(scale);
    ui::EditorUiContext owner;
    ui::ContextScope scope(owner);
    owner.selection.select_entity(source);
    ui::FoliagePlacement panel;
    panel.open = true;
    test::observe_ui = true;
    auto frame = [&] {
        test::ui_targets.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
        ImGui::SetWindowSize("Foliage placement", {(compact ? 400.f : 650.f) * scale, 720 * scale},
                             ImGuiCond_Always);
        panel.draw(scene, owner.selection, false, &project);
        ImGui::Render();
    };
    test::ImGuiInputDriver input(frame);
    for (unsigned i = 0; i < 4; ++i)
        frame();
    input.click("foliage:source");
    input.type("foliage:count", "32");
    input.type("foliage:radius", "3");
    input.type("foliage:spacing", "0");
    input.type("foliage:y", "0");
    input.click("foliage:collision");
    input.type("foliage:clearance_z", "25");
    const auto settle = [&] {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (panel.collision_task().busy()) {
            check(std::chrono::steady_clock::now() < end, "XY UI clearance stalled");
            frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        frame();
    };
    for (unsigned profile : {2u, 3u, 4u, 5u}) {
        const bool surfaces = profile >= 4, xy = profile != 4;
        input.choose("foliage:clearance_profile", profile);
        if (profile == 4)
            input.type("foliage:clearance_z", "0");
        check(test::ui_targets.at("foliage:clearance_z").enabled == !xy,
              "Clearance profile lost its active Z offset control state");
        if (surfaces)
            check(!test::ui_targets.at("foliage:clearance_padding").enabled &&
                      !test::ui_targets.contains("foliage:clearance_radius"),
                  "Surface profile exposed inactive radius or padding controls");
        if (profile == 2) {
            input.type("foliage:clearance_radius", ".001");
            input.click("foliage:check");
            settle();
            check(owner.problems.size() == 1 && scene.document() == before,
                  "Unsupported XY circle radius bypassed native UI preflight");
            input.type("foliage:clearance_radius", ".005");
            input.click("foliage:check");
            settle();
            check(owner.problems.size() == 0 &&
                      panel.collision_task().state().at("state") == "ready" &&
                      scene.document() == before,
                  "Native minimum XY circle radius failed to clear the UI diagnostic");
        }
        if (!surfaces)
            input.type(profile == 2 ? "foliage:clearance_radius" : "foliage:clearance_padding",
                       profile == 2 ? ".25" : ".1");
        input.click("foliage:check");
        settle();
        const auto state = panel.collision_task().state();
        check(state.at("state") == "ready" &&
                  state.at("profile") == foliage_clearance_choices()[profile].key &&
                  state.at("retained_count") > 0 && state.at("retained_count") < 32 &&
                  state.at("result").at("settings").at("offset").at("z") ==
                      (profile < 4 ? 25 : 0) &&
                  scene.document() == before && owner.problems.size() == 0,
              "Actual XY profile controls lost native filtering, settings or read-only intent");
        if (surfaces)
            check(state.at("surface_triangle_count") == 12 && state.at("shape_query_count") > 0 &&
                      state.at("result").at("settings").at("padding") == .1,
                  "Actual surface controls lost query progress or retained inactive padding");
        input.click("foliage:place");
        settle();
        const auto placed = scene.document();
        check(scene.undo() && scene.document() == before && scene.redo() &&
                  scene.document() == placed && scene.undo() && scene.document() == before,
              "Actual XY placement lost atomic history");
    }
    input.choose("foliage:clearance_profile", 3);
    check(test::ui_targets.at("foliage:clearance_padding").enabled,
          "Returning to Mesh bounds failed to restore padding editing");
    input.choose("foliage:clearance_profile", 0);
    check(test::ui_targets.at("foliage:clearance_z").enabled,
          "Returning to 3D failed to restore the Z offset control");
    input.type("foliage:clearance_mask", "0");
    input.choose("foliage:clearance_profile", 2);
    input.type("foliage:clearance_radius", "100");
    input.click("foliage:separate_new");
    input.click("foliage:check");
    settle();
    check(panel.collision_task().state().at("retained_count") == 1 &&
              panel.collision_task().state().at("new_overlap_count") == 31 &&
              scene.document() == before,
          "Actual XY separation did not use the shared ordered candidate filter");
    test::observe_ui = false;
}
void exercise(float scale, bool compact, const std::filesystem::path& scratch) {
    EngineContext engine;
    Scene scene(engine.world());
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root / "Assets");
    SceneDocument project(scene);
    project.open_project(root, true);
    const auto source = cube(scene);
    const auto before = scene.document();
    std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context(ImGui::CreateContext(),
                                                                            ImGui::DestroyContext);
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    io.DisplaySize = {compact ? 460 * scale : 1000 * scale, 900 * scale};
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::style(scale);
    ui::EditorUiContext owner;
    ui::ContextScope scope(owner);
    owner.selection.select_entity(source);
    ui::FoliagePlacement panel;
    panel.open = true;
    bool locked = false;
    bool brush_scene = false;
    EntityId brush_surface;
    AssetId brush_mesh;
    std::string brush_revision;
    Double3 brush_center{};
    ImVec2 brush_origin{}, brush_size{300 * scale, 70 * scale};
    EditorCamera brush_camera;
    test::observe_ui = true;
    auto frame = [&] {
        test::ui_targets.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
        ImGui::SetWindowSize("Foliage placement", {(compact ? 400.f : 650.f) * scale, 720 * scale},
                             ImGuiCond_Always);
        panel.draw(scene, owner.selection, locked, &project);
        if (brush_scene) {
            ImGui::SetNextWindowPos({10 * scale, 735 * scale}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({350 * scale, 155 * scale}, ImGuiCond_Always);
            ImGui::Begin("Brush controller Scene", nullptr,
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
            brush_origin = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("Surface", brush_size, ImGuiButtonFlags_MouseButtonLeft);
            panel.surface_input(
                scene, owner.selection, &project, brush_camera, brush_origin, brush_size,
                ImGui::IsWindowFocused(), !locked, ImGui::IsItemHovered(), ImGui::IsItemActivated(),
                ImGui::IsItemActive(), [&](double x, double) -> std::optional<MeshSurfaceHit> {
                    // The GPU fixture proves retained picking. This controller
                    // fixture supplies points inside the real published terrain.
                    return MeshSurfaceHit{brush_surface,
                                          brush_mesh,
                                          brush_revision,
                                          "",
                                          0,
                                          0,
                                          {brush_center[0] + (x / brush_size.x - .5) * 2,
                                           brush_center[1], brush_center[2]}};
                });
            ImGui::End();
        }
        ImGui::Render();
    };
    test::ImGuiInputDriver input(frame);
    for (unsigned i = 0; i < 4; ++i)
        frame();
    const auto* window = ImGui::FindWindowByName("Foliage placement");
    check(window && std::abs(window->Size.x - (compact ? 400.f : 650.f) * scale) < 1 &&
              std::abs(window->Size.y - 720 * scale) < 1,
          "Fixture did not exercise its requested full/compact layout");
    input.click("foliage:source");
    input.type("foliage:name", "Meadow");
    input.type("foliage:count", "12");
    input.type("foliage:radius", "3.5");
    input.type("foliage:spacing", "0.6");
    input.type("foliage:seed", "42");
    input.type("foliage:y", "2");
    input.click("foliage:check");
    check(scene.document() == before && owner.problems.size() == 0,
          "Layout check mutated Scene or reported valid input as invalid");
    input.click("foliage:place");
    const auto placed = scene.document();
    check(placed.at("entities").size() == 14 && owner.selection.entity() != source &&
              placed.at("entities").at(1).at("name") == "Meadow" && owner.problems.size() == 0,
          "Actual placement controls did not create/select one named group");
    check(scene.undo() && scene.document() == before && scene.redo() && scene.document() == placed,
          "Actual placement control did not use one normal history step");
    input.type("foliage:count", "513");
    input.click("foliage:place");
    check(scene.document() == placed && owner.problems.size() == 1 &&
              !owner.problems.items().front().occurred_at.empty(),
          "Rejected actual input mutated Scene or lacked timestamped Problem");
    input.type("foliage:count", "1");
    input.click("foliage:check");
    check(owner.problems.size() == 0, "Corrected actual layout retained its Problem");
    auto terrain =
        TerrainDocument::create(project.writer_guard(), "Assets/Ground.terrain.json",
                                [](AssetId id) { return TerrainSource::create(id, 3, 8); });
    terrain->edit(terrain->revision(), "Ground slope", [](Json& json) {
        json["tiles"][0]["layers"][0]["heights"] = {0, 2, 4, 1, 3, 5, 2, 4, 6};
    });
    terrain->save();
    AssetImportService imports(project.writer_guard(), terrain_import_registry(),
                               {"linux", "none", "cpu"});
    const auto terrain_id = terrain->source().asset();
    imports.submit(
        imports.prepare(terrain->locator(), {}, terrain_id),
        [](auto& candidate, const auto& plan, const auto&) {
            prepare_terrain_publication(candidate, plan);
        },
        [](const auto&, const auto&) {});
    check(imports.wait_idle(std::chrono::seconds(20)), "UI terrain publication timed out");
    const auto receipts = imports.poll();
    check(receipts.size() == 1 && receipts[0].published, "UI terrain did not publish");
    const auto surface = cube(scene);
    authoring_command(scene, "property.set",
                      {{"entity", surface},
                       {"component", "forge.mesh_renderer"},
                       {"field", "mesh"},
                       {"value", terrain_id}});
    owner.selection.select_entity(surface);
    frame();
    input.click("foliage:projected");
    input.click("foliage:terrain");
    input.type("foliage:x", "4");
    input.type("foliage:z", "4");
    input.type("foliage:y", "7");
    input.type("foliage:height_offset", "0.35");
    input.type("foliage:slope_min", "0");
    input.type("foliage:slope_max", "10");
    const auto before_slope_rejection = scene.document();
    input.click("foliage:check");
    check(scene.document() == before_slope_rejection && owner.problems.size() == 1,
          "Slope-filtered empty UI layout changed Scene or failed to report rejection");
    input.type("foliage:slope_max", "40");
    input.click("foliage:align_slope");
    input.type("foliage:radius", "0.8");
    input.type("foliage:spacing", "0.2");
    input.type("foliage:count", "8");
    const auto before_projection = scene.document();
    input.click("foliage:check");
    check(scene.document() == before_projection && owner.problems.size() == 0,
          "Actual terrain check changed the Scene or rejected valid terrain");
    input.click("foliage:place");
    const auto projected = scene.document();
    const auto group = owner.selection.entity();
    check(projected.at("entities").size() == before_projection.at("entities").size() + 9 &&
              owner.problems.size() == 0,
          "Actual terrain placement failed");
    const auto effective = scene.effective_document();
    Double3 ground_origin;
    for (const auto& row : effective.at("entities"))
        if (row.at("id") == surface) {
            const auto& m = row.at("world_affine");
            ground_origin = {m[3], m[7], m[11]};
        }
    unsigned on_surface = 0;
    for (const auto& row : effective.at("entities"))
        if (row.value("parent", "") == group) {
            const auto& m = row.at("world_affine");
            check(std::abs(m[7].get<double>() -
                           (ground_origin[1] + .5 * (m[3].get<double>() - ground_origin[0]) +
                            .25 * (m[11].get<double>() - ground_origin[2]) + .35)) < 1e-8,
                  "Actual terrain controls produced off-surface origins");
            check(std::abs(m[1].get<double>() / m[5].get<double>() + .5) < 2e-6 &&
                      std::abs(m[9].get<double>() / m[5].get<double>() + .25) < 2e-6,
                  "Actual Align to terrain slope control did not orient the placement frame");
            ++on_surface;
        }
    check(on_surface == 8 && scene.undo() && scene.document() == before_projection &&
              scene.redo() && scene.document() == projected,
          "Terrain UI did not use one Scene history entry");
    const auto tilt = rotation_about_axis({1, 0, 0}, 15);
    authoring_command(scene, "transform.local",
                      {{"entity", surface},
                       {"rotation", {{"x", tilt.x}, {"y", tilt.y}, {"z", tilt.z}, {"w", tilt.w}}}});
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    input.click("foliage:surface_kind");
    const auto press = [&](ImGuiKey key) {
        io.AddKeyEvent(key, true);
        frame();
        io.AddKeyEvent(key, false);
        frame();
    };
    press(ImGuiKey_Home);
    press(ImGuiKey_DownArrow);
    press(ImGuiKey_Enter);
    const auto before_mesh = scene.document();
    input.click("foliage:check");
    check(scene.document() == before_mesh && owner.problems.size() == 0,
          "Actual static Mesh selection did not accept tilted published triangles");
    input.click("foliage:place");
    const auto mesh_placed = scene.document();
    const auto mesh_group = owner.selection.entity();
    unsigned mesh_count = 0;
    const double c = std::cos(15 * std::acos(-1.) / 180), s = std::sin(15 * std::acos(-1.) / 180),
                 denominator = c + .25 * s;
    const auto mesh_effective = scene.effective_document();
    for (const auto& row : mesh_effective.at("entities"))
        if (row.value("parent", "") == mesh_group) {
            const auto& m = row.at("world_affine");
            const double x = m[3].get<double>() - ground_origin[0],
                         z = m[11].get<double>() - ground_origin[2];
            check(std::abs(m[7].get<double>() -
                           (ground_origin[1] + (.5 * x + (.25 * c - s) * z) / denominator + .35)) <
                      2e-6,
                  "Native Mesh projection differs from the independently tilted plane");
            check(std::abs(m[1].get<double>() / m[5].get<double>() + .5 / denominator) < 2e-6 &&
                      std::abs(m[9].get<double>() / m[5].get<double>() -
                               (s - .25 * c) / denominator) < 2e-6,
                  "Native Mesh slope alignment did not use the tilted geometric normal");
            ++mesh_count;
        }
    check(mesh_count == 8 && scene.undo() && scene.document() == before_mesh && scene.redo() &&
              scene.document() == mesh_placed,
          "Mesh surface controls did not use one ordinary Scene history entry");
    input.click("foliage:projected");
    owner.selection.select_entity(source);
    input.click("foliage:source");
    input.type("foliage:count", "32");
    input.type("foliage:radius", "3");
    input.type("foliage:spacing", "0");
    input.type("foliage:y", "0");
    const auto obstacle = cube(scene);
    for (auto type : {"forge.physics_body", "forge.box_collider"})
        authoring_command(scene, "component.add", {{"entity", obstacle}, {"component", type}});
    authoring_command(scene, "property.set",
                      {{"entity", obstacle},
                       {"component", "forge.physics_body"},
                       {"field", "motion"},
                       {"value", 0}});
    authoring_command(
        scene, "property.set",
        {{"entity", obstacle}, {"component", "forge.box_collider"}, {"field", "z"}, {"value", 10}});
    authoring_command(scene, "transform.position",
                      {{"entity", obstacle}, {"value", {{"x", 0}, {"y", .5}, {"z", 0}}}});
    const auto clearance_before = scene.document();
    input.click("foliage:collision");
    input.type("foliage:clearance_radius", ".25");
    input.type("foliage:clearance_x", "0");
    input.type("foliage:clearance_y", ".5");
    input.type("foliage:clearance_z", "0");
    input.click("foliage:sensors");
    input.click("foliage:sensors");
    auto settle = [&] {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (panel.collision_task().busy()) {
            check(std::chrono::steady_clock::now() < end, "UI clearance stalled");
            frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        frame();
    };
    input.type("foliage:clearance_mask", "0");
    input.click("foliage:check");
    settle();
    check(scene.document() == clearance_before &&
              panel.collision_task().state().at("result").at("retained_count") == 32,
          "Actual clearance layer mask did not use the shared native task");
    input.choose("foliage:clearance_profile", 1);
    input.type("foliage:clearance_padding", ".125");
    input.type("foliage:clearance_y", "0");
    input.click("foliage:check");
    settle();
    const auto bounded = panel.collision_task().state();
    check(bounded.at("state") == "ready" && bounded.at("profile") == "mesh-bounds-v1" &&
              bounded.at("result").at("settings").at("padding") == .125 &&
              bounded.at("result").at("retained_count") == 32 &&
              scene.document() == clearance_before,
          "Actual Mesh bounds controls failed to prepare the shared native task");
    input.click("foliage:place");
    settle();
    const auto bounded_scene = scene.document();
    check(scene.undo() && scene.document() == clearance_before && scene.redo() &&
              scene.document() == bounded_scene && scene.undo() &&
              scene.document() == clearance_before,
          "Actual Mesh bounds placement lost its ordinary Scene history");
    input.choose("foliage:clearance_profile", 0);
    input.type("foliage:clearance_y", ".5");
    input.type("foliage:clearance_radius", "1");
    input.click("foliage:separate_new");
    input.click("foliage:check");
    settle();
    const auto separated = panel.collision_task().state().at("result");
    check(scene.document() == clearance_before && separated.at("retained_count") > 0 &&
              separated.at("retained_count") < 32 && separated.at("new_overlap_count") > 0 &&
              separated.at("settings").at("avoid_new_overlaps") == true,
          "Actual separate-new-plants control did not filter the shared candidate task");
    input.click("foliage:place");
    settle();
    const auto separate_group = owner.selection.entity();
    std::vector<Double3> separate_origins;
    const auto separate_effective = scene.effective_document();
    for (const auto& row : separate_effective.at("entities"))
        if (row.value("parent", "") == separate_group) {
            const auto& affine = row.at("world_affine");
            const Double3 origin{affine[3].get<double>(), affine[7].get<double>(),
                                 affine[11].get<double>()};
            for (const auto& earlier : separate_origins)
                check(std::hypot(origin[0] - earlier[0], origin[1] - earlier[1],
                                 origin[2] - earlier[2]) >= 2,
                      "Actual placement control created overlapping new clearance spheres");
            separate_origins.push_back(origin);
        }
    const auto separate_scene = scene.document();
    check(separate_origins.size() == separated.at("retained_count") && scene.undo() &&
              scene.document() == clearance_before && scene.redo() &&
              scene.document() == separate_scene && scene.undo() &&
              scene.document() == clearance_before,
          "Separated native placement lost its single Scene Undo/Redo transaction");
    input.click("foliage:separate_new");
    input.type("foliage:clearance_radius", ".25");
    input.type("foliage:clearance_mask", "4294967295");
    owner.selection.select_entity(obstacle);
    input.click("foliage:ignore");
    input.click("foliage:check");
    settle();
    check(panel.collision_task().state().at("result").at("retained_count") == 32,
          "Actual ignored-object control failed canonical exclusion");
    input.click("foliage:ignore_clear");
    input.click("foliage:place");
    settle();
    const auto cleared = scene.document();
    const auto group_id = owner.selection.entity();
    unsigned cleared_count = 0;
    const auto cleared_effective = scene.effective_document();
    for (const auto& row : cleared_effective.at("entities"))
        if (row.value("parent", "") == group_id) {
            check(std::abs(row.at("world_affine")[3].get<double>()) > .75,
                  "Actual collision controls placed an overlapping candidate");
            ++cleared_count;
        }
    check(cleared_count > 0 && cleared_count < 32 && owner.problems.size() == 0 && scene.undo() &&
              scene.document() == clearance_before && scene.redo() && scene.document() == cleared,
          "Actual collision placement did not use one normal filtered history entry");
    detail::MeshScatterSettings pending;
    pending.count = 512;
    pending.radius = 30;
    pending.spacing = 0;
    panel.collision_task().start(
        project.writer_guard(), scene,
        detail::mesh_scatter_arguments(source, pending, "Cancelled check"));
    input.click("foliage:cancel");
    frame();
    check(panel.collision_task().state().at("state") == "cancelled" &&
              scene.document() == cleared && owner.problems.size() == 0,
          "Actual Cancel clearance control changed the Scene or reported an intentional "
          "cancellation as an error");
    input.type("foliage:count", "512");
    input.type("foliage:radius", "30");
    input.click("foliage:place");
    check(panel.collision_task().busy(), "Large pending placement finished before close test");
    panel.open = false;
    frame();
    check(panel.collision_task().state().at("state") == "cancelled" &&
              scene.document() == cleared && owner.problems.size() == 0,
          "Closing a pending placement created objects or reported intentional cancellation as an "
          "error");
    panel.open = true;
    locked = true;
    frame();
    check(!test::ui_targets.at("foliage:place").enabled &&
              !test::ui_targets.at("foliage:source").enabled,
          "Play/task lock did not disable foliage editing");
    locked = false;
    authoring_command(
        scene, "transform.local",
        {{"entity", surface}, {"rotation", {{"x", 0}, {"y", 0}, {"z", 0}, {"w", 1}}}});
    owner.selection.select_entity(source);
    input.click("foliage:source");
    input.click("foliage:projected");
    input.click("foliage:surface_kind");
    io.AddKeyEvent(ImGuiKey_Home, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Home, false);
    io.AddKeyEvent(ImGuiKey_Enter, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Enter, false);
    frame();
    owner.selection.select_entity(surface);
    input.click("foliage:terrain");
    input.type("foliage:height_offset", "0");
    input.type("foliage:radius", ".1");
    input.type("foliage:spacing", "0");
    input.type("foliage:count", "2");
    input.type("foliage:clearance_mask", "0");
    input.type("foliage:clearance_radius", ".4");
    input.click("foliage:separate_new");
    input.click("foliage:brush");
    input.type("foliage:dab_distance", ".2");
    check(panel.brush_enabled(), "Native collision controls disabled the selected brush mode");
    brush_surface = EntityId::parse(surface);
    brush_mesh = terrain_id;
    brush_center = {ground_origin[0] + 4, ground_origin[1], ground_origin[2] + 4};
    detail::MeshScatterSettings brush_settings;
    brush_settings.center = brush_center;
    brush_settings.count = 2;
    brush_settings.radius = .1;
    brush_settings.spacing = 0;
    brush_revision =
        prepare_terrain_foliage(
            *project.writer_guard(), scene,
            detail::mesh_scatter_arguments(source, brush_settings, "Controller"), surface)
            .at("terrain_revision")
            .get<std::string>();
    brush_scene = true;
    frame();
    const auto stroke_before = scene.document();
    auto gesture = [&] {
        io.AddMousePosEvent(brush_origin.x + 60 * scale, brush_origin.y + 30 * scale);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMousePosEvent(brush_origin.x + 140 * scale, brush_origin.y + 30 * scale);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };
    gesture();
    check(panel.collision_task().busy() && !panel.brush_enabled() &&
              scene.document() == stroke_before,
          "Native brush release failed to queue clearance without immediate publication");
    settle();
    const auto stroke = scene.document();
    // The settle helper draws one final frame; the ordinary Scene mutation may
    // already have retired the now-stale ready task. Verify saved intent/selection.
    check(stroke.at("entities").size() > stroke_before.at("entities").size() &&
              owner.selection.entity() == surface && panel.brush_enabled() &&
              owner.problems.size() == 0 && scene.undo() && scene.document() == stroke_before &&
              scene.redo() && scene.document() == stroke,
          "Native cleared brush completion lost surface selection or one-step history");
    gesture();
    check(panel.collision_task().busy() && scene.document() == stroke,
          "Repeat native brush could not queue on the same selected surface");
    settle();
    check(scene.document().at("entities").size() > stroke.at("entities").size() &&
              owner.selection.entity() == surface && scene.undo() && scene.document() == stroke,
          "Repeat native cleared brush failed its Undo or selection contract");
    input.type("foliage:count", "256");
    gesture();
    check(panel.collision_task().busy() && scene.document() == stroke,
          "Large native stroke failed to remain pending for cancellation");
    input.click("foliage:cancel");
    frame();
    check(panel.collision_task().state().at("state") == "cancelled" && scene.document() == stroke &&
              owner.problems.size() == 0,
          "Native cleared-stroke cancellation published Scene objects");
    brush_scene = false;
    Scene other(engine.world());
    frame();
    // A different Scene identity must never keep a template from the old project.
    auto replacement = other.document();
    scene.reset(replacement);
    frame();
    check(!test::ui_targets.at("foliage:place").enabled,
          "Scene switch retained the old foliage template");
    test::observe_ui = false;
}
enum class CaptureMode {
    Terrain,
    MeshSurface,
    SphereClearance,
    MeshBounds,
    XYBounds,
    MeshSurfaces
};
void capture(const std::filesystem::path& output, const std::filesystem::path& gallery = {},
             CaptureMode mode = CaptureMode::Terrain) {
    const bool surfaces = mode == CaptureMode::MeshSurfaces, xy = mode == CaptureMode::XYBounds,
               bounds = mode == CaptureMode::MeshBounds || xy || surfaces,
               clearance = bounds || mode == CaptureMode::SphereClearance,
               mesh_surface = mode == CaptureMode::MeshSurface;
    std::filesystem::create_directories(output);
    if (!SDL_Init(SDL_INIT_VIDEO))
        throw std::runtime_error(SDL_GetError());
    {
        const float height = surfaces    ? 1700
                             : xy        ? 2200
                             : clearance ? 2050
                                         : (gallery.empty() ? 1100 : (mesh_surface ? 1480 : 1420));
        test::ImGuiSoftwareCapture capture(720, unsigned(height));
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::GetIO().ConfigInputTrickleEventQueue = false;
        ui::style(1);
        EngineContext engine;
        Scene scene(engine.world());
        std::unique_ptr<SceneDocument> project;
        std::string surface, prototype;
        if (!gallery.empty()) {
            const auto root = output / "project" / AssetId::generate().str();
            std::filesystem::create_directories(root.parent_path());
            std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
            project = std::make_unique<SceneDocument>(scene);
            project->open_project(root);
            project->open_scene(surfaces       ? "Scenes/269-foliage-mesh-surfaces.scene.json"
                                : xy           ? "Scenes/268-foliage-xy-clearance.scene.json"
                                : bounds       ? "Scenes/267-foliage-mesh-bounds.scene.json"
                                : mesh_surface ? "Scenes/200-mesh-surface-foliage.scene.json"
                                : clearance    ? "Scenes/156-foliage-clearance.scene.json"
                                               : "Scenes/147-terrain-foliage.scene.json");
            const auto source = scene.snapshot();
            for (const auto& row : source.at("entities")) {
                const auto name = row.at("name").get<std::string>();
                if (name.starts_with("Blade template"))
                    prototype = row.at("id");
                if (name.starts_with(mesh_surface ? "Mesh surface - tilted" : "Layered terrain"))
                    surface = row.at("id");
            }
            check((surfaces || !surface.empty()) && !prototype.empty(),
                  "Capture Gallery actors are missing");
        }
        ui::EditorUiContext owner;
        ui::ContextScope scope(owner);
        owner.selection.select_entity(project ? prototype : cube(scene));
        ui::FoliagePlacement panel;
        panel.open = true;
        test::observe_ui = true;
        auto frame = [&] {
            test::ui_targets.clear();
            capture.new_frame();
            ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
            ImGui::SetWindowSize("Foliage placement", {720, height}, ImGuiCond_Always);
            panel.draw(scene, owner.selection, false, project.get());
            capture.render();
        };
        test::ImGuiInputDriver input(frame);
        for (unsigned i = 0; i < 4; ++i)
            frame();
        input.click("foliage:source");
        input.click("foliage:check");
        for (unsigned i = 0; i < 4; ++i)
            frame();
        capture.save(output / "foliage-placement-controls.png");
        if (project) {
            const auto before = scene.document();
            if (surfaces) {
                input.type("foliage:x", "2");
                input.type("foliage:y", "0");
                input.type("foliage:z", "0");
                input.type("foliage:count", "2");
                input.type("foliage:radius", ".01");
                input.type("foliage:spacing", "0");
                input.type("foliage:scale_min", "1");
                input.type("foliage:scale_max", "1");
                input.type("foliage:yaw_min", "0");
                input.type("foliage:yaw_max", "0");
            } else {
                owner.selection.select_entity(surface);
                frame();
                input.click("foliage:projected");
                if (mesh_surface) {
                    auto& io = ImGui::GetIO();
                    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
                    io.ConfigInputTrickleEventQueue = false;
                    input.choose("foliage:surface_kind", 1);
                }
                input.click("foliage:terrain");
                input.type("foliage:x", mesh_surface ? "-7" : "0");
                input.type("foliage:y", "0");
                input.type("foliage:z", "0");
                input.type("foliage:count", "128");
                if (mesh_surface)
                    input.type("foliage:radius", "4.6");
                input.click("foliage:align_slope");
                input.type("foliage:seed", "91");
                input.type("foliage:spacing", "0.55");
                input.type("foliage:scale_min", "0.65");
                input.type("foliage:scale_max", "1.15");
            }
            if (clearance) {
                input.click("foliage:collision");
                if (!surfaces)
                    input.click("foliage:separate_new");
                if (bounds) {
                    input.choose("foliage:clearance_profile", surfaces ? 4 : xy ? 3 : 1);
                    if (!surfaces)
                        input.type("foliage:clearance_padding", ".05");
                    input.type("foliage:clearance_y", "0");
                }
            }
            input.click("foliage:check");
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            while (panel.collision_task().busy()) {
                check(std::chrono::steady_clock::now() < deadline,
                      "Clearance controls capture stalled");
                frame();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            for (unsigned i = 0; i < 4; ++i)
                frame();
            check(scene.document() == before && owner.problems.size() == 0,
                  "Terrain capture changed authored intent or failed preparation");
            if (!surfaces)
                for (const char* key :
                     {"foliage:projected", "foliage:terrain", "foliage:height_offset",
                      "foliage:slope_min", "foliage:slope_max", "foliage:check",
                      "foliage:align_slope", "foliage:place"}) {
                    const auto& box = test::ui_targets.at(key);
                    check(box.enabled && box.minimum.y >= box.clip_minimum.y &&
                              box.maximum.y <= box.clip_maximum.y,
                          "Terrain capture clipped a required control");
                }
            if (clearance) {
                check(panel.collision_task().state().at("state") == "ready",
                      "Clearance capture preparation failed");
                for (const char* key :
                     {"foliage:collision", "foliage:separate_new", "foliage:clearance_profile",
                      bounds ? "foliage:clearance_padding" : "foliage:clearance_radius",
                      "foliage:clearance_x", "foliage:clearance_y", "foliage:clearance_z",
                      "foliage:clearance_mask", "foliage:sensors", "foliage:ignore",
                      "foliage:ignore_clear"}) {
                    const auto& box = test::ui_targets.at(key);
                    const bool disabled =
                        (xy && std::string_view(key) == "foliage:clearance_z") ||
                        (surfaces && std::string_view(key) == "foliage:clearance_padding");
                    check(box.enabled == !disabled && box.minimum.y >= box.clip_minimum.y &&
                              box.maximum.y <= box.clip_maximum.y,
                          "Clearance capture clipped a required control");
                }
            }
            capture.save(output / (surfaces       ? "foliage-mesh-surfaces-controls.png"
                                   : xy           ? "foliage-xy-clearance-controls.png"
                                   : bounds       ? "foliage-mesh-bounds-controls.png"
                                   : mesh_surface ? "mesh-foliage-controls.png"
                                   : clearance    ? "foliage-clearance-controls.png"
                                                  : "terrain-foliage-controls.png"));
        }
        test::observe_ui = false;
    }
    SDL_Quit();
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string_view(argv[1]) == "--capture-gallery")
            capture(argv[3], argv[2]);
        else if (argc == 4 && std::string_view(argv[1]) == "--capture-clearance")
            capture(argv[3], argv[2], CaptureMode::SphereClearance);
        else if (argc == 4 && std::string_view(argv[1]) == "--capture-bounds")
            capture(argv[3], argv[2], CaptureMode::MeshBounds);
        else if (argc == 4 && std::string_view(argv[1]) == "--capture-xy-bounds")
            capture(argv[3], argv[2], CaptureMode::XYBounds);
        else if (argc == 4 && std::string_view(argv[1]) == "--capture-mesh-surfaces")
            capture(argv[3], argv[2], CaptureMode::MeshSurfaces);
        else if (argc == 4 && std::string_view(argv[1]) == "--capture-mesh-gallery")
            capture(argv[3], argv[2], CaptureMode::MeshSurface);
        else if (argc == 2)
            capture(argv[1]);
        else if (argc == 3 && std::string_view(argv[1]) == "--xy-test-root")
            xy_controls(1.f, false, argv[2]);
        else if (argc == 3 && std::string_view(argv[1]) == "--test-root")
            for (float scale : {1.f, 1.5f, 2.f})
                for (bool compact : {false, true}) {
                    exercise(scale, compact, argv[2]);
                    xy_controls(scale, compact, argv[2]);
                }
        else
            throw std::runtime_error("Provide a capture directory or --test-root scratch");
        std::cout << "Actual foliage control checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
