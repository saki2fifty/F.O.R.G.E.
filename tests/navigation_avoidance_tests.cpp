#include "navigation_asset.hpp"
#include "runtime_package.hpp"
#include <cmath>
#include <forge/authoring.hpp>
#include <forge/navigation.hpp>
#include <forge/navigation_build.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
using namespace forge;
namespace {
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void reject(F action) {
    bool failed = false;
    try {
        action();
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, "Expected invalid avoidance input to be rejected");
}
Json initial_scene() {
    auto rows = Json::array();
    for (const auto& name : {"floor", "a", "b"})
        rows.push_back({{"id", name}, {"name", name}, {"components", Json::object()}});
    rows[0]["components"] = {{"forge.position", {{"x", 0}, {"y", -.5}, {"z", 0}}},
                             {"forge.scale", {{"x", 20}, {"y", 1}, {"z", 20}}},
                             {"forge.primitive", {{"kind", 0}}},
                             {"forge.navigation_surface", {{"enabled", true}}}};
    rows[1]["components"]["forge.position"] = {{"x", -4}, {"y", .1}, {"z", 0}};
    rows[2]["components"]["forge.position"] = {{"x", 4}, {"y", .1}, {"z", 0}};
    return {{"version", 1}, {"entities", rows}};
}
struct Running {
    EngineContext engine;
    Scene scene;
    RuntimeSimulation simulation;
    std::shared_ptr<NavigationRuntime> navigation;
    std::map<std::string, std::string> named;
    Running(const std::filesystem::path& root, const Json& snapshot)
        : engine(WorldRole::Runtime, false, {navigation_module(root)}), scene(engine.world()),
          simulation(engine.world(), scene),
          navigation(std::static_pointer_cast<NavigationRuntime>(engine.services().navigation())) {
        scene.restore_snapshot(snapshot);
        for (const auto& row : snapshot.at("entities"))
            named[row.at("name").get<std::string>()] = row.at("id").get<std::string>();
    }
    NavigationAgentObservation state(const char* name) {
        return navigation
            ->agent_state(engine.world().reference(scene.entity(named.at(name)).id()).value())
            .value();
    }
    Double3 position(const char* name) {
        engine.world().evaluate_world_transforms();
        return scene.entity(named.at(name)).get<WorldTransform>().affine.point({0, 0, 0});
    }
    void tick() { simulation.tick(1.f / 60); }
};
double horizontal(Double3 a, Double3 b) { return std::hypot(a[0] - b[0], a[2] - b[2]); }
double passing(const std::filesystem::path& root, const Json& snapshot, bool avoid, bool idle,
               bool crossing = false, bool other_participates = true) {
    Running run(root, snapshot);
    auto a = run.scene.entity("a"), b = run.scene.entity("b");
    if (avoid) {
        a.set<NavigationAvoidance>({});
        b.set<NavigationAvoidance>({other_participates, 2});
    }
    if (crossing) {
        auto config = b.get<NavigationAgent>();
        config.destination_x = 0;
        config.destination_z = 4;
        b.set<NavigationAgent>(config).set<LocalTranslation>({0, .1, -4});
    }
    if (idle) {
        auto config = b.get<NavigationAgent>();
        config.has_destination = false;
        b.set<NavigationAgent>(config).set<LocalTranslation>({0, .1, 0});
    }
    auto minimum = 100.;
    bool deviated = false;
    for (unsigned tick = 1; tick <= 600; ++tick) {
        run.tick();
        const auto aa = run.state("a"), bb = run.state("b");
        check(aa.sampled && aa.tick == tick && aa.status == NavStatus::Success,
              "Avoidance lost fixed-tick pose/route sample");
        check(horizontal(aa.velocity, {}) <= 2.001, "Avoidance exceeded configured speed");
        check(std::isfinite(aa.position[0]) && std::abs(aa.position[0]) < 10 &&
                  std::isfinite(aa.position[2]) && std::abs(aa.position[2]) < 10,
              "Avoidance left finite admitted surface bounds");
        minimum = std::min(minimum, horizontal(run.position("a"), run.position("b")));
        deviated |= std::abs(aa.position[2]) > .2;
        if (idle)
            check(bb.phase == NavigationAgentPhase::Idle &&
                      horizontal(run.position("b"), {}) < .001,
                  "Idle participant moved or acquired a movement sample");
        else
            check(bb.sampled && bb.tick == tick && bb.status == NavStatus::Success,
                  "Second passing participant lost its completed sample");
    }
    check(run.state("a").phase == NavigationAgentPhase::Arrived &&
              (idle || run.state("b").phase == NavigationAgentPhase::Arrived),
          "Avoiding agents did not arrive");
    if (avoid && other_participates)
        check(deviated && minimum > .65, "Participating agents failed to steer apart");
    else
        check(minimum < .1 && !deviated, "Legacy movement unexpectedly used avoidance");
    std::cout << (avoid ? "avoid" : "legacy")
              << (crossing ? " crossing"
                  : idle   ? " idle"
                           : " passing")
              << " minimum separation=" << minimum << '\n';
    return minimum;
}
void lifecycle(const std::filesystem::path& root, const Json& snapshot) {
    Running run(root, snapshot);
    auto a = run.scene.entity("a");
    a.set<NavigationAvoidance>({});
    auto b = run.scene.entity("b");
    auto disabled = b.get<NavigationAgent>();
    disabled.enabled = false;
    b.set<NavigationAgent>(disabled);
    check(run.state("a").phase == NavigationAgentPhase::Pending,
          "Avoidance advanced before a tick");
    const auto reference = run.engine.world().reference(a.id()).value();
    check(run.navigation->tune_avoidance(reference, "horizon", .75) == .75 &&
              run.state("a").phase == NavigationAgentPhase::Pending,
          "Live avoidance edit advanced navigation");
    reject([&] { run.navigation->tune_avoidance(reference, "horizon", 0); });
    reject([&] { run.navigation->tune_avoidance(reference, "speed", 2); });
    ecs_readonly_begin(run.engine.world().world().c_ptr(), false);
    reject([&] { run.navigation->tune_avoidance(reference, "horizon", 1); });
    ecs_readonly_end(run.engine.world().world().c_ptr());
    check(a.get<NavigationAvoidance>().horizon == .75f,
          "Readonly tuning queued a deferred value mutation");
    auto foreign = reference;
    foreign.scene = AssetId::generate();
    reject([&] { run.navigation->tune_avoidance(foreign, "horizon", 1); });
    check(run.engine.services().profiles().empty(), "Disabled avoidance profiling collected data");
    run.engine.services().profiling(true);
    run.tick();
#ifndef FORGE_DISABLE_PROFILING
    unsigned steering_samples = 0;
    for (const auto& sample : run.engine.services().profiles())
        if (sample.at("name") == "NavAvoidance") {
            ++steering_samples;
            check(sample.at("category") == "navigation" && sample.at("tick") == 1 &&
                      sample.at("seconds").get<double>() >= 0,
                  "Invalid avoidance profile record");
        }
    check(steering_samples == 1, "Missing avoidance steering profile");
#endif
    run.engine.services().profiling(false);
    const auto captured = run.navigation->checkpoint();
    const auto observed = run.state("a");
    for (unsigned i = 0; i != 20; ++i) {
        (void)run.navigation->find_path(a.get<NavigationAgent>().navmesh, {-4, .1, 0}, {4, .1, 0});
        check(run.state("a").position == observed.position && run.state("a").tick == observed.tick,
              "Readonly path/observation advanced avoidance");
    }
    check(run.navigation->checkpoint() == captured, "Readonly queries changed avoidance recovery");
    Running resumed(root, run.scene.snapshot());
    resumed.scene.entity("a").set<NavigationAvoidance>({true, 3});
    resumed.navigation->restore(captured);
    check(resumed.scene.entity("a").get<NavigationAvoidance>().horizon == .75f &&
              resumed.position("a") == run.position("a"),
          "Avoidance semantic recovery failed");
    auto legacy = captured;
    legacy["version"] = 1;
    reject([&] { resumed.navigation->restore(legacy); });
    auto bad = captured;
    for (auto& row : bad["agents"])
        if (!row["avoidance"].is_null())
            row["avoidance"]["owned"] = false;
    reject([&] { resumed.navigation->restore(bad); });
    auto paused = a.get<NavigationAgent>();
    paused.speed = 0;
    a.set<NavigationAgent>(paused);
    const auto before_pause = run.position("a");
    run.tick();
    check(run.state("a").phase == NavigationAgentPhase::Paused && run.position("a") == before_pause,
          "Paused avoidance moved");
    paused.speed = 2;
    a.set<NavigationAgent>(paused).set<NavigationAvoidance>({true, 0});
    run.tick();
    check(run.state("a").phase == NavigationAgentPhase::Failed &&
              run.state("a").status == NavStatus::Invalid && run.position("a") == before_pause,
          "Invalid live avoidance configuration moved");
    a.set<NavigationAvoidance>({});
    run.tick();
    run.scene.entity("floor").set<LocalTranslation>({1, -.5, 0});
    const auto before_stale = run.position("a");
    run.tick();
    check(run.state("a").status == NavStatus::Stale && run.position("a") == before_stale,
          "Avoidance followed stale geometry");
    a.destruct();
    run.tick();
    check(!run.navigation->agent_state(reference), "Deleted participant retained observation");
    reject([&] { run.navigation->tune_avoidance(reference, "horizon", 1); });
}
void spatial_and_prefab(const std::filesystem::path& root, const Json& snapshot) {
    Running run(root, snapshot);
    auto a = run.scene.entity("a");
    a.set<NavigationAvoidance>({});
    auto b = run.scene.entity("b");
    auto disabled = b.get<NavigationAgent>();
    disabled.enabled = false;
    b.set<NavigationAgent>(disabled);
    const auto parent_id =
        authoring_command(run.scene, "entity.create", {{"name", "Static parent"}})
            .at("selected")
            .get<std::string>();
    auto parent = run.scene.entity(parent_id);
    parent.set<LocalTranslation>({2, 0, 0}).set<LocalScale>({2, 1, 1});
    a.set<SpatialBinding>(
         {SpatialMode::Explicit, run.engine.world().reference(parent.id()).value()})
        .set<LocalTranslation>({-3, .1, 0});
    const auto rotation_owned = a.owns<LocalRotation>(), scale_owned = a.owns<LocalScale>();
    run.tick();
    check(run.state("a").status == NavStatus::Success && run.position("a")[0] > -4 &&
              a.get<LocalTranslation>().x > -3 && a.owns<LocalRotation>() == rotation_owned &&
              a.owns<LocalScale>() == scale_owned,
          "Avoidance failed parent inverse or materialized unrelated channels");
    parent.set<NavigationAgent>({a.get<NavigationAgent>().navmesh, true, true, 1, .1f, 5, .1, 0});
    const auto local = a.get<LocalTranslation>();
    run.tick();
    check(run.state("a").status == NavStatus::Invalid && a.get<LocalTranslation>() == local,
          "Avoidance admitted a second moving spatial authority");
    a.set<SpatialBinding>({SpatialMode::World, {}});
    run.tick();
    check(run.state("a").status == NavStatus::Success,
          "World binding retained structural moving-parent rejection");

    EngineContext author;
    Scene authored(author.world());
    authored.restore_snapshot(snapshot);
    auto base = authored.entity("a");
    base.set<NavigationAvoidance>({true, 1.5f});
    const auto prefab = create_prefab_source(authored, "a");
    authored.set_prefab_sources({{prefab.asset(), prefab.source}});
    const auto instance = instantiate_prefab(authored, prefab.asset());
    check(authored.entity(instance).get<NavigationAvoidance>().horizon == 1.5f &&
              !authored.entity(instance).owns<NavigationAvoidance>(),
          "Prefab did not inherit avoidance");
    authoring_command(authored, "property.set",
                      {{"entity", instance},
                       {"component", "forge.navigation_avoidance"},
                       {"field", "horizon"},
                       {"value", 1.25}});
    Running prefab_run(root, authored.snapshot());
    auto holder = prefab_run.scene.entity(instance);
    auto config = holder.get<NavigationAvoidance>();
    config.horizon = .5f;
    config.enabled = false;
    holder.set<NavigationAvoidance>(config);
    const auto checkpoint = prefab_run.navigation->checkpoint();
    Running recovered(root, prefab_run.scene.snapshot());
    check(recovered.scene.entity(instance).get<NavigationAvoidance>() ==
              NavigationAvoidance{true, .5f},
          "Fixture did not exercise authored property mask over a gameplay change");
    recovered.navigation->restore(checkpoint);
    check(recovered.scene.entity(instance).get<NavigationAvoidance>() == config,
          "Avoidance recovery lost gameplay value hidden by authored override intent");
    NavigationRuntime author_access(author.world(), root);
    author_access.bind(&authored);
    reject([&] {
        author_access.tune_avoidance(author.world().reference(base.id()).value(), "horizon", 1);
    });
}
void slope(const std::filesystem::path& root, const Json& snapshot,
           const std::filesystem::path& worker) {
    EngineContext author;
    Scene source(author.world());
    source.restore_snapshot(snapshot);
    source.entity("floor").set<LocalRotation>({0, 0, .258819045f, .965925826f});
    const auto record = prepare_navigation(root, source.effective_document(), {}, worker)
                            .publish(source.effective_document());
    auto admitted = navigation_detail::load(root, record);
    navigation_detail::Query query(admitted.mesh);
    const auto start = query.project({-4, -2, 0}), end = query.project({4, 2, 0});
    check(start.status == NavStatus::Success && end.status == NavStatus::Success,
          "Slope fixture lacks admitted endpoints");
    const auto p = start.points.front(), d = end.points.front();
    source.entity("a")
        .set<LocalTranslation>({p[0], p[1], p[2]})
        .set<NavigationAgent>({{record.id}, true, true, 2, .1f, d[0], d[1], d[2]})
        .set<NavigationAvoidance>({});
    auto b = source.entity("b").get<NavigationAgent>();
    b.enabled = false;
    source.entity("b").set<NavigationAgent>(b);
    Running run(root, source.snapshot());
    for (unsigned tick = 0; tick != 400; ++tick) {
        run.tick();
        const auto state = run.state("a");
        check(state.sampled && state.status == NavStatus::Success &&
                  std::hypot(state.velocity[0], state.velocity[1], state.velocity[2]) <= 2.002,
              "Slope steering exceeded full world-distance speed budget");
    }
    check(run.state("a").phase == NavigationAgentPhase::Arrived, "Slope steering did not arrive");
}
void isolation(const std::filesystem::path& root, const Json& snapshot, const AssetRecord& original,
               const std::filesystem::path& worker) {
    const auto split_root = std::filesystem::path(root.string() + "-asset-isolation");
    std::filesystem::copy(root, split_root, std::filesystem::copy_options::recursive);
    // Two independently baked assets for the same source scene, with both admitted.
    // Start the second bake from an empty catalog, then restore the first record.
    AssetCatalog(split_root).save(AssetCatalog::project_index(split_root));
    EngineContext author;
    Scene source(author.world());
    source.restore_snapshot(snapshot);
    const auto second = prepare_navigation(split_root, source.effective_document(), {}, worker)
                            .publish(source.effective_document());
    auto catalog = AssetCatalog::open_project(split_root);
    catalog.add(original);
    catalog.save(AssetCatalog::project_index(split_root));
    check(original.id != second.id, "Isolation fixture reused one selected asset");

    Running split(split_root, snapshot), baseline(split_root, snapshot);
    for (auto* run : {&split, &baseline}) {
        run->scene.entity("a").set<NavigationAvoidance>({});
        auto other = run->scene.entity("b").get<NavigationAgent>();
        other.navmesh = {second.id};
        run->scene.entity("b").set<NavigationAgent>(other);
    }
    split.scene.entity("b").set<NavigationAvoidance>({});
    for (unsigned tick = 0; tick != 360; ++tick) {
        split.tick();
        baseline.tick();
        check(split.state("a").status == NavStatus::Success &&
                  split.state("b").status == NavStatus::Success &&
                  split.position("a") == baseline.position("a"),
              "A valid participant on another selected asset influenced steering");
    }
    check(split.state("a").phase == NavigationAgentPhase::Arrived &&
              split.state("b").phase == NavigationAgentPhase::Arrived,
          "Different-asset participants did not finish their valid routes");

    const auto stacked_root = std::filesystem::path(root.string() + "-height-isolation");
    std::filesystem::create_directories(stacked_root);
    const auto upper = authoring_command(source, "entity.create", {{"name", "Upper floor"}})
                           .at("selected")
                           .get<std::string>();
    source.entity(upper)
        .set<LocalTranslation>({0, 3.5, 0})
        .set<LocalScale>({20, 1, 20})
        .set<NavigationSurface>({true});
    const auto stacked = prepare_navigation(stacked_root, source.effective_document(), {}, worker)
                             .publish(source.effective_document());
    source.entity("a").set<NavigationAgent>({{stacked.id}, true, true, 2, .1f, 4, .1, 0});
    source.entity("b")
        .set<LocalTranslation>({0, 4.1, 0})
        .set<NavigationAgent>({{stacked.id}, true, false, 2, .1f, 0, 4.1, 0});
    Running high(stacked_root, source.snapshot()), low(stacked_root, source.snapshot());
    high.scene.entity("a").set<NavigationAvoidance>({});
    high.scene.entity("b").set<NavigationAvoidance>({});
    low.scene.entity("a").set<NavigationAvoidance>({});
    check(high.navigation->project_point({stacked.id}, {0, 4.1, 0}).status == NavStatus::Success,
          "Height-isolation participant was outside its selected mesh");
    for (unsigned tick = 0; tick != 360; ++tick) {
        high.tick();
        low.tick();
        check(high.state("a").status == NavStatus::Success &&
                  high.state("b").phase == NavigationAgentPhase::Idle &&
                  high.position("a") == low.position("a"),
              "Vertically separated valid participant influenced steering");
    }
    check(high.state("a").phase == NavigationAgentPhase::Arrived,
          "Height-isolated route did not arrive");
}
void authoring(const std::filesystem::path& root, const Json& snapshot) {
    Running run(root, snapshot);
    const auto id = run.scene.entity("a").get<PersistentEntityId>().value.str();
    authoring_command(run.scene, "component.add",
                      {{"entity", id}, {"component", "forge.navigation_avoidance"}});
    check(run.scene.entity("a").get<NavigationAvoidance>() == NavigationAvoidance{},
          "Avoidance defaults missing");
    authoring_command(run.scene, "property.set",
                      {{"entity", id},
                       {"component", "forge.navigation_avoidance"},
                       {"field", "horizon"},
                       {"value", 1.25}});
    const auto changed = run.scene.snapshot();
    check(run.scene.undo() && run.scene.redo() && run.scene.snapshot() == changed,
          "Avoidance history lost reflected value");
    reject([&] {
        authoring_command(run.scene, "property.set",
                          {{"entity", id},
                           {"component", "forge.navigation_avoidance"},
                           {"field", "horizon"},
                           {"value", 0}});
    });
    check(run.scene.snapshot() == changed, "Rejected avoidance edit changed authored state");
    Running reopened(root, changed);
    check(reopened.scene.entity("a").get<NavigationAvoidance>().horizon == 1.25f,
          "Avoidance reopen lost authored value");
    Running legacy(root, snapshot);
    legacy.tick();
    auto checkpoint = legacy.navigation->checkpoint();
    checkpoint["version"] = 1;
    for (auto& row : checkpoint["agents"])
        row.erase("avoidance");
    Running legacy_resume(root, legacy.scene.snapshot());
    legacy_resume.navigation->restore(checkpoint);
}
Json read(const std::filesystem::path& path) {
    std::ifstream stream(path);
    Json document;
    stream >> document;
    return document;
}
void gallery_exercise(const std::filesystem::path& root, const Json& source,
                      const std::filesystem::path& path) {
    const auto catalog_before = AssetCatalog::open_project(root).document();
    Running run(root, source);
    const char* names[]{"Passing cube - select Live gameplay",
                        "Passing sphere - select Live gameplay", "Moving cube around parked agent",
                        "Parked sphere - idle participant"};
    auto nearest = 100.;
    bool went_around = false;
    for (unsigned tick = 1; tick <= 750; ++tick) {
        run.tick();
        for (unsigned i = 0; i != 3; ++i) {
            const auto state = run.state(names[i]);
            check(state.sampled && state.tick == tick && state.status == NavStatus::Success,
                  "Gallery52 lost valid avoidance progress");
        }
        nearest = std::min({nearest, horizontal(run.position(names[0]), run.position(names[1])),
                            horizontal(run.position(names[2]), run.position(names[3]))});
        went_around |= std::abs(run.position(names[2])[2] - 3) > .2;
        check(run.state(names[3]).phase == NavigationAgentPhase::Idle &&
                  horizontal(run.position(names[3]), {0, .1, 3}) < .001,
              "Gallery52 parked participant moved");
        if (tick == 1 || tick == 375 || tick == 750) {
            const auto render = extract_render_scene(run.simulation.presentation(.5));
            check(render.meshes.size() == 5 && render.cameras.size() == 1 &&
                      render.lights.size() == 1,
                  "Gallery52 did not admit its floor, four visuals, camera and light");
        }
    }
    for (unsigned i = 0; i != 3; ++i)
        check(run.state(names[i]).phase == NavigationAgentPhase::Arrived &&
                  run.state(names[i]).velocity == Double3{},
              "Gallery52 agent did not arrive and stop");
    check(nearest > .65 && went_around, "Gallery52 failed passing or parked-participant steering");
    check(read(root / path) == source &&
              AssetCatalog::open_project(root).document() == catalog_before,
          "Gallery52 runtime rewrote saved content/catalog");
    std::cout << "Gallery52 minimum separation=" << nearest << '\n';
}
void gallery(const std::filesystem::path& original, const std::filesystem::path& scratch) {
    const auto root = std::filesystem::absolute(scratch) / AssetId::generate().str();
    std::filesystem::create_directories(root);
    std::filesystem::copy(original, root, std::filesystem::copy_options::recursive);
    const auto path = std::filesystem::path("Scenes/52-navigation-avoidance.scene.json");
    const auto source = read(root / path);
    gallery_exercise(root, source, path);
    const auto game = root.string() + "-game", moved = game + "-relocated";
    const std::array roots{source.at("asset_id").get<AssetId>()};
    (void)package_runtime_content(root, game, roots, {"windows", "d3d12"});
    std::filesystem::rename(game, moved);
    std::filesystem::rename(root, root.string() + "-source-offline");
    const auto catalog = open_runtime_content(moved, {"windows", "d3d12"});
    check(catalog.records().size() == 2, "Gallery52 export retained unrelated project records");
    const auto exported = catalog.resolve(AssetRef<SceneAsset>{roots.front()});
    check(exported.state == AssetState::Available, "Relocated Gallery52 scene unavailable");
    gallery_exercise(moved, read(std::filesystem::path(moved) / exported.record->source),
                     exported.record->source);
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string_view(argv[1]) == "--gallery") {
            gallery(argv[2], argv[3]);
            return 0;
        }
        check(argc == 3 || (argc == 4 && std::string_view(argv[3]) == "--prepare"),
              "Need scratch root and navigation worker");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        EngineContext author;
        Scene scene(author.world());
        scene.restore_snapshot(initial_scene());
        auto baked = prepare_navigation(root, scene.effective_document(), {},
                                        std::filesystem::absolute(argv[2]));
        const auto record = baked.publish(scene.effective_document());
        scene.entity("a").set<NavigationAgent>({{record.id}, true, true, 2, .1f, 4, .1, 0});
        scene.entity("b").set<NavigationAgent>({{record.id}, true, true, 2, .1f, -4, .1, 0});
        const auto snapshot = scene.snapshot();
        if (argc == 4) {
            scene.entity("a").set<NavigationAvoidance>({});
            scene.entity("b").set<NavigationAvoidance>({});
            std::ofstream(root / "scene.json") << scene.snapshot().dump();
            std::ofstream(root / "forge.project.json") << Json{
                {"version", 2},
                {"name", "Avoidance process"},
                {"startup_scene", nullptr},
                {"input", {{"version", 1}, {"actions", Json::array()}}},
                {"modules", Json::array()}}.dump();
            std::cout << root.string() << '\n';
            return 0;
        }
        passing(root, snapshot, false, false);
        passing(root, snapshot, true, false);
        passing(root, snapshot, true, true);
        passing(root, snapshot, true, false, true);
        passing(root, snapshot, true, false, false, false);
        lifecycle(root, snapshot);
        authoring(root, snapshot);
        spatial_and_prefab(root, snapshot);
        isolation(root, snapshot, record, std::filesystem::absolute(argv[2]));
        auto admitted = navigation_detail::load(root, record);
        navigation_detail::Query query(admitted.mesh);
        std::vector<navigation_detail::Query::Neighbour> too_many(64, {{0, .1, 0}, {}});
        check(query.avoid_velocity({-4, .1, 0}, {2, 0, 0}, {}, 2, .4f, 2, too_many).status ==
                  NavStatus::Limit,
              "Upstream silent neighbour truncation was not rejected");
        check(query.avoid_velocity({-4, .1, 0}, {2, 0, 0}, {}, 2, .4f,
                                   std::numeric_limits<float>::quiet_NaN(), {})
                      .status == NavStatus::Invalid,
              "Invalid steering horizon reached upstream sampling");
        slope(root, snapshot, std::filesystem::absolute(argv[2]));
        std::cout << "Navigation avoidance tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
