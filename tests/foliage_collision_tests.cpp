#include "application_state_fixture.hpp"
#include "authored_capture_fixture.hpp"
#include "collision_authoring.hpp"
#include "engine_render_resource.hpp"
#include "foliage_collision.hpp"
#include "mesh_scatter_authoring.hpp"
#include "spatial_document.hpp"
#include <chrono>
#include <cmath>
#include <forge/assets.hpp>
#include <forge/project.hpp>
#include <forge/runtime_world.hpp>
#include <iostream>
#include <limits>
#include <string_view>
#include <thread>
using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void candidate_workload(const std::filesystem::path& base, bool separate = false) {
    const auto root = std::filesystem::absolute(base / AssetId::generate().str());
    std::filesystem::create_directories(root);
    auto lease = std::make_shared<ProjectLease>(root);
    AssetCatalog(root).save(AssetCatalog::project_index(root));
    ProjectSettings(root).save(ProjectSettings::defaults("Candidate clearance"));
    EngineContext engine;
    Scene scene(engine.world());
    const auto source = authoring_command(scene, "entity.create", {{"recipe", "primitive.0"}})
                            .at("selected")
                            .get<std::string>();
    detail::MeshScatterSettings scatter;
    scatter.radius = 1000;
    scatter.spacing = 0;
    scatter.count = 512;
    scatter.seed = 314159;
    const auto arguments = detail::mesh_scatter_arguments(source, scatter, "Maximum candidates");
    const auto before = scene.document();
    const auto revision = scene.revision();
    FoliageCollisionSettings settings;
    settings.radius = .001;
    settings.mask = 0;
    settings.avoid_new_overlaps = separate;
    FoliageCollisionPreparation task;
    const auto started = std::chrono::steady_clock::now();
    task.start(lease, scene, arguments, settings);
    unsigned polls = 0;
    while (task.busy()) {
        check(++polls <= 40, "Maximum clearance lost its bounded polling");
        task.poll(scene, root);
    }
    const auto elapsed =
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started)
            .count();
    const auto state = task.state();
    check(state.at("state") == "ready" && state.at("checked_count") == 512 &&
              state.at("result").at("retained_count") == 512 &&
              state.at("result").at("physics_tick") == 0,
          "Maximum default clearance lost candidates or advanced simulation");
    check(scene.document() == before && scene.revision() == revision,
          "Maximum read-only clearance changed Scene intent");
    std::cout << (separate ? "Separate" : "Default")
              << " 512-candidate clearance microseconds: " << elapsed << '\n';
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
void xy_clearance(const std::filesystem::path& base) {
    const auto root = std::filesystem::absolute(base / AssetId::generate().str());
    std::filesystem::create_directories(root);
    auto lease = std::make_shared<ProjectLease>(root);
    AssetCatalog(root).save(AssetCatalog::project_index(root));
    auto project = ProjectSettings::defaults("XY foliage clearance");
    project["physics"]["layers"][1] = "Second query layer";
    ProjectSettings(root).save(project);
    EngineContext engine;
    Scene scene(engine.world());
    const auto create = [&] {
        return authoring_command(scene, "entity.create", {{"recipe", "primitive.0"}})
            .at("selected")
            .get<std::string>();
    };
    const auto source = create(), obstacle = create();
    for (const char* type : {"forge.physics_body_2d", "forge.box_collider_2d"})
        authoring_command(scene, "component.add", {{"entity", obstacle}, {"component", type}});
    authoring_command(scene, "transform.position",
                      {{"entity", obstacle}, {"value", {{"x", 0}, {"y", .5}, {"z", 0}}}});
    detail::MeshScatterSettings scatter;
    scatter.count = 8;
    scatter.radius = .01;
    scatter.spacing = 0;
    const auto arguments = detail::mesh_scatter_arguments(source, scatter, "XY plants");
    FoliageCollisionPreparation task;
    const auto before = scene.document();
    task.start(lease, scene, arguments);
    const auto legacy = settled(task, scene, root);
    check(legacy.at("state") == "ready" && legacy.at("retained_count") == 8 &&
              scene.document() == before,
          "Legacy3D foliage profile unexpectedly queried XY geometry or changed intent");
    FoliageCollisionSettings xy;
    xy.profile = FoliageClearanceProfile::CircleXY;
    task.start(lease, scene, arguments, xy);
    check(settled(task, scene, root).at("state") == "failed" && scene.document() == before,
          "XY circle failed to reject native XY occupancy atomically");
    xy.mask = 2;
    task.start(lease, scene, arguments, xy);
    check(settled(task, scene, root).at("retained_count") == 8,
          "XY circle ignored collider layer filtering");
    xy.mask = UINT32_MAX;
    xy.ignored = {EntityId::parse(obstacle)};
    task.start(lease, scene, arguments, xy);
    check(settled(task, scene, root).at("retained_count") == 8,
          "XY circle ignored exact canonical exclusions");
    xy.ignored.clear();
    authoring_command(scene, "property.set",
                      {{"entity", obstacle},
                       {"component", "forge.physics_body_2d"},
                       {"field", "sensor"},
                       {"value", true}});
    task.start(lease, scene, arguments, xy);
    check(settled(task, scene, root).at("retained_count") == 8,
          "XY clearance counted a sensor by default");
    xy.include_sensors = true;
    task.start(lease, scene, arguments, xy);
    check(settled(task, scene, root).at("state") == "failed",
          "XY clearance omitted an explicitly included sensor");
    authoring_command(scene, "property.set",
                      {{"entity", obstacle},
                       {"component", "forge.physics_body_2d"},
                       {"field", "sensor"},
                       {"value", false}});
    scatter.count = 1;
    auto dab = detail::mesh_scatter_arguments(source, scatter, "XY stroke");
    dab.erase("entity");
    dab.erase("name");
    auto distant = dab;
    distant["center"]["z"] = 2e9;
    Json stroke{{"entity", source}, {"name", "XY stroke"}, {"dabs", {dab, distant}}};
    xy.mask = 0;
    xy.avoid_new_overlaps = true;
    for (const auto profile :
         {FoliageClearanceProfile::CircleXY, FoliageClearanceProfile::MeshBoundsXY}) {
        xy.profile = profile;
        task.start(lease, scene, stroke, xy);
        const auto state = settled(task, scene, root);
        check(state.at("state") == "ready" && state.at("retained_count") == 1 &&
                  state.at("new_overlap_count") == 1 &&
                  state.at("result").at("retained_dab_indices") == Json::array({0}),
              "XY cross-dab separation used Z or rejected unused native Z coordinates");
    }
    const auto stroke_before = scene.document();
    const auto result = task.state().at("result");
    const auto placed = authoring_command(scene, "mesh.stroke", result.at("arguments"));
    check(placed.at("placed_count") == 1 && scene.undo() && scene.document() == stroke_before &&
              scene.redo() && scene.undo() && scene.document() == stroke_before,
          "XY clearance bypassed ordinary atomic placement/history");
    RuntimeWorld world({}, ProjectSettings(root).physics(), std::nullopt, root, false);
    world.scene.restore_snapshot(scene.snapshot());
    const auto prepare = [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!world.prepare_physics_assets()) {
            check(std::chrono::steady_clock::now() < deadline, "XY native geometry stalled");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        world.physics()->synchronize(0);
    };
    prepare();
    PhysicsOverlap2D query;
    query.origin = {0, .5};
    query.dimensions = {.25, 0};
    check(world.physics()->overlap_shape_2d(query).size() == 1,
          "Native XY obstacle counterexample lacks actual geometry");
    auto dense = scene.document();
    Json body;
    for (const auto& row : dense.at("entities"))
        if (row.at("id") == obstacle)
            body = row;
    std::erase_if(dense["entities"].get_ref<Json::array_t&>(),
                  [&](const Json& row) { return row.at("id") == obstacle; });
    for (unsigned i = 0; i < 300; ++i) {
        auto row = body;
        row["id"] = EntityId::generate();
        row["components"]["forge.physics_body_2d"]["layer"] = i % 2;
        dense["entities"].push_back(std::move(row));
    }
    scene.edit(dense);
    world.scene.restore_snapshot(scene.snapshot());
    prepare();
    check(world.physics()->overlap_shape_2d(query, {1, false}).size() == 150 &&
              world.physics()->overlap_shape_2d(query, {2, false}).size() == 150,
          "Dense XY fixture has invalid native geometry or layer ownership");
    check(world.physics()->overlap_any_2d(query, {1, false}) &&
              world.physics()->overlap_any_2d(query, {2, false}) &&
              !world.physics()->overlap_any_2d(query, {4, false}) &&
              !world.physics()->overlap_any_2d(query, {0, false}),
          "Dense XY existence query failed native early-out or layer filtering");
    bool limited = false;
    try {
        (void)world.physics()->overlap_shape_2d(query);
    } catch (const std::runtime_error&) {
        limited = true;
    }
    check(limited, "Dense XY all-hit query lost its existing256unique-body limit");
    auto current = world.physics()->overlap_shape_2d(query, {1, false});
    const auto other = world.physics()->overlap_shape_2d(query, {2, false});
    current.insert(current.end(), other.begin(), other.end());
    for (const auto ref : current) {
        const auto selected = world.engine.world().resolve(ref);
        check(selected.state == WorldContext::ResolveState::Available,
              "Dense XY fixture lost a canonical binding before retirement");
        world.engine.world().world().entity(selected.entity).destruct();
    }
    check(!world.physics()->overlap_any_2d(query),
          "XY existence query counted retired canonical targets before native synchronization");
    RuntimeWorld empty({}, ProjectSettings(root).physics(), std::nullopt, root, false);
    check(!empty.physics()->overlap_any_2d(query),
          "Empty lazy XY world reported occupied geometry");
    query.dimensions[0] = -1;
    bool invalid = false;
    try {
        (void)empty.physics()->overlap_any_2d(query, {0, false});
    } catch (const std::runtime_error&) {
        invalid = true;
    }
    check(invalid, "Empty/masked XY existence query skipped shape validation");
    query.dimensions[0] = .25;
    const std::array<EntityRef, physics_query_exclusion_limit + 1> too_many{};
    const std::array<EntityRef, 1> incomplete{};
    for (const auto exclusions :
         {std::span<const EntityRef>(too_many), std::span<const EntityRef>(incomplete)}) {
        bool rejected = false;
        try {
            (void)empty.physics()->overlap_any_2d(query, {0, false, exclusions});
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        check(rejected, "Empty/masked XY existence query skipped exclusion validation");
    }
    bool wrong_thread = false;
    std::thread other_thread([&] {
        try {
            (void)empty.physics()->overlap_any_2d(query);
        } catch (const std::runtime_error&) {
            wrong_thread = true;
        }
    });
    other_thread.join();
    check(wrong_thread, "XY existence query bypassed its owner-thread guard");
    std::cout << "Native XY baseline:3D foliage retains8 despite XY occupancy; "
                 "300valid native bodies exceed the bounded all-hit query\n";
}
void template_geometry_admission() {
    const auto original = asset_detail::engine_mesh_resource(engine_primitive(0));
    auto lods = original;
    auto extra = lods.mesh.lods.front();
    extra.screen_coverage = .5f;
    for (auto& part : extra.parts) {
        for (auto& stream : part.streams)
            if (stream.semantic == "POSITION") {
                auto& values = std::get<std::vector<float>>(stream.values);
                for (std::size_t i = 0; i < values.size(); i += 3)
                    values[i] += 4;
            }
        part.bounds = mesh_bounds(part);
    }
    lods.mesh.lods.push_back(extra);
    const auto bounds = foliage_template_bounds(lods);
    check(bounds.minimum[0] == original.mesh.lods[0].parts[0].bounds.minimum[0] &&
              bounds.maximum[0] == extra.parts[0].bounds.maximum[0],
          "Template bounds discarded a static LOD's geometry");
    const auto refused = [&](const MeshResourceData& value) {
        bool rejected = false;
        try {
            (void)foliage_template_bounds(value);
        } catch (const AuthoringError& error) {
            rejected = error.code == "unsupported_template";
        }
        check(rejected, "Deformed Mesh was admitted as static foliage bounds");
    };
    auto model = original;
    model.model.emplace();
    model.model->nodes.push_back({AssetId::generate(), 0, {}, true, true});
    refused(model);
    model.model->nodes[0].skin.reset();
    model.model->nodes[0].morph_weights = {1};
    refused(model);
    auto morph = original;
    morph.mesh.morph_names = {"Bend"};
    morph.mesh.morph_defaults = {0};
    for (auto& part : morph.mesh.lods[0].parts)
        part.morph_targets.push_back({{"POSITION", 3, std::vector<float>(part.vertices * 3)}});
    refused(morph);
    auto skin = original;
    for (auto& part : skin.mesh.lods[0].parts) {
        part.streams.push_back({"JOINTS_0", 4, std::vector<std::uint32_t>(part.vertices * 4)});
        std::vector<float> weights(part.vertices * 4);
        for (std::size_t i = 0; i < weights.size(); i += 4)
            weights[i] = 1;
        part.streams.push_back({"WEIGHTS_0", 4, std::move(weights)});
    }
    refused(skin);
}
void mesh_bounds_clearance(const std::filesystem::path& base, bool xy = false) {
    const auto root = std::filesystem::absolute(base / AssetId::generate().str());
    std::filesystem::create_directories(root);
    auto lease = std::make_shared<ProjectLease>(root);
    AssetCatalog(root).save(AssetCatalog::project_index(root));
    ProjectSettings(root).save(ProjectSettings::defaults("Template bounds clearance"));
    EngineContext engine;
    Scene scene(engine.world());
    const auto create = [&] {
        return authoring_command(scene, "entity.create", {{"recipe", "primitive.0"}})
            .at("selected")
            .get<std::string>();
    };
    const auto source = create(), obstacle = create();
    for (const char* type : {xy ? "forge.physics_body_2d" : "forge.physics_body",
                             xy ? "forge.box_collider_2d" : "forge.box_collider"})
        authoring_command(scene, "component.add", {{"entity", obstacle}, {"component", type}});
    authoring_command(scene, "property.set",
                      {{"entity", obstacle},
                       {"component", xy ? "forge.physics_body_2d" : "forge.physics_body"},
                       {"field", "motion"},
                       {"value", 0}});
    const auto place_obstacle = [&](double x, double y) {
        authoring_command(scene, "transform.position",
                          {{"entity", obstacle}, {"value", {{"x", x}, {"y", y}, {"z", 0}}}});
    };
    const auto template_pose = [&](double x_scale, double yaw) {
        authoring_command(scene, "transform.local",
                          {{"entity", source},
                           {"translation", {{"x", 90}, {"y", 80}, {"z", 70}}},
                           {"rotation", detail::encode(rotation_about_axis({0, 1, 0}, yaw))},
                           {"scale", {{"x", x_scale}, {"y", 2}, {"z", .2}}}});
    };
    template_pose(6, 0);
    place_obstacle(2.5, 0);
    detail::MeshScatterSettings scatter;
    scatter.radius = .01;
    scatter.spacing = 0;
    scatter.count = 2;
    scatter.scale_min = scatter.scale_max = 1;
    scatter.yaw_min = scatter.yaw_max = 0;
    auto arguments = detail::mesh_scatter_arguments(source, scatter, "Bounded plants");
    arguments["world_heights"] = {0., 10.};
    FoliageCollisionSettings settings;
    settings.radius = .1;
    settings.offset = {};
    settings.profile =
        xy ? FoliageClearanceProfile::CircleXY : FoliageClearanceProfile::WorldSphere;
    FoliageCollisionPreparation task;
    const auto query = [&] {
        const auto before = scene.document();
        const auto revision = scene.revision();
        task.start(lease, scene, arguments, settings);
        const auto state = settled(task, scene, root);
        check(state.at("state") == "ready" && scene.document() == before &&
                  scene.revision() == revision && state.at("result").at("physics_tick") == 0,
              "Template bounds clearance failed or changed the source Scene");
        return state.at("result");
    };
    check(query().at("retained_count") == 2,
          "The root-sphere counterexample unexpectedly contacted its distant obstacle");
    settings.profile =
        xy ? FoliageClearanceProfile::MeshBoundsXY : FoliageClearanceProfile::MeshBounds;
    auto result = query();
    check(result.at("retained_count") == 1 &&
              result.at("arguments").at("placement_mask") == Json::array({false, true}) &&
              result.at("settings").at("profile") == (xy ? "mesh-bounds-xy-v1" : "mesh-bounds-v1"),
          "Mesh bounds failed to reject the canopy while preserving the raised candidate");
    arguments["yaw_min"] = arguments["yaw_max"] = 90;
    check(query().at("retained_count") == 2, "Mesh bounds ignored seeded placement yaw");
    template_pose(-6, 90);
    check(query().at("retained_count") == 1,
          "Mesh bounds lost mirrored template scale or template-plus-placement rotation");
    template_pose(-6, 0);
    arguments["yaw_min"] = arguments["yaw_max"] = 0;
    place_obstacle(0, 2.5);
    check(query().at("retained_count") == 2,
          "Unaligned Mesh bounds unexpectedly reached the elevated obstacle");
    arguments["world_normals"] =
        Json::array({{{"x", 1}, {"y", 1}, {"z", 0}}, {{"x", 1}, {"y", 1}, {"z", 0}}});
    check(query().at("retained_count") == 1, "Mesh bounds did not follow the resolved slope frame");
    arguments.erase("world_normals");
    arguments["world_heights"] = {0., .3};
    settings.mask = 0;
    check(query().at("retained_count") == 2, "Disabled separation changed the candidate pool");
    settings.avoid_new_overlaps = true;
    result = query();
    check(result.at("retained_count") == 1 && result.at("new_overlap_count") == 1 &&
              result.at("arguments").at("placement_mask") == Json::array({true, false}),
          "Mesh bounds separation failed to keep the first overlapping candidate");
    const auto before = scene.document();
    const auto placed = authoring_command(scene, "mesh.scatter", result.at("arguments"));
    check(placed.at("placed_count") == 1 && scene.undo() && scene.document() == before &&
              scene.redo() && scene.undo() && scene.document() == before,
          "Mesh bounds result bypassed ordinary atomic authoring history");
    auto windy = scene.document();
    for (auto& row : windy["entities"])
        if (row.at("id") == source)
            row["components"]["forge.mesh_renderer"]["wind"] = {
                {"enabled", true}, {"direction_x", 1}, {"direction_z", 0}, {"amplitude", .2},
                {"frequency", .5}, {"wavelength", 8},  {"height", 1.6},    {"phase", 0}};
    scene.edit(windy);
    const auto wind_before = scene.document();
    bool wind_refused = false;
    try {
        task.start(lease, scene, arguments, settings);
    } catch (const AuthoringError& error) {
        wind_refused = error.code == "unsupported_template";
    }
    check(wind_refused && scene.document() == wind_before && scene.undo() &&
              scene.document() == before,
          "Wind-enabled template clearance was admitted or changed source intent");
    settings.padding = std::numeric_limits<double>::quiet_NaN();
    bool rejected = false;
    try {
        task.start(lease, scene, arguments, settings);
    } catch (const AuthoringError& error) {
        rejected = error.code == "invalid_arguments";
    }
    check(rejected && scene.document() == before,
          "Invalid Mesh bounds padding changed the Scene or entered native preparation");
}
void separate_candidates(const std::filesystem::path& base) {
    const auto root = std::filesystem::absolute(base / AssetId::generate().str());
    std::filesystem::create_directories(root);
    auto lease = std::make_shared<ProjectLease>(root);
    AssetCatalog(root).save(AssetCatalog::project_index(root));
    ProjectSettings(root).save(ProjectSettings::defaults("Separate plants"));
    EngineContext engine;
    Scene scene(engine.world());
    const auto source = authoring_command(scene, "entity.create", {{"recipe", "primitive.0"}})
                            .at("selected")
                            .get<std::string>();
    const auto before = scene.document();
    FoliageCollisionSettings settings;
    settings.mask = 0;
    settings.radius = .6;
    settings.offset = {300, -400, 500};
    detail::MeshScatterSettings scatter;
    scatter.count = 3;
    scatter.radius = .01;
    scatter.spacing = 0;
    auto arguments = detail::mesh_scatter_arguments(source, scatter, "Vertical plants");
    arguments["world_heights"] = {0., 1., 2.};
    FoliageCollisionPreparation task;
    const auto original = task.start(lease, scene, arguments, settings);
    auto changed = settings;
    changed.avoid_new_overlaps = true;
    bool refused = false;
    try {
        task.start(lease, scene, arguments, changed);
    } catch (const AuthoringError& error) {
        refused = error.code == "foliage_collision_busy";
    }
    check(refused && task.state().at("operation") == original.at("operation"),
          "A changed candidate filter replaced pending work");
    const auto unchanged = settled(task, scene, root).at("result");
    check(unchanged.at("retained_count") == 3 && unchanged.at("new_overlap_count") == 0,
          "Default clearance started filtering new plants");
    const auto started = task.start(lease, scene, arguments, changed);
    check(started.at("operation") != original.at("operation"),
          "The changed candidate filter reused an old ready result");
    const auto separated = settled(task, scene, root).at("result");
    check(separated.at("arguments").at("placement_mask") == Json::array({true, false, true}) &&
              separated.at("new_overlap_count") == 1 && separated.at("retained_count") == 2 &&
              separated.at("settings").at("avoid_new_overlaps") == true &&
              separated.at("arguments").at("world_heights") == arguments.at("world_heights"),
          "Candidate filtering lost 3D heights or let a rejected plant block later plants");
    check(task.start(lease, scene, arguments, changed).at("operation") == started.at("operation"),
          "An exact separated ready retry changed its operation");
    check(scene.document() == before, "Candidate preparation changed source intent");
    const auto applied = authoring_command(scene, "mesh.scatter", separated.at("arguments"));
    const auto after = scene.document();
    check(applied.at("created").size() == 2 && scene.undo() && scene.document() == before &&
              scene.redo() && scene.document() == after && scene.undo(),
          "Separated plants did not preserve one ordinary Scene Undo/Redo entry");

    scatter.count = 8;
    arguments = detail::mesh_scatter_arguments(source, scatter, "Masked plants");
    arguments["placement_mask"] = {false, true, true, true, true, true, true, true};
    changed.radius = 100;
    task.start(lease, scene, arguments, changed);
    const auto masked = settled(task, scene, root).at("result");
    check(masked.at("arguments").at("placement_mask") ==
                  Json::array({false, true, false, false, false, false, false, false}) &&
              masked.at("new_overlap_count") == 6,
          "An excluded first candidate blocked later plants or filtering resampled the pool");

    scatter.count = 2;
    scatter.radius = 2;
    const auto pair = detail::mesh_scatter_layout(scatter);
    const auto diameter = std::hypot(pair[0].offset[0] - pair[1].offset[0], 0.,
                                     pair[0].offset[1] - pair[1].offset[1]);
    arguments = detail::mesh_scatter_arguments(source, scatter, "Tangent plants");
    changed.offset = {0, 0, 0};
    for (const auto radius :
         {std::nextafter(diameter / 2, 0.), diameter / 2,
          std::nextafter(diameter / 2, std::numeric_limits<double>::infinity())}) {
        changed.radius = radius;
        task.start(lease, scene, arguments, changed);
        const auto result = settled(task, scene, root).at("result");
        const auto expected = radius > diameter / 2 ? 1 : 2;
        check(result.at("retained_count") == expected &&
                  result.at("new_overlap_count") == 2 - expected,
              "Touching spheres or one-ULP separation used the wrong boundary");
    }

    scatter.count = 64;
    scatter.radius = 3;
    changed.radius = .6;
    for (const auto seed : {7u, 31u, 59u, 101u}) {
        scatter.seed = seed;
        const auto points = detail::mesh_scatter_layout(scatter);
        arguments = detail::mesh_scatter_arguments(source, scatter, "Ordered candidate oracle");
        std::vector<double> heights(scatter.count);
        std::vector<bool> admitted(scatter.count);
        for (unsigned i = 0; i < scatter.count; ++i) {
            heights[i] = double(i % 4);
            admitted[i] = i % 3 != 0;
        }
        arguments["world_heights"] = heights;
        arguments["placement_mask"] = admitted;
        task.start(lease, scene, arguments, changed);
        const auto result = settled(task, scene, root).at("result");
        const auto selected = result.at("arguments").at("placement_mask").get<std::vector<bool>>();
        unsigned retained = 0, rejected = 0;
        for (unsigned i = 0; i < scatter.count; ++i) {
            bool blocked = false;
            for (unsigned j = 0; j < i && admitted[i]; ++j) {
                if (!selected[j])
                    continue;
                const auto x = static_cast<long double>(points[i].offset[0]) - points[j].offset[0];
                const auto y = static_cast<long double>(heights[i]) - heights[j];
                const auto z = static_cast<long double>(points[i].offset[1]) - points[j].offset[1];
                const auto threshold = static_cast<long double>(changed.radius) * 2;
                blocked |= x * x + y * y + z * z < threshold * threshold;
            }
            check(selected[i] == (admitted[i] && !blocked),
                  "Candidate subset disagrees with independent ordered squared-distance oracle");
            retained += selected[i];
            rejected += admitted[i] && blocked;
        }
        check(retained > 1 && rejected > 0 && result.at("retained_count") == retained &&
                  result.at("new_overlap_count") == rejected,
              "Candidate separation lost its count or failed to exercise both outcomes");
    }
}
void stroke_candidates(const std::filesystem::path& base) {
    const auto root = std::filesystem::absolute(base / AssetId::generate().str());
    std::filesystem::create_directories(root);
    auto lease = std::make_shared<ProjectLease>(root);
    AssetCatalog(root).save(AssetCatalog::project_index(root));
    ProjectSettings(root).save(ProjectSettings::defaults("Stroke clearance"));
    EngineContext engine;
    Scene scene(engine.world());
    const auto source = authoring_command(scene, "entity.create", {{"recipe", "primitive.0"}})
                            .at("selected")
                            .get<std::string>();
    detail::MeshScatterSettings scatter;
    scatter.count = 2;
    scatter.radius = .01;
    scatter.spacing = 0;
    auto dab = detail::mesh_scatter_arguments(source, scatter, "Stroke");
    dab.erase("entity");
    dab.erase("name");
    Json arguments{{"entity", source}, {"name", "Cleared stroke"}, {"dabs", Json::array()}};
    for (unsigned i = 0; i < 3; ++i) {
        auto value = dab;
        value["world_heights"] = {double(i), double(i)};
        value["world_normals"] = {{{"x", 0}, {"y", 1}, {"z", 0}}, {{"x", 0}, {"y", 1}, {"z", 0}}};
        arguments["dabs"].push_back(std::move(value));
    }
    const auto before = scene.document();
    const auto revision = scene.revision();
    FoliageCollisionSettings settings;
    settings.mask = 0;
    settings.radius = .6;
    FoliageCollisionPreparation task;
    task.start(lease, scene, arguments, settings);
    const auto unfiltered = settled(task, scene, root).at("result");
    check(unfiltered.at("authoring_operation") == "mesh.stroke" &&
              unfiltered.at("original_dab_count") == 3 &&
              unfiltered.at("retained_dab_indices") == Json::array({0, 1, 2}) &&
              unfiltered.at("candidate_count") == 6 && unfiltered.at("retained_count") == 6,
          "Default stroke clearance lost dabs or changed the generated pool");
    settings.avoid_new_overlaps = true;
    const auto started = task.start(lease, scene, arguments, settings);
    check(task.start(lease, scene, arguments, settings).at("operation") == started.at("operation"),
          "Exact pending stroke retry created another task");
    const auto cleared = settled(task, scene, root).at("result");
    const auto& resolved = cleared.at("arguments");
    check(cleared.at("candidate_count") == 6 && cleared.at("retained_count") == 2 &&
              cleared.at("new_overlap_count") == 4 &&
              cleared.at("retained_dab_indices") == Json::array({0, 2}) &&
              resolved.at("dabs").size() == 2,
          "Cross-dab separation did not omit the fully blocked middle dab");
    for (unsigned i = 0; i < 2; ++i) {
        const auto& returned = resolved.at("dabs")[i];
        check(returned.at("placement_mask") == Json::array({true, false}) &&
                  returned.at("world_heights") == arguments.at("dabs")[i * 2].at("world_heights") &&
                  returned.at("world_normals") == arguments.at("dabs")[i * 2].at("world_normals"),
              "Retained stroke dab lost its original height/normal/mask correspondence");
    }
    check(scene.document() == before && scene.revision() == revision,
          "Stroke preparation modified Scene/history");
    const auto placed =
        authoring_command(scene, cleared.at("authoring_operation").get<std::string>(), resolved);
    const auto after = scene.document();
    check(placed.at("groups").size() == 2 && placed.at("placed_count") == 2 && scene.undo() &&
              scene.document() == before && scene.redo() && scene.document() == after &&
              scene.undo() && scene.document() == before,
          "Cleared stroke did not compose one ordinary atomic Scene transaction");

    auto malformed = arguments;
    malformed["dabs"][1]["world_normals"][1]["y"] = 0;
    bool rejected = false;
    try {
        task.start(lease, scene, malformed, settings);
    } catch (const AuthoringError&) {
        rejected = true;
    }
    check(rejected && scene.document() == before,
          "A would-be-omitted dab bypassed normal validation");

    scatter.count = 32;
    scatter.radius = 1;
    dab = detail::mesh_scatter_arguments(source, scatter, "Maximum stroke");
    dab.erase("entity");
    dab.erase("name");
    arguments["dabs"] = Json::array();
    for (unsigned i = 0; i < 16; ++i) {
        auto value = dab;
        value["center"]["x"] = i * 10;
        arguments["dabs"].push_back(std::move(value));
    }
    task.start(lease, scene, arguments, settings);
    const auto maximum = settled(task, scene, root).at("result");
    check(maximum.at("original_dab_count") == 16 && maximum.at("candidate_count") == 512 &&
              maximum.at("retained_dab_indices").size() == 16 &&
              maximum.at("retained_count") >= 16 && scene.document() == before,
          "Maximum stroke lost its original bounded pool or surviving dabs");

    const auto obstacle = authoring_command(scene, "entity.create", {{"recipe", "primitive.0"}})
                              .at("selected")
                              .get<std::string>();
    for (const auto* type : {"forge.physics_body", "forge.box_collider"})
        authoring_command(scene, "component.add", {{"entity", obstacle}, {"component", type}});
    authoring_command(scene, "property.set",
                      {{"entity", obstacle},
                       {"component", "forge.physics_body"},
                       {"field", "motion"},
                       {"value", 0}});
    authoring_command(scene, "transform.position",
                      {{"entity", obstacle}, {"value", {{"x", 0}, {"y", .5}, {"z", 0}}}});
    const auto blocked_before = scene.document();
    scatter.count = 2;
    scatter.radius = .01;
    scatter.center = {0, 0, 0};
    dab = detail::mesh_scatter_arguments(source, scatter, "Blocked dabs");
    dab.erase("entity");
    dab.erase("name");
    arguments["dabs"] = {dab, dab, dab};
    arguments["dabs"][1]["center"]["x"] = 5;
    settings.mask = UINT32_MAX;
    settings.radius = .25;
    task.start(lease, scene, arguments, settings);
    const auto middle = settled(task, scene, root).at("result");
    check(middle.at("retained_dab_indices") == Json::array({1}) &&
              middle.at("candidate_count") == 6 && middle.at("retained_count") == 1 &&
              middle.at("new_overlap_count") == 1 &&
              middle.at("arguments").at("dabs")[0].at("center").at("x") == 5 &&
              scene.document() == blocked_before,
          "Existing colliders did not omit the first/last dabs before new-plant separation");
    arguments["dabs"][1]["center"]["x"] = 0;
    task.start(lease, scene, arguments, settings);
    const auto blocked = settled(task, scene, root);
    check(blocked.at("state") == "failed" && !blocked.contains("result") &&
              scene.document() == blocked_before,
          "An entirely blocked stroke published an empty group or partial result");
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
    FoliageCollisionSettings separated_settings;
    separated_settings.avoid_new_overlaps = true;
    task.start(lease, scene, subset, separated_settings);
    const auto combined = settled(task, scene, root).at("result");
    const auto selected = combined.at("arguments").at("placement_mask").get<std::vector<bool>>();
    unsigned separated_count = 0, overlapping_count = 0;
    for (unsigned i = 0; i < mask.size(); ++i) {
        const bool clear = mask[i] && std::abs(layout[i].offset[0]) > .75;
        bool overlapping = false;
        for (unsigned j = 0; j < i && clear; ++j) {
            if (!selected[j])
                continue;
            const auto x = static_cast<long double>(layout[i].offset[0]) - layout[j].offset[0];
            const auto z = static_cast<long double>(layout[i].offset[1]) - layout[j].offset[1];
            overlapping |= x * x + z * z < .25L;
        }
        check(selected[i] == (clear && !overlapping),
              "Existing native collider and new-candidate filters disagreed with geometry");
        separated_count += selected[i];
        overlapping_count += clear && overlapping;
    }
    check(combined.at("retained_count") == separated_count &&
              combined.at("new_overlap_count") == overlapping_count && scene.document() == after,
          "Composed clearance lost candidate counts or changed the source Scene");
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
        if (argc == 3 && std::string_view(argv[2]) == "--xy-clearance") {
            xy_clearance(std::filesystem::u8path(argv[1]));
            mesh_bounds_clearance(std::filesystem::u8path(argv[1]), true);
            return 0;
        }
        check(argc == 2 || ((argc == 3 || argc == 4) &&
                            std::string_view(argv[2]) == "--candidate-workload" &&
                            (argc == 3 || std::string_view(argv[3]) == "--separate")),
              "Need private test root and optional candidate workload");
        candidate_workload(std::filesystem::u8path(argv[1]), argc == 4);
        if (argc >= 3)
            return 0;
        candidate_workload(std::filesystem::u8path(argv[1]), true);
        template_geometry_admission();
        mesh_bounds_clearance(std::filesystem::u8path(argv[1]));
        separate_candidates(std::filesystem::u8path(argv[1]));
        stroke_candidates(std::filesystem::u8path(argv[1]));
        exercise(std::filesystem::u8path(argv[1]));
        resource_and_state(std::filesystem::u8path(argv[1]));
        std::cout << "Native clearance geometry, copied masks, retries/history/cancel passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
