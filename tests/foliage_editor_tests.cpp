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
    test::observe_ui = true;
    auto frame = [&] {
        test::ui_targets.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
        ImGui::SetWindowSize("Foliage placement", {(compact ? 400.f : 650.f) * scale, 720 * scale},
                             ImGuiCond_Always);
        panel.draw(scene, owner.selection, locked, &project);
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
void capture(const std::filesystem::path& output, const std::filesystem::path& gallery = {},
             bool clearance = false, bool mesh_surface = false) {
    std::filesystem::create_directories(output);
    if (!SDL_Init(SDL_INIT_VIDEO))
        throw std::runtime_error(SDL_GetError());
    {
        const float height =
            clearance ? 1930 : (gallery.empty() ? 1100 : (mesh_surface ? 1480 : 1420));
        test::ImGuiSoftwareCapture capture(720, unsigned(height));
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
            project->open_scene(mesh_surface ? "Scenes/200-mesh-surface-foliage.scene.json"
                                : clearance  ? "Scenes/156-foliage-clearance.scene.json"
                                             : "Scenes/147-terrain-foliage.scene.json");
            const auto source = scene.snapshot();
            for (const auto& row : source.at("entities")) {
                const auto name = row.at("name").get<std::string>();
                if (name.starts_with("Blade template"))
                    prototype = row.at("id");
                if (name.starts_with(mesh_surface ? "Mesh surface - tilted" : "Layered terrain"))
                    surface = row.at("id");
            }
            check(!surface.empty() && !prototype.empty(), "Capture Gallery actors are missing");
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
            owner.selection.select_entity(surface);
            frame();
            input.click("foliage:projected");
            if (mesh_surface) {
                auto& io = ImGui::GetIO();
                io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
                io.ConfigInputTrickleEventQueue = false;
                input.click("foliage:surface_kind");
                for (const auto key : {ImGuiKey_Home, ImGuiKey_DownArrow, ImGuiKey_Enter}) {
                    io.AddKeyEvent(key, true);
                    frame();
                    io.AddKeyEvent(key, false);
                    frame();
                }
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
            if (clearance)
                input.click("foliage:collision");
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
            for (const char* key : {"foliage:projected", "foliage:terrain", "foliage:height_offset",
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
                     {"foliage:collision", "foliage:clearance_radius", "foliage:clearance_x",
                      "foliage:clearance_y", "foliage:clearance_z", "foliage:clearance_mask",
                      "foliage:sensors", "foliage:ignore", "foliage:ignore_clear"}) {
                    const auto& box = test::ui_targets.at(key);
                    check(box.enabled && box.minimum.y >= box.clip_minimum.y &&
                              box.maximum.y <= box.clip_maximum.y,
                          "Clearance capture clipped a required control");
                }
            }
            capture.save(output / (mesh_surface ? "mesh-foliage-controls.png"
                                   : clearance  ? "foliage-clearance-controls.png"
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
            capture(argv[3], argv[2], true);
        else if (argc == 4 && std::string_view(argv[1]) == "--capture-mesh-gallery")
            capture(argv[3], argv[2], false, true);
        else if (argc == 2)
            capture(argv[1]);
        else if (argc == 3 && std::string_view(argv[1]) == "--test-root")
            for (float scale : {1.f, 1.5f, 2.f})
                for (bool compact : {false, true})
                    exercise(scale, compact, argv[2]);
        else
            throw std::runtime_error("Provide a capture directory or --test-root scratch");
        std::cout << "Actual foliage control checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
