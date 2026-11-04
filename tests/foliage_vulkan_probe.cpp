#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "editor/authoring.hpp"
#include "editor/viewport.hpp"
#include "foliage_collision.hpp"
#include "gallery_game_capture.hpp"
#include "mesh_scatter_authoring.hpp"
#include "mesh_wind.hpp"
#include "runtime_package.hpp"
#include "terrain_foliage_authoring.hpp"
#include <cmath>
#include <iostream>
#include <numbers>
using namespace forge;
using namespace Diligent;
namespace {
Json prepare_slope_patch(const std::filesystem::path& root, const std::filesystem::path& path,
                         const Json& source, bool collisions = false) {
    EngineContext engine;
    Scene scene(engine.world());
    scene.reset(source);
    auto lease = std::make_shared<ProjectLease>(root);
    std::string prototype, terrain;
    for (const auto& row : source.at("entities")) {
        const auto name = row.at("name").get<std::string>();
        if (name.starts_with("Blade template"))
            prototype = row.at("id");
        if (name.starts_with("Layered terrain"))
            terrain = row.at("id");
    }
    test_gallery_capture::check(!prototype.empty() && !terrain.empty(),
                                "Slope placement actors unavailable");
    detail::MeshScatterSettings settings;
    settings.center = {0, 0, 0};
    settings.count = 48;
    settings.radius = 5;
    settings.spacing = .35;
    settings.seed = 8128;
    const auto arguments =
        detail::mesh_scatter_arguments(prototype, settings, "Slope-aligned patch");
    const auto observed = prepare_terrain_foliage(*lease, scene, arguments, terrain, 0, {}, true);
    double minimum = 90, maximum = 0;
    for (const auto& value : observed.at("arguments").at("world_normals")) {
        const Double3 n{value.at("x"), value.at("y"), value.at("z")};
        const auto slope = std::atan2(std::hypot(n[0], n[2]), n[1]) * 180 / std::numbers::pi;
        minimum = std::min(minimum, slope);
        maximum = std::max(maximum, slope);
    }
    test_gallery_capture::check(maximum - minimum > 1e-6, "Gallery surface has no varied slopes");
    const auto cutoff = (minimum + maximum) * .5;
    auto plan = prepare_terrain_foliage(*lease, scene, arguments, terrain, 0, {0, cutoff}, true);
    auto retained = plan.at("retained_count").get<unsigned>();
    test_gallery_capture::check(retained > 0 && retained < settings.count,
                                "Native slope selection did not produce a proper subset");
    const auto slope_count = retained;
    if (collisions) {
        const auto obstacle =
            authoring_command(scene, "entity.create", {{"name", "Clearance volume"}})
                .at("selected");
        for (auto component : {"forge.physics_body", "forge.box_collider"})
            authoring_command(scene, "component.add",
                              {{"entity", obstacle}, {"component", component}});
        for (auto [field, value] : {std::pair{"x", 2.}, {"y", 20.}, {"z", 12.}})
            authoring_command(scene, "property.set",
                              {{"entity", obstacle},
                               {"component", "forge.box_collider"},
                               {"field", field},
                               {"value", value}});
        authoring_command(scene, "property.set",
                          {{"entity", obstacle},
                           {"component", "forge.physics_body"},
                           {"field", "motion"},
                           {"value", 0}});
        authoring_command(scene, "transform.position",
                          {{"entity", obstacle}, {"value", {{"x", 0}, {"y", 4}, {"z", 0}}}});
        FoliageCollisionPreparation task;
        FoliageCollisionSettings config;
        config.surface = EntityId::parse(terrain);
        const auto before = scene.document();
        task.start(lease, scene, plan.at("arguments"), config);
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (task.busy()) {
            task.poll(scene, root);
            test_gallery_capture::check(std::chrono::steady_clock::now() < end,
                                        "Native foliage clearance stalled");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto ready = task.read(scene, root);
        test_gallery_capture::check(ready.at("state") == "ready", ready.dump().c_str());
        plan = ready.at("result");
        retained = plan.at("retained_count");
        test_gallery_capture::check(
            retained > 0 && retained < slope_count && plan.at("physics_tick") == 0 &&
                scene.document() == before,
            "Native clearance failed to compose a read-only proper slope subset");
    }
    const auto result =
        apply_authoring(
            scene,
            Json::array({{{"operation", "mesh.scatter"}, {"arguments", plan.at("arguments")}}}),
            plan.at("scene_revision"))
            .at("results")
            .at(0);
    test_gallery_capture::check(result.at("created").size() == retained &&
                                    result.at("aligned_to_slope") == true,
                                "Native slope preparation/Scene command disagree");
    scene.save(root / path);
    const std::array roots{scene.asset_id()};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
    return {{"candidate_count", settings.count},
            {"retained_count", retained},
            {"slope_retained_count", slope_count},
            {"collision_clearance", collisions},
            {"maximum_slope", cutoff},
            {"aligned_to_slope", true}};
}
Json scene_view(DiligentPresentation& presentation, IDeviceContext* context,
                const std::filesystem::path& root, AssetId id, bool terrain,
                unsigned added_instances = 0, unsigned saved_clearance_instances = 0) {
    EngineContext engine(WorldRole::Runtime, false);
    Scene scene(engine.world());
    scene.restore_snapshot(load_game_scene(root, {id}));
    const auto source = scene.effective_document();
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root);
    host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
    forge::Viewport viewport(presentation);
    viewport.resources(host);
    viewport.preview_lighting(true);
    EditorCamera camera;
    camera.target = {-.5f, 1, 0};
    camera.pitch = -.35f;
    camera.distance = 20;
    if (terrain) {
        camera.target = {0, 2, 0};
        camera.distance = 26;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    unsigned settled = 0;
    do {
        presentation.begin_frame();
        auto* image =
            viewport.render(context, source, 640, 360, camera, scene.revision(), false, {false});
        test_gallery_capture::check(image, "Foliage Scene View has no output");
        presentation.end_frame(context);
        host->submit();
        (void)test_sprite_draw::read_pixels(presentation.device(), context, image->GetTexture());
        context->FinishFrame();
        presentation.device()->ReleaseStaleResources();
        test_gallery_capture::check(std::chrono::steady_clock::now() < deadline,
                                    "Foliage Scene View resources stalled");
        settled = viewport.meshes()->pending() ? 0 : settled + 1;
        if (!settled)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (settled < 2);
    const auto* meshes = viewport.meshes();
    test_gallery_capture::check(meshes->diagnostics().empty() && meshes->omitted_diagnostics() == 0,
                                "Foliage Scene View rejected its Mesh/material values");
    const auto stats = meshes->draw_stats();
    const auto counts = Json{{"instances", stats.instances},
                             {"batched_calls", stats.batched_calls},
                             {"calls", stats.calls}};
    const auto failure = "Foliage static Mesh draw counts: " + counts.dump();
    // Each of the terrain's two tile parts contributes a draw instance.
    test_gallery_capture::check(
        stats.instances ==
                (saved_clearance_instances ? saved_clearance_instances : (terrain ? 131u : 195u)) +
                    added_instances &&
            stats.batched_calls > 0 && stats.calls < stats.instances,
        failure.c_str());
    std::string prototype;
    AffineTransform prototype_transform;
    for (const auto& e : source.at("entities"))
        if (e.at("name").get<std::string>().starts_with("Blade template")) {
            prototype = e.at("id");
            prototype_transform.m = e.at("world_affine").get<std::array<double, 12>>();
        }
    auto center = prototype_transform.point({.125, .8, 0});
    for (const auto& mesh : extract_render_scene(source).meshes)
        if (mesh.entity.str() == prototype && mesh.renderer.wind.enabled) {
            ResourcePool<MeshAsset> pool;
            const auto ticket =
                asset_detail::request_model_mesh(pool, root, host->catalog(), mesh.renderer.mesh);
            test_gallery_capture::check(pool.wait(ticket, std::chrono::seconds(20)),
                                        "Wind picking oracle Mesh admission stalled");
            const auto selected = pool.acquire(ticket);
            test_gallery_capture::check(bool(selected), "Wind picking oracle Mesh expired");
            const auto& part = selected->mesh.lods.front().parts.front();
            const auto& vertices = std::get<std::vector<float>>(part.find("POSITION")->values);
            const auto wind = sample_mesh_wind(mesh.renderer.wind, mesh.world, .5);
            // The shader deforms vertices, then rasterizes linear triangles.
            // A point on the continuous bend curve is not generally on that
            // polygon for a coarse mesh. Use a deformed triangle's centroid.
            center = {};
            for (unsigned corner = 0; corner < 3; ++corner) {
                const auto index = part.indices.empty() ? corner : part.indices.at(corner);
                const auto point =
                    wind_point(wind, mesh.world.point({vertices[3 * index], vertices[3 * index + 1],
                                                       vertices[3 * index + 2]}));
                for (unsigned axis = 0; axis < 3; ++axis)
                    center[axis] += point[axis] / 3;
            }
        }
    const auto point =
        project_point(camera, {float(center[0]), float(center[1]), float(center[2])}, 640, 360);
    test_gallery_capture::check(
        point && viewport.pick(source, camera, 640, 360, (*point)[0], (*point)[1]) == prototype,
        "Independent template picking failed after batched foliage rendering");
    return {{"instances", stats.instances},
            {"draw_calls", stats.calls},
            {"batched_calls", stats.batched_calls},
            {"independent_template_pick", true}};
}
Json wind_time_policy(DiligentPresentation& presentation, IDeviceContext* context,
                      const std::filesystem::path& root, AssetId id,
                      const std::filesystem::path& output) {
    WorldContext engine;
    Scene source(engine);
    source.restore_snapshot(load_game_scene(root, {id}));
    const auto before = source.document();
    const auto base = extract_render_scene(source.effective_document());
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root);
    host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
    FrameRenderer frame(presentation);
    frame.resources(host);
    const auto operation = AssetId::generate();
    auto render = [&](std::optional<double> borrowed, std::optional<double> explicit_time,
                      double wall, const RenderScene* replacement = nullptr,
                      bool expected_failure = false) {
        auto scene = replacement ? *replacement : base;
        scene.simulation_seconds = borrowed;
        const TemporalFrameSample sample{operation, explicit_time.value_or(0)};
        test_gallery_capture::Pixels result;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        do {
            presentation.begin_frame();
            auto* image =
                frame.game(context, scene, 640, 360, wall, explicit_time ? &sample : nullptr);
            test_gallery_capture::check(image, "Wind time policy has no Game display");
            presentation.end_frame(context);
            host->submit();
            result =
                test_sprite_draw::read_pixels(presentation.device(), context, image->GetTexture());
            context->FinishFrame();
            presentation.device()->ReleaseStaleResources();
            test_gallery_capture::check(std::chrono::steady_clock::now() < deadline,
                                        "Wind time resources stalled");
        } while (frame.pending());
        if (expected_failure)
            test_gallery_capture::check(
                frame.omitted_diagnostics() == 0 &&
                    std::any_of(frame.diagnostics().begin(), frame.diagnostics().end(),
                                [](const auto& value) {
                                    return value.text.find("wind amplitude") != std::string::npos;
                                }),
                "Invalid wind failed without its actionable geometry diagnostic");
        else
            test_gallery_capture::check(
                frame.diagnostics().empty() && frame.omitted_diagnostics() == 0,
                "Wind time policy has incomplete geometry/shadow admission");
        return result;
    };
    const auto frozen = render({}, {}, 0), held = render({}, {}, 10000), first = render(.25, {}, 1),
               second = render(.75, {}, 2), explicit_first = render(.75, .25, 3),
               explicit_edit = render(.75, .5, 4);
    unsigned changed = 0;
    for (std::size_t i = 0; i < first.size(); ++i)
        changed += first[i] != second[i];
    test_gallery_capture::check(
        frozen == held && first == explicit_first && frozen == explicit_edit && changed > 100 &&
            source.document() == before,
        "Wind geometry did not respect frozen/borrowed/explicit time ownership");
    auto invalid = base;
    const auto moving = std::find_if(invalid.meshes.begin(), invalid.meshes.end(),
                                     [](const auto& mesh) { return mesh.renderer.wind.enabled; });
    test_gallery_capture::check(moving != invalid.meshes.end(),
                                "Wind lifecycle fixture has no active mesh");
    moving->renderer.wind.amplitude = 101;
    const auto retained = render(.5, {}, 5, &invalid, true), repaired = render(.5, {}, 6);
    test_gallery_capture::check(
        retained == frozen && repaired == frozen,
        "Invalid/repaired wind lost complete last-good color/shadow geometry");
    auto disabled = base;
    for (auto& mesh : disabled.meshes)
        mesh.renderer.wind.enabled = false;
    const auto off = render(.5, {}, 7, &disabled), on = render(.5, {}, 8);
    frame.resources(host);
    const auto fresh_off = render(.5, {}, 9, &disabled);
    unsigned disabled_changed = 0;
    for (std::size_t i = 0; i < off.size(); ++i)
        disabled_changed += off[i] != frozen[i];
    test_gallery_capture::check(
        disabled_changed > 100 && off == fresh_off && on == frozen && source.document() == before,
        "Wind mode/owner replacement retained stale geometry or changed source");
    auto temporal = base;
    temporal.settings.temporal_anti_aliasing.enabled = true;
    (void)render(.25, {}, 10, &temporal);
    (void)render(.75, {}, 10 + 1. / 60, &temporal);
    for (unsigned sample = 2; sample < 16 && !frame.temporal_ready(); ++sample)
        (void)render(.75, {}, 10 + double(sample) / 60, &temporal);
    test_gallery_capture::check(frame.temporal_ready(),
                                "Wind Game temporal history did not settle within sixteen frames");
    auto temporal_off = temporal;
    for (auto& mesh : temporal_off.meshes)
        mesh.renderer.wind.enabled = false;
    (void)render(.75, {}, 10.3, &temporal_off);
    (void)render(.75, {}, 10.3 + 1. / 60, &temporal);
    test_gallery_capture::check(frame.temporal_ready() && source.document() == before,
                                "Temporal Wind mode changes lost admission or changed source");
    std::filesystem::create_directories(output);
    test_gallery_capture::ppm(output / "wind-025.ppm", first, 640, 360);
    test_gallery_capture::ppm(output / "wind-075.ppm", second, 640, 360);
    return {{"edit_wall_time_independent", true},
            {"borrowed_time_changed_pixels", changed},
            {"explicit_overrides_borrowed", true},
            {"explicit_matches_frozen_edit", true},
            {"invalid_retains_complete_geometry", true},
            {"repair_restores_geometry", true},
            {"mode_toggle_changed_pixels", disabled_changed},
            {"fresh_owner_matches_disabled", true},
            {"temporal_game_geometry_and_mode_changes", true},
            {"source_unchanged", true}};
}
} // namespace
int main(int argc, char** argv) {
    try {
        test_gallery_capture::check(
            argc == 3 || argc == 4 ||
                (argc == 5 && (std::string_view(argv[4]) == "--slope-placement" ||
                               std::string_view(argv[4]) == "--collision-placement")),
            "Need Gallery/scratch, optional terrain Scene and --slope-placement");
        const bool terrain = argc >= 4, slopes = argc == 5;
        const bool wind =
            terrain && std::string_view(argv[3]) == "Scenes/157-foliage-wind.scene.json";
        const bool collisions = slopes && std::string_view(argv[4]) == "--collision-placement";
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto root = scratch / "Gallery";
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        EngineVkCreateInfo create;
        create.Features.GeometryShaders = DEVICE_FEATURE_STATE_ENABLED;
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        test_gallery_capture::check(device && context, "Vulkan unavailable");
        DiligentPresentation presentation(device);
        Json source;
        std::ifstream(root / (terrain ? argv[3] : "Scenes/144-foliage-placement.scene.json")) >>
            source;
        const auto id = source.at("asset_id").get<AssetId>();
        Json slope_proof;
        if (slopes)
            slope_proof = prepare_slope_patch(root, argv[3], source, collisions);
        unsigned saved_clearance_instances = 0;
        if (wind)
            saved_clearance_instances = 91;
        if (terrain && std::string_view(argv[3]) == "Scenes/156-foliage-clearance.scene.json") {
            unsigned plants = 0;
            for (const auto& row : source.at("entities"))
                plants += row.at("name").get<std::string>().starts_with("Cleared meadow ");
            test_gallery_capture::check(plants > 0 && plants < 96,
                                        "Saved clearance Gallery plant count invalid");
            saved_clearance_instances = plants + 4;
        }
        const auto stats = scene_view(
            presentation, context, root, id, terrain,
            // The clearance volume also has the ordinary primitive draw.
            slopes ? slope_proof.at("retained_count").get<unsigned>() + (collisions ? 1u : 0u) : 0,
            saved_clearance_instances);
        Json time_proof;
        if (wind)
            time_proof = wind_time_policy(presentation, context, root, id, scratch / "time-policy");
        const auto authored =
            test_gallery_capture::render(presentation, context, root, id, scratch / "authored", 6,
                                         !terrain || wind, {30, 60, 90}, wind);
        unsigned green = 0, amber = 0;
        for (const auto& p : authored[2]) {
            green += double(p[1]) > double(p[0]) * 1.3 && double(p[1]) > double(p[2]) * 1.1;
            amber += double(p[0]) > double(p[1]) * 1.1 && double(p[1]) > double(p[2]) * 1.4;
        }
        test_gallery_capture::check(green > 200 && (terrain || amber > 100),
                                    "Gallery did not visibly render the expected foliage");
        const std::array roots{id};
        package_runtime_content(root, scratch / "Content", roots, {"windows", "d3d12"});
        std::filesystem::rename(scratch / "Content", scratch / "Relocated game");
        std::filesystem::rename(root, scratch / "SourcesOffline");
        const auto exported = test_gallery_capture::render(
            presentation, context, scratch / "Relocated game", id, scratch / "relocated", 6,
            !terrain || wind, {30, 60, 90}, wind);
        test_gallery_capture::check(authored == exported,
                                    "Source-hidden foliage displays changed after relocation");
        Json receipt = {{"scene", id},
                        {"exact_display_pairs", 4},
                        {"scene_view", stats},
                        {"green_pixels", green},
                        {"amber_pixels", amber},
                        {"scope", "Actual Scene/Game on software Vulkan; no Windows/physical "
                                  "GPU/graphical EXE acceptance"}};
        if (slopes)
            receipt["slope_placement"] = std::move(slope_proof);
        if (wind)
            receipt["wind_time_policy"] = std::move(time_proof);
        std::ofstream(scratch / "receipt.json") << receipt.dump(2) << '\n';
        context->WaitForIdle();
        std::cout << receipt.dump() << "\nFoliage native evidence: " << scratch << '\n';
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
