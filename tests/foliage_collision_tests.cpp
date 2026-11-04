#include "application_state_fixture.hpp"
#include "authored_capture_fixture.hpp"
#include "collision_authoring.hpp"
#include "foliage_collision.hpp"
#include "mesh_scatter_authoring.hpp"
#include <chrono>
#include <forge/assets.hpp>
#include <forge/project.hpp>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
Json settled(FoliageCollisionPreparation& task, const Scene& scene,
             const std::filesystem::path& root) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (task.busy()) {
        check(std::chrono::steady_clock::now() < deadline, "Clearance task stalled");
        task.poll(scene, root);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return task.state();
}
void exercise(const std::filesystem::path& base) {
    const auto root = std::filesystem::absolute(base / AssetId::generate().str());
    std::filesystem::create_directories(root);
    auto lease = std::make_shared<ProjectLease>(root);
    AssetCatalog(root).save(AssetCatalog::project_index(root));
    ProjectSettings(root).save(ProjectSettings::defaults("Clearance"));
    EngineContext engine;
    Scene scene(engine.world());
    auto create = [&](const char* name) {
        return authoring_command(scene, "entity.create",
                                 {{"recipe", "primitive.0"}, {"name", name}})
            .at("selected")
            .get<std::string>();
    };
    const auto source = create("Template"), obstacle = create("Obstacle");
    for (const char* type : {"forge.physics_body", "forge.box_collider"})
        for (const auto& target : {source, obstacle})
            authoring_command(scene, "component.add", {{"entity", target}, {"component", type}});
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
    detail::MeshScatterSettings scatter;
    scatter.radius = 3;
    scatter.spacing = 0;
    scatter.count = 32;
    const auto layout = detail::mesh_scatter_layout(scatter);
    const auto args = detail::mesh_scatter_arguments(source, scatter, "Cleared patch");
    const auto before = scene.document();
    const auto revision = scene.revision();
    FoliageCollisionPreparation task;
    const auto started = task.start(lease, scene, args);
    check(task.start(lease, scene, args).at("operation") == started.at("operation"),
          "Exact pending clearance retry created another operation");
    auto different = args;
    different["name"] = "Different request";
    bool busy_rejected = false;
    try {
        task.start(lease, scene, different);
    } catch (const AuthoringError& error) {
        busy_rejected = error.code == "foliage_collision_busy";
    }
    check(busy_rejected && task.state().at("operation") == started.at("operation"),
          "A different request replaced active native preparation");
    const auto done = settled(task, scene, root);
    check(done.at("state") == "ready", done.dump().c_str());
    const auto& result = done.at("result");
    unsigned expected = 0;
    for (unsigned i = 0; i < layout.size(); ++i) {
        // Source Box spans x[-.5,.5], z[-5,5], y[0,1]. Sphere radius .25
        // centered at candidate Y=.5 intersects iff |X| <= .75.
        const bool clear = std::abs(layout[i].offset[0]) > .75;
        check(result.at("arguments").at("placement_mask")[i] == clear,
              "Clearance differs from independent sphere/box geometry");
        expected += clear;
    }
    check(result.at("physics_tick") == 0 && expected > 0 && expected < scatter.count &&
              result.at("retained_count") == expected && result.at("scene_revision") == revision &&
              scene.document() == before && scene.revision() == revision,
          "Read-only clearance changed source intent or lost the candidate subset");
    check(task.start(lease, scene, args).at("operation") == started.at("operation"),
          "Exact ready clearance retry changed operation");
    const auto applied = authoring_command(scene, "mesh.scatter", result.at("arguments"));
    const auto after = scene.document();
    check(applied.at("created").size() == expected && scene.undo() && scene.document() == before &&
              scene.redo() && scene.document() == after,
          "Cleared objects did not use one ordinary Scene history entry");
    task.poll(scene, root);
    check(task.state().at("state") == "cancelled", "Changed Scene retained stale clearance");
    FoliageCollisionSettings ignored;
    ignored.ignored.push_back(EntityId::parse(obstacle));
    task.start(lease, scene, args, ignored);
    check(settled(task, scene, root).at("result").at("retained_count") == scatter.count,
          "Canonical exclusion failed to ignore the obstacle");
    FoliageCollisionSettings no_layers;
    no_layers.mask = 0;
    task.start(lease, scene, args, no_layers);
    check(settled(task, scene, root).at("result").at("retained_count") == scatter.count,
          "Zero mask did not select zero collision layers");
    task.start(lease, scene, args);
    task.cancel();
    check(!task.busy() && task.state().at("state") == "cancelled",
          "Explicit clearance cancellation retained work");
    auto one = scatter;
    one.count = 1;
    one.radius = .01;
    task.start(lease, scene, detail::mesh_scatter_arguments(source, one, "Blocked"));
    check(settled(task, scene, root).at("state") == "failed" && scene.document() == after,
          "Fully blocked clearance created an empty group or altered the source");
    auto subset = args;
    std::vector<bool> mask(scatter.count);
    unsigned combined_count = 0;
    for (unsigned i = 0; i < mask.size(); ++i) {
        mask[i] = i % 2 == 0;
        combined_count += mask[i] && std::abs(layout[i].offset[0]) > .75;
    }
    subset["placement_mask"] = mask;
    subset["world_heights"] = std::vector<double>(scatter.count, 0.);
    subset["world_normals"] = Json::array();
    for (unsigned i = 0; i < scatter.count; ++i)
        subset["world_normals"].push_back({{"x", 0.}, {"y", 1.}, {"z", 0.}});
    task.start(lease, scene, subset);
    auto composed = settled(task, scene, root).at("result");
    check(composed.at("retained_count") == combined_count &&
              composed.at("arguments").at("world_heights") == subset.at("world_heights") &&
              composed.at("arguments").at("world_normals") == subset.at("world_normals"),
          "Clearance lost original-order slope/height/normal correspondence");
    for (unsigned i = 0; i < mask.size(); ++i)
        check(composed.at("arguments").at("placement_mask")[i] ==
                  (mask[i] && std::abs(layout[i].offset[0]) > .75),
              "Clearance resampled or restored an excluded candidate");
    authoring_command(scene, "property.set",
                      {{"entity", obstacle},
                       {"component", "forge.physics_body"},
                       {"field", "sensor"},
                       {"value", true}});
    task.start(lease, scene, args);
    check(settled(task, scene, root).at("result").at("retained_count") == scatter.count,
          "Default clearance included sensors");
    FoliageCollisionSettings sensors;
    sensors.include_sensors = true;
    task.start(lease, scene, args, sensors);
    check(settled(task, scene, root).at("result").at("retained_count") == expected,
          "Sensor clearance did not use native collision filtering");
    FoliageCollisionSettings surface;
    surface.surface = EntityId::parse(obstacle);
    surface.include_sensors = true;
    task.start(lease, scene, args, surface);
    check(settled(task, scene, root).at("result").at("retained_count") == scatter.count,
          "Template/terrain exclusions did not use canonical identities");
    task.start(lease, scene, args);
    task.poll(scene, root / "another-project");
    check(task.state().at("state") == "cancelled", "Project switch kept native preparation");
}
void resource_and_state(const std::filesystem::path& base) {
    const auto root = std::filesystem::absolute(base / AssetId::generate().str());
    const auto state_snapshot = test::application_state_fixture(root);
    auto lease = std::make_shared<ProjectLease>(root);
    EngineContext engine;
    Scene scene(engine.world());
    scene.restore_snapshot(state_snapshot);
    scene.publish_component_schemas(test::authored_capture_manifest(1).at("components"));
    const auto source =
        authoring_command(scene, "entity.create",
                          {{"recipe", "primitive.0"}, {"name", "Resource template"}})
            .at("selected")
            .get<std::string>();
    authoring_command(scene, "component.add", {{"entity", source}, {"component", "project.pilot"}});
    const auto obstacle = authoring_command(scene, "entity.create", {{"name", "Cooked obstacle"}})
                              .at("selected")
                              .get<std::string>();
    auto collision = CollisionSource::create(AssetId::generate(), CollisionKind::Box);
    collision.document["nodes"][0]["dimensions"] = {1, 1, 10};
    atomic_write(root / "Assets/Clearance.collision.json", collision.document.dump(2));
    AssetImportService imports(lease, collision_import_registry(), {"linux", "none", "cpu"});
    imports.submit(
        imports.prepare("Assets/Clearance.collision.json", {}, collision.asset()),
        [](auto& candidate, const auto& plan, const auto&) {
            prepare_collision_publication(candidate, plan);
        },
        [](const auto&, const auto&) {});
    check(imports.wait_idle(std::chrono::seconds(10)), "Collision publication timed out");
    const auto receipts = imports.poll();
    check(receipts.size() == 1 && receipts[0].published,
          "Clearance collision asset did not publish");
    for (auto type : {"forge.physics_body", "forge.asset_collider"})
        authoring_command(scene, "component.add", {{"entity", obstacle}, {"component", type}});
    authoring_command(scene, "property.set",
                      {{"entity", obstacle},
                       {"component", "forge.physics_body"},
                       {"field", "motion"},
                       {"value", 0}});
    authoring_command(scene, "property.set",
                      {{"entity", obstacle},
                       {"component", "forge.asset_collider"},
                       {"field", "asset"},
                       {"value", collision.asset()}});
    authoring_command(scene, "transform.position",
                      {{"entity", obstacle}, {"value", {{"x", 0}, {"y", .5}, {"z", 0}}}});
    detail::MeshScatterSettings scatter;
    scatter.radius = 3;
    scatter.spacing = 0;
    scatter.count = 32;
    const auto args = detail::mesh_scatter_arguments(source, scatter, "Cooked clearance");
    const auto before = scene.document();
    const auto revision = scene.revision();
    FoliageCollisionPreparation task;
    task.start(lease, scene, args);
    const auto ready = settled(task, scene, root);
    check(ready.at("state") == "ready", ready.dump().c_str());
    const auto layout = detail::mesh_scatter_layout(scatter);
    for (unsigned i = 0; i < layout.size(); ++i)
        check(ready.at("result").at("arguments").at("placement_mask")[i] ==
                  (std::abs(layout[i].offset[0]) > .75),
              "Published collider clearance differs from native geometry");
    check(ready.at("result").at("physics_tick") == 0 && scene.document() == before &&
              scene.revision() == revision,
          "Private state/resource preparation progressed or changed custom authored intent");
    const auto old_operation = ready.at("operation");
    auto changed_ready_settings = ProjectSettings(root).document();
    changed_ready_settings["name"] = "Changed ready settings";
    ProjectSettings(root).save(changed_ready_settings);
    check(task.read(scene, root).at("state") == "failed",
          "Reading ready clearance retained stale project settings");
    const auto restarted = task.start(lease, scene, args);
    check(restarted.at("operation") != old_operation &&
              settled(task, scene, root).at("state") == "ready",
          "Changed settings reused an old ready operation");
    task.cancel();
    task.start(lease, scene, args);
    task.poll(scene, root);
    auto settings = ProjectSettings(root).document();
    settings["name"] = "Changed settings";
    ProjectSettings(root).save(settings);
    check(settled(task, scene, root).at("state") == "failed" && scene.document() == before,
          "Changed project settings admitted stale native clearance");
    authoring_command(scene, "property.set",
                      {{"entity", obstacle},
                       {"component", "forge.asset_collider"},
                       {"field", "asset"},
                       {"value", AssetId::generate()}});
    const auto missing = scene.document();
    task.start(lease, scene, args);
    check(settled(task, scene, root).at("state") == "failed" && scene.document() == missing,
          "Missing published collision resource yielded a clearance result");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need private test root");
        exercise(std::filesystem::u8path(argv[1]));
        resource_and_state(std::filesystem::u8path(argv[1]));
        std::cout << "Native clearance geometry, copied masks, retries/history/cancel passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
