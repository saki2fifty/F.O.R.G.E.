#include "collision_authoring.hpp"
#include "editable_mesh_authoring.hpp"
#include "editable_mesh_document.hpp"
#include "foliage_collision.hpp"
#include "mesh_scatter_authoring.hpp"
#include "runtime_package.hpp"
#include "spatial_document.hpp"
#include <algorithm>
#include <chrono>
#include <forge/engine_assets.hpp>
#include <forge/mesh_resource.hpp>
#include <forge/project.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime_world.hpp>
#include <forge/scene_identity.hpp>
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>

using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
AssetId publish_panels(const std::shared_ptr<ProjectLease>& lease,
                       const std::filesystem::path& path, unsigned repeats = 0) {
    auto source = EditableMeshDocument::create(lease, path, [repeats](AssetId id) {
        auto mesh = EditableMeshSource::create_planar_quad(id);
        const std::array faces{mesh.document.at("faces")[0].at("id").get<std::uint32_t>()};
        (void)mesh.duplicate_faces(faces, {4, 0, 0});
        for (unsigned i = 0; i < repeats; ++i)
            (void)mesh.duplicate_faces(faces, {0, 0, 0});
        return mesh;
    });
    source->save();
    const auto mesh_id = source->source().asset();
    AssetImportService imports(lease, editable_mesh_import_registry(),
                               {"windows", "d3d12", "desktop"});
    imports.submit(
        imports.prepare(path, {}, mesh_id),
        [](auto& candidate, const auto& plan, const auto&) {
            prepare_editable_mesh_publication(candidate, plan);
        },
        [](const auto&, const auto&) {});
    check(imports.wait_idle(std::chrono::seconds(30)), "Static panel publication stalled");
    const auto published = imports.poll();
    check(published.size() == 1 && published[0].published,
          "Static panel publication failed through its normal owner");

    return mesh_id;
}
void surface_baseline(const std::filesystem::path& scratch) {
    const auto root = std::filesystem::absolute(scratch) / AssetId::generate().str();
    std::filesystem::create_directories(root / "Assets");
    auto lease = std::make_shared<ProjectLease>(root);
    AssetCatalog(root).save(AssetCatalog::project_index(root));
    ProjectSettings(root).save(ProjectSettings::defaults("Static surface clearance"));
    const auto mesh_id = publish_panels(lease, "Assets/SeparatedPanels.mesh.json");

    EngineContext engine;
    Scene scene(engine.world());
    const auto create = [&] {
        return authoring_command(scene, "entity.create", {{"recipe", "primitive.0"}})
            .at("selected")
            .get<std::string>();
    };
    const auto prototype = create(), obstacle = create(), xy_obstacle = create();
    authoring_command(scene, "property.set",
                      {{"entity", prototype},
                       {"component", "forge.mesh_renderer"},
                       {"field", "mesh"},
                       {"value", mesh_id}});
    for (auto type : {"forge.physics_body", "forge.box_collider"})
        authoring_command(scene, "component.add", {{"entity", obstacle}, {"component", type}});
    authoring_command(scene, "property.set",
                      {{"entity", obstacle},
                       {"component", "forge.physics_body"},
                       {"field", "motion"},
                       {"value", 0}});
    authoring_command(scene, "transform.position",
                      {{"entity", obstacle}, {"value", {{"x", 2}, {"y", 0}, {"z", 0}}}});
    for (auto type : {"forge.physics_body_2d", "forge.box_collider_2d"})
        authoring_command(scene, "component.add", {{"entity", xy_obstacle}, {"component", type}});
    authoring_command(scene, "transform.position",
                      {{"entity", xy_obstacle}, {"value", {{"x", 2}, {"y", 0}, {"z", 0}}}});
    const auto before = scene.document();
    RuntimeWorld world({}, {}, std::nullopt, root, false);
    world.scene.restore_snapshot(scene.snapshot());
    const auto physics_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!world.prepare_physics_assets()) {
        check(std::chrono::steady_clock::now() < physics_deadline, "Native gap obstacle stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    world.physics()->synchronize(0);
    PhysicsOverlap gap;
    gap.origin = {2, 0, 0};
    gap.dimensions = {.1, 0, 0};
    check(world.physics()->overlap_any(gap), "The panel gap lacks an actual native obstacle");
    using Triangle = std::array<Double3, 3>;
    const Triangle clear{{{-1, -1, 0}, {-1, 5, 0}, {5, 5, 0}}};
    Triangle occupied{{{1.8, -.2, 0}, {2.2, -.2, 0}, {2, .2, 0}}};
    check(!world.physics()->overlap_triangle_any(clear) &&
              world.physics()->overlap_triangle_any(occupied),
          "Native triangle query lost empty bounds space or solid-target geometry");
    std::swap(occupied[1], occupied[2]);
    check(world.physics()->overlap_triangle_any(occupied),
          "Native triangle query lost the existing two-sided query policy");
    const std::array excluded{EntityRef{world.scene.asset_id(), EntityId::parse(obstacle)}};
    check(!world.physics()->overlap_triangle_any(occupied, {UINT32_MAX, true, excluded}) &&
              !world.physics()->overlap_triangle_any(occupied, {0, true}) &&
              !world.physics()->overlap_triangle_any(occupied, {2, true}),
          "Native triangle query bypassed canonical exclusions or project layers");
    for (unsigned invalid = 0; invalid < 3; ++invalid) {
        auto bad = occupied;
        if (invalid == 0)
            bad.fill(bad[0]);
        else if (invalid == 1)
            bad[0][0] = std::numeric_limits<double>::quiet_NaN();
        else
            bad[0][0] = 20000;
        bool rejected = false;
        try {
            (void)world.physics()->overlap_triangle_any(bad, {0, false});
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        check(rejected, "Native triangle query skipped invalid geometry with a zero layer mask");
    }
    using TriangleXY = std::array<Double2, 3>;
    const TriangleXY clear_xy{{{-1, -1}, {-1, 5}, {5, 5}}};
    TriangleXY occupied_xy{{{1.8, -.2}, {2.2, -.2}, {2, .2}}};
    const TriangleXY segment{{{2, -2}, {2, 0}, {2, 2}}};
    const TriangleXY point{{{2, 0}, {2, 0}, {2, 0}}};
    check(!world.physics()->overlap_triangle_any_2d(clear_xy) &&
              world.physics()->overlap_triangle_any_2d(occupied_xy) &&
              world.physics()->overlap_triangle_any_2d(segment) &&
              world.physics()->overlap_triangle_any_2d(point),
          "Native XY triangle query lost convex geometry or collapsed segment/point projection");
    std::swap(occupied_xy[1], occupied_xy[2]);
    check(world.physics()->overlap_triangle_any_2d(occupied_xy),
          "Native XY triangle query depends on projected winding");
    const std::array excluded_xy{EntityRef{world.scene.asset_id(), EntityId::parse(xy_obstacle)}};
    check(!world.physics()->overlap_triangle_any_2d(occupied_xy, {UINT32_MAX, true, excluded_xy}) &&
              !world.physics()->overlap_triangle_any_2d(occupied_xy, {0, true}) &&
              !world.physics()->overlap_triangle_any_2d(occupied_xy, {2, true}),
          "Native XY triangle query bypassed canonical exclusions or layers");
    RuntimeWorld empty({}, {}, std::nullopt, root, false);
    check(!empty.physics()->overlap_triangle_any_2d(point),
          "Empty lazy XY triangle query reported geometry");
    for (unsigned invalid = 0; invalid < 3; ++invalid) {
        auto bad = occupied_xy;
        bad[0][0] = invalid == 0   ? std::numeric_limits<double>::quiet_NaN()
                    : invalid == 1 ? 1000001.
                                   : 2000.;
        bool rejected = false;
        try {
            (void)empty.physics()->overlap_triangle_any_2d(bad, {0, false});
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        check(rejected, "Empty XY triangle query skipped finite/position/extent validation");
    }

    const auto resources = world.engine.services().resources();
    const auto token = resources->request(RuntimeResourceKind::Mesh, mesh_id);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    ResourceLease<MeshAsset> mesh;
    while (!(mesh = resources->mesh(token))) {
        resources->synchronize();
        check(std::chrono::steady_clock::now() < deadline, "Published panel resource stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    unsigned triangles = 0;
    for (const auto& part : mesh->mesh.lods.at(0).parts) {
        const auto* stream = part.find("POSITION");
        check(part.topology == MeshTopology::Triangles && stream && stream->components == 3 &&
                  std::holds_alternative<std::vector<float>>(stream->values),
              "Published panels lack canonical float3 triangle geometry");
        const auto& positions = std::get<std::vector<float>>(stream->values);
        const auto count = part.indices.empty() ? part.vertices : part.indices.size();
        check(count && count % 3 == 0, "Published panel triangles are incomplete");
        for (std::size_t i = 0; i < count; i += 3) {
            bool left = true, right = true;
            for (unsigned corner = 0; corner < 3; ++corner) {
                const auto vertex = part.indices.empty() ? i + corner : part.indices.at(i + corner);
                const auto x = positions.at(3 * vertex);
                left = left && x <= .5;
                right = right && x >= 3.5;
            }
            check(left || right, "A published panel triangle crosses the independently clear gap");
            ++triangles;
        }
    }
    check(triangles == 4, "Published panel topology differs from the independent fixture");
    resources->release(token);
    const auto resolved = world.engine.world().resolve(excluded[0]);
    check(resolved.state == WorldContext::ResolveState::Available,
          "Native triangle fixture lost its owned body before retirement");
    world.engine.world().world().entity(resolved.entity).destruct();
    check(!world.physics()->overlap_triangle_any(occupied),
          "Native triangle query counted a retired canonical body before synchronization");
    const auto resolved_xy = world.engine.world().resolve(excluded_xy[0]);
    check(resolved_xy.state == WorldContext::ResolveState::Available,
          "Native XY triangle fixture lost its body before retirement");
    world.engine.world().world().entity(resolved_xy.entity).destruct();
    check(!world.physics()->overlap_triangle_any_2d(occupied_xy),
          "Native XY triangle query counted a retired canonical body before synchronization");

    detail::MeshScatterSettings settings;
    settings.count = 2;
    settings.radius = .01;
    settings.spacing = 0;
    settings.scale_min = settings.scale_max = 1;
    settings.yaw_min = settings.yaw_max = 0;
    auto arguments = detail::mesh_scatter_arguments(prototype, settings, "Separated panels");
    arguments["world_heights"] = {0., 10.};
    FoliageCollisionSettings config;
    config.radius = .1;
    config.offset = {};
    FoliageCollisionPreparation task;
    const auto prepare = [&] {
        const auto unchanged = scene.document();
        task.start(lease, scene, arguments, config);
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        unsigned previous_queries = 0;
        while (task.busy()) {
            task.poll(scene, root);
            const auto queries = task.state().at("shape_query_count").get<unsigned>();
            check(queries >= previous_queries && queries - previous_queries <= 16,
                  "Surface preparation exceeded its bounded per-poll query work");
            previous_queries = queries;
            check(std::chrono::steady_clock::now() < limit, "Static panel clearance stalled");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto result = task.state();
        check(result.at("state") == "ready" && scene.document() == unchanged &&
                  result.at("result").at("physics_tick") == 0,
              "Static panel clearance changed intent or failed");
        return result.at("result");
    };
    check(prepare().at("retained_count") == 2, "Root sphere unexpectedly reaches the gap obstacle");
    config.profile = FoliageClearanceProfile::MeshBounds;
    const auto bounded = prepare();
    check(bounded.at("retained_count") == 1 &&
              bounded.at("arguments").at("placement_mask") == Json::array({false, true}),
          "Static panel bounds no longer demonstrate the empty-space rejection");
    for (const auto profile :
         {FoliageClearanceProfile::MeshSurfaces, FoliageClearanceProfile::MeshSurfacesXY}) {
        config.profile = profile;
        config.padding = 100; // Inactive in both surface modes.
        config.radius = .001; // Also inactive, including the XY mode.
        const auto surface = prepare();
        check(surface.at("retained_count") == 2 && surface.at("surface_triangle_count") == 4 &&
                  surface.at("shape_query_count") == 6 &&
                  surface.at("arguments").at("placement_mask") == Json::array({true, true}),
              "Surface clearance did not retain the independently clear Mesh geometry");
        config.offset[0] = 2;
        const auto contact = prepare();
        check(contact.at("arguments").at("placement_mask") == Json::array({false, true}),
              "Surface clearance ignored actual triangle contact or its world offset");
        config.ignored = {EntityId::parse(obstacle), EntityId::parse(xy_obstacle)};
        check(prepare().at("retained_count") == 2,
              "Surface clearance bypassed its canonical body exclusions");
        config.ignored.clear();
        config.mask = 0;
        check(prepare().at("retained_count") == 2,
              "Surface clearance ignored the collider layer mask");
        config.mask = UINT32_MAX;
        config.offset[0] = 0;
        config.avoid_new_overlaps = true;
        arguments["world_heights"] = {0., 0.};
        const auto separated = prepare();
        check(separated.at("retained_count") == 1 && separated.at("new_overlap_count") == 1 &&
                  separated.at("arguments").at("placement_mask") == Json::array({true, false}),
              "Surface mode lost conservative ordered new-plant separation");
        const auto placed = authoring_command(scene, "mesh.scatter", separated.at("arguments"));
        const auto after = scene.document();
        check(placed.at("created").size() == 1 && scene.undo() && scene.document() == before &&
                  scene.redo() && scene.document() == after && scene.undo(),
              "Surface placement lost ordinary atomic Scene history");
        arguments["placement_mask"] = {false, true};
        check(prepare().at("arguments").at("placement_mask") == Json::array({false, true}),
              "An incoming masked surface candidate blocked a later plant");
        arguments.erase("placement_mask");
        arguments["world_heights"] = {0., 10.};
        config.avoid_new_overlaps = false;
    }
    authoring_command(scene, "transform.local",
                      {{"entity", prototype},
                       {"translation", {{"x", 90}, {"y", 80}, {"z", 70}}},
                       {"rotation", detail::encode(rotation_from_euler({25, 35, 10}))},
                       {"scale", {{"x", -1.2}, {"y", 1.6}, {"z", .8}}}});
    settings.count = 32;
    settings.radius = 3;
    settings.scale_min = .6;
    settings.scale_max = 1.3;
    settings.yaw_min = -120;
    settings.yaw_max = 160;
    arguments = detail::mesh_scatter_arguments(prototype, settings, "Transformed panels");
    arguments["world_heights"] = Json::array();
    arguments["world_normals"] = Json::array();
    for (unsigned i = 0; i < settings.count; ++i) {
        arguments["world_heights"].push_back(double(i % 3) * 1.25);
        arguments["world_normals"].push_back({{"x", 0}, {"y", .8}, {"z", .6}});
    }
    config.offset = {.2, .3, -.1};
    std::vector<FoliageTriangle> local_triangles;
    (void)foliage_template_bounds(mesh.get(), &local_triangles);
    for (const bool xy : {false, true}) {
        config.profile =
            xy ? FoliageClearanceProfile::MeshSurfacesXY : FoliageClearanceProfile::MeshSurfaces;
        const auto prepared = prepare();
        const auto unchanged = scene.document();
        const auto created = authoring_command(scene, "mesh.scatter", arguments).at("created");
        check(created.size() == settings.count, "Transform oracle lost the unfiltered pool");
        {
            RuntimeWorld oracle({}, {}, std::nullopt, root, false);
            oracle.scene.restore_snapshot(scene.snapshot());
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!oracle.prepare_physics_assets()) {
                check(std::chrono::steady_clock::now() < deadline, "Transform oracle stalled");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            oracle.physics()->synchronize(0);
            const auto rendered = extract_render_scene(oracle.simulation.presentation(1));
            for (unsigned i = 0; i < settings.count; ++i) {
                const auto draw =
                    std::find_if(rendered.meshes.begin(), rendered.meshes.end(),
                                 [&](const auto& item) { return item.entity.str() == created[i]; });
                check(draw != rendered.meshes.end(), "Transform oracle lacks a saved plant");
                bool contact = false;
                for (const auto& local : local_triangles) {
                    FoliageTriangle triangle;
                    for (unsigned corner = 0; corner < 3; ++corner) {
                        triangle[corner] = draw->world.point(local[corner]);
                        for (unsigned axis = 0; axis < 3; ++axis)
                            triangle[corner][axis] += config.offset[axis];
                    }
                    contact |= xy ? oracle.physics()->overlap_triangle_any_2d(
                                        {{{triangle[0][0], triangle[0][1]},
                                          {triangle[1][0], triangle[1][1]},
                                          {triangle[2][0], triangle[2][1]}}})
                                  : oracle.physics()->overlap_triangle_any(triangle);
                }
                check(prepared.at("arguments").at("placement_mask")[i] == !contact,
                      "Surface preparation differs from saved signed/yaw/scale/slope transforms");
            }
        }
        check(scene.undo() && scene.document() == unchanged,
              "Independent transform oracle did not restore the authored Scene");
    }
    std::cout << "Four published triangles clear the actual native gap obstacle; "
                 "root spheres retain2 while enclosing Mesh boxes retain1\n";
}
void bounded_surface_work(const std::filesystem::path& scratch) {
    const auto root = std::filesystem::absolute(scratch) / AssetId::generate().str();
    std::filesystem::create_directories(root / "Assets");
    auto lease = std::make_shared<ProjectLease>(root);
    AssetCatalog(root).save(AssetCatalog::project_index(root));
    ProjectSettings(root).save(ProjectSettings::defaults("Surface query budget"));
    EngineContext engine;
    Scene scene(engine.world());
    const auto mesh = publish_panels(lease, "Assets/BudgetPanels.mesh.json", 32);
    const auto source = authoring_command(scene, "entity.create", {{"recipe", "primitive.0"}})
                            .at("selected")
                            .get<std::string>();
    const auto collider = authoring_command(scene, "entity.create", {{"recipe", "primitive.0"}})
                              .at("selected")
                              .get<std::string>();
    for (const char* type : {"forge.physics_body", "forge.box_collider"})
        authoring_command(scene, "component.add", {{"entity", collider}, {"component", type}});
    authoring_command(scene, "property.set",
                      {{"entity", collider},
                       {"component", "forge.physics_body"},
                       {"field", "motion"},
                       {"value", 0}});
    for (const char* axis : {"x", "y", "z"})
        authoring_command(scene, "property.set",
                          {{"entity", collider},
                           {"component", "forge.box_collider"},
                           {"field", axis},
                           {"value", .01}});
    authoring_command(scene, "property.set",
                      {{"entity", source},
                       {"component", "forge.mesh_renderer"},
                       {"field", "mesh"},
                       {"value", mesh}});
    authoring_command(scene, "transform.position",
                      {{"entity", collider}, {"value", {{"x", 2}, {"y", 0}, {"z", 0}}}});
    detail::MeshScatterSettings settings;
    settings.count = 512;
    settings.radius = .01;
    settings.spacing = 0;
    settings.scale_min = settings.scale_max = 1;
    settings.yaw_min = settings.yaw_max = 0;
    const auto arguments = detail::mesh_scatter_arguments(source, settings, "Budget plants");
    const auto before = scene.document();
    const auto revision = scene.revision();
    FoliageCollisionSettings config;
    config.profile = FoliageClearanceProfile::MeshSurfaces;
    config.offset = {};
    FoliageCollisionPreparation task;
    const auto first = task.start(lease, scene, arguments, config);
    check(task.start(lease, scene, arguments, config).at("operation") == first.at("operation"),
          "An exact pending surface request did not reuse its operation");
    task.cancel();
    check(task.state().at("state") == "cancelled" && scene.document() == before,
          "Surface cancellation published partial Scene objects");
    task.start(lease, scene, arguments, config);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(35);
    unsigned previous = 0;
    bool partial_candidate = false;
    while (task.busy()) {
        task.poll(scene, root);
        const auto state = task.state();
        const auto count = state.at("shape_query_count").get<unsigned>();
        check(count >= previous && count - previous <= 16 && scene.revision() == revision,
              "Surface work exceeded its per-poll budget or published partial objects");
        partial_candidate |= count >= 16 && state.at("checked_count") == 0;
        previous = count;
        check(std::chrono::steady_clock::now() < deadline,
              ("Bounded surface fixture stalled: " + state.dump()).c_str());
        if (state.at("state") == "preparing")
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto failed = task.state();
    check(scene.document() == before && scene.revision() == revision && partial_candidate &&
              failed.at("state") == "failed" &&
              failed.at("shape_query_count") == foliage_shape_query_limit &&
              !failed.contains("result") &&
              failed.at("message").get<std::string>().find("8192") != std::string::npos,
          ("Surface query budget/cursor failure: " + failed.dump()).c_str());
    // Repeated nondegenerate panels miss the native gap obstacle while their
    // envelope contains it, forcing enough actual triangle requests to hit the budget.
    config.mask = 0;
    task.start(lease, scene, arguments, config);
    const auto recovery_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(35);
    while (task.busy()) {
        check(std::chrono::steady_clock::now() < recovery_deadline, "Surface recovery stalled");
        task.poll(scene, root);
    }
    check(task.state().at("state") == "ready" && task.state().at("retained_count") == 512 &&
              task.state().at("shape_query_count") == 512 && scene.document() == before,
          "Surface request did not recover after a bounded failure");
}
void surface_envelope_edges(const std::filesystem::path& scratch) {
    for (const bool xy : {false, true}) {
        const auto root = std::filesystem::absolute(scratch) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        auto lease = std::make_shared<ProjectLease>(root);
        AssetCatalog(root).save(AssetCatalog::project_index(root));
        ProjectSettings(root).save(ProjectSettings::defaults("Surface envelope corners"));
        EngineContext engine;
        Scene scene(engine.world());
        const auto create = [&] {
            return authoring_command(scene, "entity.create", {{"recipe", "primitive.0"}})
                .at("selected")
                .get<std::string>();
        };
        const auto source = create(), collider = create();
        const double size = xy ? 1000 : 10000;
        const double center = (xy ? 1e6 : 1e9) - size / 2 - 1;
        authoring_command(
            scene, "transform.scale",
            {{"entity", source}, {"value", {{"x", -size}, {"y", size}, {"z", size}}}});
        const auto body = xy ? "forge.physics_body_2d" : "forge.physics_body";
        const auto shape = xy ? "forge.box_collider_2d" : "forge.sphere_collider";
        for (const auto* type : {body, shape})
            authoring_command(scene, "component.add", {{"entity", collider}, {"component", type}});
        authoring_command(
            scene, "property.set",
            {{"entity", collider}, {"component", body}, {"field", "motion"}, {"value", 0}});
        authoring_command(
            scene, "property.set",
            {{"entity", collider}, {"component", body}, {"field", "sensor"}, {"value", true}});
        detail::MeshScatterSettings settings;
        settings.center = {center, 0, 0};
        settings.count = 2;
        settings.radius = .01;
        settings.spacing = 0;
        settings.scale_min = settings.scale_max = 1;
        settings.yaw_min = settings.yaw_max = 0;
        auto arguments = detail::mesh_scatter_arguments(source, settings, "Surface corners");
        arguments["world_heights"] = {0., 2 * size};
        FoliageCollisionSettings config;
        config.profile =
            xy ? FoliageClearanceProfile::MeshSurfacesXY : FoliageClearanceProfile::MeshSurfaces;
        config.offset = {};
        FoliageCollisionPreparation task;
        const auto run = [&] {
            const auto before = scene.document();
            task.start(lease, scene, arguments, config);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (task.busy()) {
                check(std::chrono::steady_clock::now() < deadline,
                      "Large-coordinate surface envelope fixture stalled");
                task.poll(scene, root);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            const auto state = task.state();
            check(state.at("state") == "ready" && scene.document() == before,
                  "Large surface envelope failed its native profile or changed Scene intent");
            return state.at("result");
        };
        for (const double sign : {-1., 1.}) {
            authoring_command(scene, "transform.position",
                              {{"entity", collider},
                               {"value",
                                {{"x", center + sign * (size / 2 - .1)},
                                 {"y", sign * (size / 2 - .1)},
                                 {"z", xy ? 0 : sign * (size / 2 - .1)}}}});
            config.include_sensors = false;
            check(run().at("retained_count") == 2,
                  "Surface envelope ignored sensor exclusion near its corners");
            config.include_sensors = true;
            const auto contact = run();
            check(contact.at("arguments").at("placement_mask") == Json::array({false, true}) &&
                      (!xy || contact.at("shape_query_count") > 4),
                  "Surface envelope culled corner contact or lost split-circle coverage");
        }
    }
}
void native_mesh_target(const std::filesystem::path& scratch) {
    const auto root = std::filesystem::absolute(scratch) / AssetId::generate().str();
    std::filesystem::create_directories(root / "Assets");
    auto lease = std::make_shared<ProjectLease>(root);
    AssetCatalog(root).save(AssetCatalog::project_index(root));
    ProjectSettings(root).save(ProjectSettings::defaults("Native Mesh triangle contact"));
    const auto source = CollisionSource::from_mesh(AssetId::generate(), CollisionKind::TriangleMesh,
                                                   engine_primitive(0));
    atomic_write(root / "Assets/Target.collision.json", source.document.dump(2));
    AssetImportService imports(lease, collision_import_registry(), collision_import_target());
    imports.submit(
        imports.prepare("Assets/Target.collision.json", {}, source.asset()),
        [](auto& candidate, const auto& plan, const auto&) {
            prepare_collision_publication(candidate, plan);
        },
        [](const auto&, const auto&) {});
    check(imports.wait_idle(std::chrono::seconds(20)), "Mesh target publication stalled");
    const auto results = imports.poll();
    check(results.size() == 1 && results[0].published, "Native Mesh target failed publication");
    RuntimeWorld world({}, {}, std::nullopt, root, false);
    const auto target = authoring_command(world.scene, "entity.create", {{"name", "Mesh target"}})
                            .at("selected")
                            .get<std::string>();
    for (const auto* type : {"forge.physics_body", "forge.asset_collider"})
        authoring_command(world.scene, "component.add", {{"entity", target}, {"component", type}});
    authoring_command(world.scene, "property.set",
                      {{"entity", target},
                       {"component", "forge.physics_body"},
                       {"field", "motion"},
                       {"value", 0}});
    authoring_command(world.scene, "property.set",
                      {{"entity", target},
                       {"component", "forge.asset_collider"},
                       {"field", "asset"},
                       {"value", source.asset()}});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!world.prepare_physics_assets()) {
        check(std::chrono::steady_clock::now() < deadline, "Native Mesh target load stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    world.physics()->synchronize(0);
    FoliageTriangle crossing{{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}};
    const FoliageTriangle inside{{{0, 0, 0}, {.1, 0, 0}, {0, .1, 0}}};
    check(world.physics()->overlap_triangle_any(crossing) &&
              !world.physics()->overlap_triangle_any(inside),
          "Triangle/Mesh query lost native surface contact or gained solid containment");
    std::swap(crossing[1], crossing[2]);
    check(world.physics()->overlap_triangle_any(crossing),
          "Triangle/Mesh query lost two-sided contact");
    bool wrong_thread = false;
    std::thread other([&] {
        try {
            (void)world.physics()->overlap_triangle_any(crossing);
        } catch (const std::runtime_error&) {
            wrong_thread = true;
        }
    });
    other.join();
    check(wrong_thread, "Triangle query bypassed its native owner-thread boundary");
}
void author_gallery(const std::filesystem::path& project) {
    const auto root = std::filesystem::absolute(project);
    constexpr auto destination = "Scenes/269-foliage-mesh-surfaces.scene.json";
    check(!std::filesystem::exists(root / destination), "Surface Gallery already exists");
    std::filesystem::create_directories(root / "Assets/Foliage");
    auto lease = std::make_shared<ProjectLease>(root);
    const auto mesh = publish_panels(lease, "Assets/Foliage/SurfacePanels.mesh.json");
    EngineContext engine;
    Scene scene(engine.world());
    Json original;
    std::ifstream(root / "Scenes/147-terrain-foliage.scene.json") >> original;
    auto source = duplicate_scene_asset(original, AssetId::generate(), scene.schema());
    std::erase_if(source["entities"].get_ref<Json::array_t&>(), [](const Json& row) {
        return !row.at("components").contains("forge.camera") &&
               !row.at("components").contains("forge.light") &&
               !row.at("name").get<std::string>().starts_with("Blade template");
    });
    std::string prototype;
    Json colors;
    for (auto& row : source["entities"]) {
        auto& components = row["components"];
        if (components.contains("forge.camera")) {
            components["forge.local_translation"] = {{"x", 0}, {"y", 7}, {"z", 12}};
            components["forge.local_rotation"] = detail::encode(rotation_from_euler({28, 180, 0}));
        }
        if (components.contains("forge.light"))
            components["forge.local_rotation"] = detail::encode(rotation_from_euler({28, 180, 0}));
        if (row.at("name").get<std::string>().starts_with("Blade template")) {
            prototype = row.at("id");
            row["name"] = "Blade template - static separated panels";
            components["forge.local_translation"] = {{"x", -10}, {"y", -4}, {"z", 0}};
            components["forge.local_scale"] = {{"x", 1}, {"y", 1}, {"z", 1}};
            components["forge.mesh_renderer"]["mesh"] = mesh;
            colors = components["forge.mesh_renderer"]["material_parameters"];
        }
    }
    check(!prototype.empty(), "Surface Gallery template missing");
    scene.reset(source);
    for (unsigned patch = 0; patch < 2; ++patch) {
        const double x = patch ? 2 : -6;
        const auto obstacle =
            authoring_command(scene, "entity.create",
                              {{"recipe", "primitive.0"},
                               {"name", patch ? "Surface gap obstacle" : "Bounds gap obstacle"}})
                .at("selected");
        authoring_command(scene, "transform.position",
                          {{"entity", obstacle}, {"value", {{"x", x + 2}, {"y", 0}, {"z", 0}}}});
        for (const char* type : {"forge.physics_body", "forge.box_collider"})
            authoring_command(scene, "component.add", {{"entity", obstacle}, {"component", type}});
        authoring_command(scene, "property.set",
                          {{"entity", obstacle},
                           {"component", "forge.physics_body"},
                           {"field", "motion"},
                           {"value", 0}});
        auto values = colors;
        if (!patch)
            for (auto& value : values)
                if (value.at("key") == "baseColorFactor")
                    value["parameter"]["value"] = {.8, .35, .04, 1.};
        authoring_command(scene, "property.set",
                          {{"entity", prototype},
                           {"component", "forge.mesh_renderer"},
                           {"field", "material_parameters"},
                           {"value", values}});
        detail::MeshScatterSettings settings;
        settings.center = {x, 0, 0};
        settings.count = 2;
        settings.radius = .01;
        settings.spacing = 0;
        settings.scale_min = settings.scale_max = 1;
        settings.yaw_min = settings.yaw_max = 0;
        auto arguments = detail::mesh_scatter_arguments(
            prototype, settings, patch ? "Surface-clear panels" : "Bounds-clear panels");
        arguments["world_heights"] = {0., 3.};
        FoliageCollisionSettings config;
        config.profile =
            patch ? FoliageClearanceProfile::MeshSurfaces : FoliageClearanceProfile::MeshBounds;
        config.offset = {};
        FoliageCollisionPreparation task;
        task.start(lease, scene, arguments, config);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (task.busy()) {
            check(std::chrono::steady_clock::now() < deadline, "Surface Gallery clearance stalled");
            task.poll(scene, root);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto ready = task.read(scene, root);
        check(ready.at("state") == "ready" && ready.at("retained_count") == (patch ? 2 : 1),
              ready.dump().c_str());
        apply_authoring(scene,
                        Json::array({{{"operation", "mesh.scatter"},
                                      {"arguments", ready.at("result").at("arguments")}}}),
                        ready.at("result").at("scene_revision"));
    }
    scene.save(root / destination);
    auto catalog = AssetCatalog::open_project(root);
    catalog.add_scene(destination);
    catalog.save(AssetCatalog::project_index(root));
    const std::array roots{scene.asset_id()};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
    std::cout << "Surface Gallery: bounds1 vs surfaces2 ordinary saved objects\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--author-gallery") {
            author_gallery(argv[2]);
            return 0;
        }
        check(argc == 2, "Need an isolated static surface clearance root");
        surface_baseline(argv[1]);
        bounded_surface_work(argv[1]);
        surface_envelope_edges(argv[1]);
        native_mesh_target(argv[1]);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
