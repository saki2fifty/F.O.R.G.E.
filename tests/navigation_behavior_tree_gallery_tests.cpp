#include "navigation_behavior_admission.hpp"
#include "navigation_behavior_authoring.hpp"
#include "runtime_package.hpp"
#include "state_graph_layout.hpp"
#include <forge/animation_components.hpp>
#include <forge/authoring.hpp>
#include <forge/game_session.hpp>
#include <forge/navigation_build.hpp>
#include <forge/primitive_catalog.hpp>
#include <forge/render_components.hpp>
#include <fstream>
#include <iostream>
#include <set>
#include <thread>
using namespace forge;
namespace {
constexpr const char* scene_path = "Scenes/139-behavior-tree.scene.json";
constexpr const char* graph_path = "Assets/Navigation/PatrolChaseTree.behavior.json";
constexpr const char* actor_name = "Tree agent - inspect Node and Tree result";
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
Json read(const std::filesystem::path& path) {
    Json value;
    std::ifstream(path) >> value;
    return value;
}
NavigationBehaviorSource tree(AssetId id) {
    auto value = NavigationBehaviorSource::create_tree(id);
    value.document["next_id"] = 9;
    value.document["nodes"] = Json::array(
        {{{"id", 1}, {"name", "Choose activity"}, {"kind", "selector"}, {"children", {2, 5}}},
         {{"id", 2},
          {"name", "Chase while near"},
          {"kind", "reactive_sequence"},
          {"children", {3, 4}}},
         {{"id", 3}, {"name", "Target within 3.5 m"}, {"kind", "target_near"}, {"distance", 3.5}},
         {{"id", 4}, {"name", "Follow target"}, {"kind", "follow_target"}},
         {{"id", 5}, {"name", "Patrol"}, {"kind", "sequence"}, {"children", {6, 7, 8}}},
         {{"id", 6}, {"name", "Pause before patrol"}, {"kind", "wait"}, {"seconds", .5}},
         {{"id", 7}, {"name", "Walk left"}, {"kind", "move_to_point"}, {"point", {-5, 0, 0}}},
         {{"id", 8}, {"name", "Walk right"}, {"kind", "move_to_point"}, {"point", {5, 0, 0}}}});
    const std::array<std::array<double, 2>, 8> positions{{{0, 240},
                                                          {300, 60},
                                                          {600, 0},
                                                          {600, 120},
                                                          {300, 360},
                                                          {600, 240},
                                                          {600, 360},
                                                          {600, 480}}};
    for (std::size_t i = 0; i < positions.size(); ++i)
        asset_detail::set_state_graph_position(value.document, std::uint32_t(i + 1), positions[i],
                                               navigation_behavior_layout_format);
    return value;
}
AssetId publish_tree(const std::shared_ptr<ProjectLease>& lease) {
    auto doc = std::filesystem::exists(lease->root() / graph_path)
                   ? std::make_unique<NavigationBehaviorDocument>(lease, graph_path)
                   : NavigationBehaviorDocument::create(lease, graph_path, tree);
    const auto id = doc->source().asset();
    try {
        const auto catalog = AssetCatalog::open_project(lease->root());
        const auto& record = catalog.records().at(id);
        const auto raw =
            asset_detail::read_bytes(lease->root() / graph_path, navigation_behavior_byte_limit);
        check(record.metadata.at("forge.import").at("source_digest") ==
                  asset_detail::content_digest(raw),
              "Tree source needs publication");
        check(record.metadata.at("forge.navigation_behavior").at("sha256") ==
                  asset_detail::content_digest(doc->source().cook()),
              "Tree cooked bytes need publication");
        DerivedDataCache cache(lease->root());
        const auto artifact = cache.load_selected(
            record.metadata.at("forge.import").at("key").get<std::string>(), [](const auto&) {});
        (void)admit_navigation_behavior_artifact(record, artifact);
        return id;
    } catch (
        const std::exception&) { /* Prepare the existing logical asset through its normal owner. */
    }
    AssetImportService imports(lease, navigation_behavior_import_registry(),
                               {"windows", "d3d12", "desktop"});
    imports.submit(
        imports.prepare(doc->locator(), {}, id),
        [](auto& candidate, const auto& plan, const auto&) {
            prepare_navigation_behavior_publication(candidate, plan);
        },
        [](const auto&, const auto&) {});
    check(imports.wait_idle(std::chrono::seconds(15)), "Tree publication timed out");
    const auto result = imports.poll();
    check(result.size() == 1 && result[0].published,
          result.empty() ? "Missing tree publication" : result[0].diagnostic);
    return id;
}
flecs::entity create(Scene& scene, const char* name, Double3 at = {},
                     unsigned primitive = no_primitive) {
    const auto id = authoring_command(scene, "entity.create", {{"name", name}})
                        .at("selected")
                        .get<std::string>();
    const auto entity = scene.entity(id);
    entity.set<LocalTranslation>({at[0], at[1], at[2]}).set<Primitive>({primitive});
    return entity;
}
void prepare(const std::filesystem::path& root, const std::filesystem::path& worker) {
    const auto lease = std::make_shared<ProjectLease>(root);
    const auto graph = publish_tree(lease);
    AssetId clip;
    const auto initial_catalog = AssetCatalog::open_project(root);
    for (const auto& [id, record] : initial_catalog.records())
        if (record.type == TransformAnimationAsset::type &&
            record.source == "Assets/Navigation/ChaseTarget.animation.json")
            clip = id;
    check(bool(clip), "Existing original target animation is missing");
    EngineContext engine;
    Scene scene(engine.world());
    if (std::filesystem::exists(root / scene_path))
        scene.load(root / scene_path);
    else {
        create(scene, "Walkable XY floor").set<NavigationRegion2D>({true, false, 20, 14});
        create(scene, "Floor visual", {0, 0, 3}, 0)
            .set<LocalScale>({20, 14, .1f})
            .set<Tint>({.045f, .12f, .16f});
        const auto actor = create(scene, actor_name, {-6, 0, -1});
        actor.set<NavigationAgent>({{}, true, false, 3, .08f});
        const auto body = create(scene, "Cyan agent body", {}, 0);
        body.set<LocalScale>({.8f, .8f, .8f}).set<Tint>({.06f, .82f, 1});
        scene.reparent_entity(body.get<PersistentEntityId>().value.str(),
                              actor.get<PersistentEntityId>().value.str(), ReparentMode::KeepLocal);
        const auto target = create(scene, "Animated target - shared original clip", {2, 0, 0}, 1);
        target.set<LocalScale>({.7f, .7f, .7f}).set<Tint>({1, .55f, .08f});
        TransformAnimator animator;
        animator.clip = {clip};
        target.set<TransformAnimator>(animator);
        actor.set<NavigationBehavior>(
            {{graph}, scene.reference(target.get<PersistentEntityId>().value.str()), {}, true});
        for (const float x : {-5.f, 5.f})
            create(scene, x < 0 ? "Left patrol marker" : "Right patrol marker", {x, 0, -.5}, 1)
                .set<LocalScale>({.25f, .25f, .25f})
                .set<Tint>({.2f, .8f, .55f});
        Camera camera;
        camera.projection = unsigned(CameraProjection::Orthographic);
        camera.orthographic_height = 17;
        camera.aspect = 16.0 / 9;
        create(scene, "Overview Camera", {0, 0, -25}).set<Camera>(camera);
        create(scene, "Sunlight").set<Light>({});
    }
    NavigationSettings settings;
    settings.plane = NavigationPlane::PlanarXY;
    settings.cell_size = .1f;
    const auto catalog = AssetCatalog::open_project(root);
    const auto profiles = navigation_profiles(catalog, scene.asset_id());
    std::optional<AssetRecord> current;
    const auto document = scene.effective_document();
    if (const auto at = profiles.find("default"); at != profiles.end()) {
        const auto& record = catalog.records().at(at->second);
        if (record.metadata.at("settings") == Json(settings) &&
            Json(navigation_geometry_digest(root, document, record.metadata)) ==
                record.metadata.at("geometry_sha256") &&
            catalog.resolve(AssetRef<NavMeshAsset>{record.id}).state == AssetState::Available)
            current = record;
    }
    const auto nav =
        current ? current->id
                : prepare_navigation(root, document, settings, worker).publish(document).id;
    std::vector<flecs::entity> actors;
    engine.world().world().each(
        [&](flecs::entity e, const NavigationAgent&) { actors.push_back(e); });
    check(actors.size() == 1, "Tree Gallery must contain one movement controller");
    auto agent = actors[0].get<NavigationAgent>();
    agent.navmesh = {nav};
    actors[0].set<NavigationAgent>(agent);
    auto behavior = actors[0].get<NavigationBehavior>();
    behavior.graph = {graph};
    actors[0].set<NavigationBehavior>(behavior);
    scene.save(root / scene_path);
    const std::array roots{scene.asset_id()};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
    std::cout << "Gallery139 behavior tree prepared through normal publication/navigation owners\n";
}
void exercise(const std::filesystem::path& root, const std::filesystem::path& path) {
    const auto saved = read(root / path);
    GameSessionConfig config;
    config.content_root = root;
    struct Preparation final : GameScenePreparation {
        GamePreparationProgress poll(RuntimeWorld& world) override {
            return {world.prepare_scene_resources(), "scene resources"};
        }
        void activate() noexcept override {}
    };
    config.preparation = [](std::uint64_t) { return std::make_unique<Preparation>(); };
    GameSession session(config);
    const auto candidate = session.prepare(saved);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!session.poll_preparation(candidate).ready) {
        check(std::chrono::steady_clock::now() < deadline, "Tree Gallery readiness timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    session.activate(candidate, {}, false);
    auto& world = session.active();
    flecs::entity actor;
    for (const auto& row : saved.at("entities"))
        if (row.at("name") == actor_name)
            actor = world.scene.entity(row.at("id"));
    check(bool(actor), "Tree Gallery actor missing");
    std::set<std::uint32_t> leaves;
    double minx = 100, maxx = -100;
    bool waited = false, moved = false;
    for (unsigned tick = 0; tick < 1200; ++tick) {
        session.step();
        check(actor.has<NavigationBehaviorCursor>(), "Prepared tree actor has no runtime cursor");
        const auto cursor = actor.get<NavigationBehaviorCursor>();
        leaves.insert(cursor.state);
        const auto p = actor.get<LocalTranslation>();
        minx = std::min(minx, p.x);
        maxx = std::max(maxx, p.x);
        check(p.z == -1 && std::abs(p.x) < 9.6 && std::abs(p.y) < 6.6,
              "Tree left its navigation floor or visual depth");
        waited |= !actor.get<NavigationAgent>().has_destination;
        moved |= actor.get<NavigationAgent>().has_destination;
        const auto debug = world.engine.services().navigation()->agent_state(
            world.scene.reference(actor.get<PersistentEntityId>().value.str()));
        check(debug && debug->phase != NavigationAgentPhase::Failed,
              "Tree Gallery navigation failed");
    }
    check(leaves.contains(4) && leaves.contains(6) && leaves.contains(7) && leaves.contains(8) &&
              waited && moved && maxx - minx > 4,
          "Tree Gallery did not exercise chase, timed wait and both patrol actions");
    check(read(root / path) == saved, "Tree gameplay changed authored source");
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string_view(argv[1]) == "--prepare") {
            prepare(std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]));
            return 0;
        }
        check(argc == 3, "Need Gallery and tree scratch root");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto root = scratch / "Gallery";
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        exercise(root, scene_path);
        const auto saved = read(root / scene_path);
        const std::array roots{saved.at("asset_id").get<AssetId>()};
        const auto output = scratch / "Export";
        package_runtime_content(root, output, roots, {"windows", "d3d12"});
        const auto moved = scratch / "Relocated";
        std::filesystem::rename(output, moved);
        std::filesystem::rename(root, scratch / "SourcesOffline");
        const auto catalog = open_runtime_content(moved, {"windows", "d3d12"});
        exercise(moved, catalog.records().at(roots[0]).source);
        std::cout
            << "Tree Gallery actual chase/patrol/wait and source-hidden relocated export PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
