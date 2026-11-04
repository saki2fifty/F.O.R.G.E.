#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "editor/authoring.hpp"
#include "editor/viewport.hpp"
#include "foliage_collision.hpp"
#include "gallery_game_capture.hpp"
#include "mesh_scatter_authoring.hpp"
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
    const auto center = prototype_transform.point({.125, .8, 0});
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
} // namespace
int main(int argc, char** argv) {
    try {
        test_gallery_capture::check(
            argc == 3 || argc == 4 ||
                (argc == 5 && (std::string_view(argv[4]) == "--slope-placement" ||
                               std::string_view(argv[4]) == "--collision-placement")),
            "Need Gallery/scratch, optional terrain Scene and --slope-placement");
        const bool terrain = argc >= 4, slopes = argc == 5;
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
        const auto authored = test_gallery_capture::render(presentation, context, root, id,
                                                           scratch / "authored", 6, !terrain);
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
        const auto exported =
            test_gallery_capture::render(presentation, context, scratch / "Relocated game", id,
                                         scratch / "relocated", 6, !terrain);
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
        std::ofstream(scratch / "receipt.json") << receipt.dump(2) << '\n';
        context->WaitForIdle();
        std::cout << receipt.dump() << "\nFoliage native evidence: " << scratch << '\n';
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
