#include "foliage_collision.hpp"
#include "mesh_scatter_authoring.hpp"
#include "render_bounds.hpp"
#include "runtime_package.hpp"
#include "terrain_foliage_authoring.hpp"
#include <cmath>
#include <forge/game_content.hpp>
#include <forge/mesh_resource.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime_world.hpp>
#include <forge/scene_identity.hpp>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
using namespace forge;
namespace {
constexpr auto path = "Scenes/156-foliage-clearance.scene.json";
constexpr auto separated_path = "Scenes/256-foliage-candidate-clearance.scene.json";
constexpr auto stroke_path = "Scenes/257-foliage-brush-clearance.scene.json";
constexpr auto bounds_path = "Scenes/267-foliage-mesh-bounds.scene.json";
constexpr double separated_radius = .4;
enum class Gallery { Sphere, Separated, Stroke, MeshBounds };
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
Json read(const std::filesystem::path& file) {
    Json result;
    std::ifstream(file) >> result;
    return result;
}
void author(const std::filesystem::path& root, Gallery mode = Gallery::Sphere) {
    const bool separate = mode != Gallery::Sphere, stroke = mode == Gallery::Stroke,
               mesh_bounds = mode == Gallery::MeshBounds;
    const auto destination = mesh_bounds ? bounds_path
                             : stroke    ? stroke_path
                             : separate  ? separated_path
                                         : path;
    check(!std::filesystem::exists(root / destination), "Clearance Gallery already exists");
    auto lease = std::make_shared<ProjectLease>(root);
    EngineContext engine;
    Scene scene(engine.world());
    auto source = duplicate_scene_asset(read(root / "Scenes/147-terrain-foliage.scene.json"),
                                        AssetId::generate(), scene.schema());
    std::erase_if(source["entities"].get_ref<Json::array_t&>(), [](const Json& row) {
        const auto name = row.at("name").get<std::string>();
        return !row.at("components").contains("forge.camera") &&
               !row.at("components").contains("forge.light") &&
               !name.starts_with("Blade template") && !name.starts_with("Layered terrain");
    });
    scene.reset(source);
    std::string prototype, surface;
    for (const auto& row : source.at("entities")) {
        const auto name = row.at("name").get<std::string>();
        if (name.starts_with("Blade template"))
            prototype = row.at("id");
        if (name.starts_with("Layered terrain"))
            surface = row.at("id");
    }
    check(!prototype.empty() && !surface.empty(), "Clearance Gallery actors missing");
    if (mesh_bounds)
        authoring_command(scene, "transform.scale",
                          {{"entity", prototype}, {"value", {{"x", -2}, {"y", 1.4}, {"z", 2}}}});
    const auto obstacle = authoring_command(scene, "entity.create",
                                            {{"recipe", "primitive.0"},
                                             {"name", "Clearance obstacle - ignore to compare"}})
                              .at("selected");
    authoring_command(scene, "transform.position",
                      {{"entity", obstacle}, {"value", {{"x", 0}, {"y", 4}, {"z", 0}}}});
    authoring_command(scene, "transform.scale",
                      {{"entity", obstacle}, {"value", {{"x", 2}, {"y", 8}, {"z", 12}}}});
    for (auto component : {"forge.physics_body", "forge.box_collider"})
        authoring_command(scene, "component.add", {{"entity", obstacle}, {"component", component}});
    authoring_command(scene, "property.set",
                      {{"entity", obstacle},
                       {"component", "forge.physics_body"},
                       {"field", "motion"},
                       {"value", 0}});
    detail::MeshScatterSettings settings;
    settings.center = {0, 0, 0};
    settings.radius = 5;
    settings.spacing = .5;
    settings.count = 96;
    settings.seed = 91;
    settings.scale_min = .65;
    settings.scale_max = 1.15;
    for (unsigned patch = 0; patch < (separate ? 2u : 1u); ++patch) {
        if (separate) {
            settings.center[0] = patch ? 3 : -3;
            settings.radius = 2.75;
            settings.spacing = .1;
        }
        const auto* name = mesh_bounds ? (patch ? "Mesh bounds meadow" : "Root sphere meadow")
                           : stroke
                               ? (patch ? "Brush plant separation" : "Brush collider clearance")
                           : !separate ? "Cleared meadow"
                           : patch     ? "Separated meadow"
                                       : "Unseparated meadow";
        Json prepared;
        if (stroke) {
            settings.radius = .9;
            settings.spacing = .05;
            settings.count = 32;
            settings.center[2] = patch ? 3 : -3;
            Json arguments{{"entity", prototype}, {"name", name}, {"dabs", Json::array()}};
            for (const double x : {-3., 0., 3.}) {
                settings.center[0] = x;
                auto dab = detail::mesh_scatter_arguments(prototype, settings, name);
                dab.erase("entity");
                dab.erase("name");
                arguments["dabs"].push_back(std::move(dab));
            }
            prepared =
                prepare_foliage_stroke(*lease, scene, arguments, surface, false, 0, {}, true);
        } else
            prepared = prepare_terrain_foliage(
                *lease, scene, detail::mesh_scatter_arguments(prototype, settings, name), surface,
                0, {}, true);
        FoliageCollisionPreparation task;
        FoliageCollisionSettings config;
        config.surface = EntityId::parse(surface);
        config.avoid_new_overlaps = separate && patch;
        if (separate)
            config.radius = separated_radius;
        if (mesh_bounds) {
            config.avoid_new_overlaps = true;
            config.radius = .15;
            if (patch) {
                config.profile = FoliageClearanceProfile::MeshBounds;
                config.padding = .05;
                config.offset = {};
            }
        }
        task.start(lease, scene, prepared.at("arguments"), config);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (task.busy()) {
            task.poll(scene, root);
            check(std::chrono::steady_clock::now() < deadline, "Gallery clearance timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto ready = task.read(scene, root);
        check(ready.at("state") == "ready", ready.dump().c_str());
        const auto count = ready.at("result").at("retained_count").get<unsigned>();
        check(count > 0 && count < settings.count * (stroke ? 3 : 1),
              "Gallery clearance did not reject a proper subset");
        if (stroke)
            check(ready.at("result").at("retained_dab_indices") == Json::array({0, 2}),
                  "Brush Gallery did not omit its fully blocked middle dab");
        if (config.avoid_new_overlaps)
            check(ready.at("result").at("new_overlap_count") > 0,
                  "Separated Gallery did not exercise new-plant overlap filtering");
        apply_authoring(scene,
                        Json::array({{{"operation", ready.at("result").at("authoring_operation")},
                                      {"arguments", ready.at("result").at("arguments")}}}),
                        ready.at("result").at("scene_revision"));
        std::cout << "Authored " << name << ": " << count << "/96 retained\n";
    }
    scene.save(root / destination);
    auto catalog = AssetCatalog::open_project(root);
    catalog.add_scene(destination);
    catalog.save(AssetCatalog::project_index(root));
    const std::array roots{scene.asset_id()};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
}
void exercise(const std::filesystem::path& root, AssetId id, Gallery mode = Gallery::Sphere) {
    const bool separate = mode != Gallery::Sphere, stroke = mode == Gallery::Stroke,
               mesh_bounds = mode == Gallery::MeshBounds;
    RuntimeWorld world({}, {}, std::nullopt, root, false);
    world.scene.restore_snapshot(load_game_scene(root, {id}));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!world.prepare_physics_assets()) {
        check(std::chrono::steady_clock::now() < deadline, "Gallery physics resources stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    world.physics()->synchronize(0);
    const auto source = world.scene.effective_document();
    unsigned plants = 0, obstacles = 0, unseparated = 0;
    std::vector<LocalTranslation> separated;
    std::vector<PhysicsOverlap> separated_boxes;
    ResourceLease<MeshAsset> template_mesh;
    if (mesh_bounds) {
        AssetId asset;
        for (const auto& row : source.at("entities"))
            if (row.at("name").get<std::string>().starts_with("Blade template"))
                asset = row.at("components").at("forge.mesh_renderer").at("mesh").get<AssetId>();
        check(bool(asset), "Mesh bounds Gallery lost its template");
        const auto resources = world.engine.services().resources();
        const auto token = resources->request(RuntimeResourceKind::Mesh, asset);
        while (!(template_mesh = resources->mesh(token))) {
            resources->synchronize();
            check(std::chrono::steady_clock::now() < deadline, "Gallery template Mesh stalled");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(template_mesh->mesh.lods.size() == 1 && template_mesh->mesh.lods[0].parts.size() == 1,
              "Gallery static blade geometry contract changed");
    }
    for (const auto& row : source.at("entities")) {
        const auto name = row.at("name").get<std::string>();
        if (name.starts_with("Clearance obstacle"))
            ++obstacles;
        const bool separated_plant =
            separate && name.starts_with(mesh_bounds ? "Mesh bounds meadow "
                                         : stroke    ? "Brush plant separation "
                                                     : "Separated meadow ");
        const bool unseparated_plant =
            separate && name.starts_with(mesh_bounds ? "Root sphere meadow "
                                         : stroke    ? "Brush collider clearance "
                                                     : "Unseparated meadow ");
        if (!separated_plant && !unseparated_plant && !name.starts_with("Cleared meadow "))
            continue;
        if (!row.at("components").contains("forge.mesh_renderer"))
            continue;
        const auto& m = row.at("world_affine");
        PhysicsOverlap query;
        query.origin = {m[3], m[7].get<double>() + .5, m[11]};
        query.dimensions = {mesh_bounds ? .15 : separate ? separated_radius : .25, 0, 0};
        if (mesh_bounds && separated_plant) {
            AffineTransform frame;
            for (unsigned i = 0; i < 12; ++i)
                frame.m[i] = m[i].get<double>();
            const auto box = transform_bounds(template_mesh->mesh.lods[0].parts[0].bounds, frame);
            query.shape = PhysicsOverlap::Shape::Box;
            query.origin = {(box.minimum[0] + box.maximum[0]) * .5,
                            (box.minimum[1] + box.maximum[1]) * .5,
                            (box.minimum[2] + box.maximum[2]) * .5};
            for (unsigned axis = 0; axis < 3; ++axis)
                query.dimensions[axis] = std::max(.001, box.maximum[axis] - box.minimum[axis] + .1);
        }
        check(!world.physics()->overlap_any(query, {UINT32_MAX, false}),
              "Saved Gallery plant overlaps the actual native collider");
        ++plants;
        unseparated += unseparated_plant;
        if (separated_plant) {
            if (mesh_bounds) {
                for (const auto& earlier : separated_boxes)
                    check(std::abs(query.origin.x - earlier.origin.x) >=
                                  .5 * (query.dimensions[0] + earlier.dimensions[0]) ||
                              std::abs(query.origin.y - earlier.origin.y) >=
                                  .5 * (query.dimensions[1] + earlier.dimensions[1]) ||
                              std::abs(query.origin.z - earlier.origin.z) >=
                                  .5 * (query.dimensions[2] + earlier.dimensions[2]),
                          "Saved Gallery Mesh bounds overlap another retained plant");
                separated_boxes.push_back(query);
            } else
                for (const auto& earlier : separated)
                    check(std::hypot(query.origin.x - earlier.x, query.origin.y - earlier.y,
                                     query.origin.z - earlier.z) >= 2 * separated_radius,
                          "Saved Gallery contains overlapping new clearance spheres");
            separated.push_back(query.origin);
        }
    }
    check(plants > 0 && plants < (separate ? 192u : 96u) && obstacles == 1 &&
              world.physics()->status().at("tick") == 0,
          "Clearance Gallery lacks ordinary saved plants/collider or progressed physics");
    if (separate)
        check(!separated.empty() && separated.size() < unseparated,
              "Separated Gallery lacks its visible dense/sparse comparison");
    const auto rendered = extract_render_scene(world.simulation.presentation(1));
    check(rendered.meshes.size() == plants + 3 && rendered.cameras.size() == 1 &&
              !rendered.lights.empty(),
          "Clearance Gallery ordinary rendering intent missing");
    std::cout << "Clearance Gallery actual native collision: " << plants << " saved plants clear\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--author") {
            author(argv[2]);
            return 0;
        }
        if (argc == 3 && std::string_view(argv[1]) == "--author-separated") {
            author(argv[2], Gallery::Separated);
            return 0;
        }
        if (argc == 3 && std::string_view(argv[1]) == "--author-stroke") {
            author(argv[2], Gallery::Stroke);
            return 0;
        }
        if (argc == 3 && std::string_view(argv[1]) == "--author-bounds") {
            author(argv[2], Gallery::MeshBounds);
            return 0;
        }
        const bool bounds_only = argc == 4 && std::string_view(argv[3]) == "--bounds";
        check(argc == 3 || bounds_only, "Need Gallery and scratch paths");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto root = scratch / "Gallery";
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        if (bounds_only) {
            const auto id = read(root / bounds_path).at("asset_id").get<AssetId>();
            exercise(root, id, Gallery::MeshBounds);
            const std::array roots{id};
            package_runtime_content(root, scratch / "Content", roots, {"windows", "d3d12"});
            std::filesystem::rename(scratch / "Content", scratch / "Relocated");
            std::filesystem::rename(root, scratch / "SourcesOffline");
            exercise(scratch / "Relocated", id, Gallery::MeshBounds);
            return 0;
        }
        const auto id = read(root / path).at("asset_id").get<AssetId>();
        const auto separated_id = read(root / separated_path).at("asset_id").get<AssetId>();
        const auto stroke_id = read(root / stroke_path).at("asset_id").get<AssetId>();
        exercise(root, id);
        exercise(root, separated_id, Gallery::Separated);
        exercise(root, stroke_id, Gallery::Stroke);
        const std::array roots{id, separated_id, stroke_id};
        package_runtime_content(root, scratch / "Content", roots, {"windows", "d3d12"});
        std::filesystem::rename(scratch / "Content", scratch / "Relocated");
        std::filesystem::rename(root, scratch / "SourcesOffline");
        exercise(scratch / "Relocated", id);
        exercise(scratch / "Relocated", separated_id, Gallery::Separated);
        exercise(scratch / "Relocated", stroke_id, Gallery::Stroke);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
