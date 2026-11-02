#include "runtime_package.hpp"
#include <forge/authoring.hpp>
#include <forge/game_session.hpp>
#include <forge/navigation.hpp>
#include <forge/navigation_build.hpp>
#include <forge/render_components.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
constexpr const char* scene_path = "Scenes/140-navigation-links.scene.json";
constexpr const char* loop_name = "Cyan patrol - two-way link";
constexpr const char* one_way_name = "Orange traveler - one-way link";
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
Json read(const std::filesystem::path& path) {
    Json document;
    std::ifstream(path) >> document;
    return document;
}
flecs::entity create(Scene& scene, const char* name, Double3 position = {}, unsigned kind = 4) {
    const auto id = authoring_command(scene, "entity.create", {{"name", name}, {"kind", kind}})
                        .at("selected")
                        .get<std::string>();
    const auto entity = scene.entity(id);
    entity.set<LocalTranslation>({position[0], position[1], position[2]});
    return entity;
}
void prepare(const std::filesystem::path& root, const std::filesystem::path& worker) {
    const auto lease = std::make_shared<ProjectLease>(root);
    const auto initial = AssetCatalog::open_project(root);
    AssetId patrol;
    for (const auto& [id, record] : initial.records())
        if (record.type == NavigationBehaviorAsset::type &&
            record.source == "Assets/Navigation/Patrol.behavior.json")
            patrol = id;
    check(bool(patrol), "The existing original Patrol behavior is missing");
    EngineContext engine;
    Scene scene(engine.world());
    if (std::filesystem::exists(root / scene_path)) {
        scene.load(root / scene_path);
    } else {
        for (double y : {0., -6.}) {
            const bool loop = y == 0;
            for (double x : {-3., 3.}) {
                create(scene, loop ? "Loop walkable island" : "One-way walkable island", {x, y, 0})
                    .set<NavigationRegion2D>({true, false, 4, 4});
                create(scene, "Island visual - no navigation source", {x, y, 2}, 0)
                    .set<LocalScale>({4, 4, .15f})
                    .set<Tint>({.045f, .13f, .18f});
                create(scene, "Ground endpoint marker", {x < 0 ? -2. : 2., y, .5}, 1)
                    .set<LocalScale>({.22f, .22f, .22f})
                    .set<Tint>({.7f, .35f, 1.f});
            }
            NavigationLink link;
            link.plane = unsigned(NavigationPlane::PlanarXY);
            link.profile = loop ? "Loop" : "OneWay";
            link.bidirectional = loop;
            link.start_x = -2;
            link.end_x = 2;
            create(scene,
                   loop ? "Two-way connection - Loop profile"
                        : "Start to End only - OneWay profile",
                   {0, y, 0})
                .set<NavigationLink>(link);
            const auto actor = create(scene, loop ? loop_name : one_way_name, {-4, y, -1});
            actor.set<NavigationAgent>({{}, true, !loop, 2.5f, .08f, 4, y, 0});
            if (loop)
                actor.set<NavigationBehavior>({{patrol}, {}, {}, true});
            const auto body =
                create(scene, loop ? "Cyan traveler body" : "Orange traveler body", {}, 0);
            body.set<LocalScale>({.7f, .7f, .7f})
                .set<Tint>(loop ? Tint{.06f, .82f, 1.f} : Tint{1.f, .48f, .08f});
            scene.reparent_entity(body.get<PersistentEntityId>().value.str(),
                                  actor.get<PersistentEntityId>().value.str(),
                                  ReparentMode::KeepLocal);
        }
        Camera camera;
        camera.projection = unsigned(CameraProjection::Orthographic);
        camera.orthographic_height = 15;
        camera.aspect = 16.0 / 9;
        create(scene, "Overview Camera", {0, -3, -25}).set<Camera>(camera);
        create(scene, "Sunlight").set<Light>({});
    }
    const auto effective = scene.effective_document();
    for (const auto* name : {"Loop", "OneWay"}) {
        NavigationSettings settings;
        settings.plane = NavigationPlane::PlanarXY;
        settings.profile = name;
        settings.dynamic_obstacles = std::string_view(name) == "Loop";
        settings.cell_size = .2f;
        const auto catalog = AssetCatalog::open_project(root);
        const auto profiles = navigation_profiles(catalog, scene.asset_id());
        std::optional<AssetRecord> current;
        if (const auto at = profiles.find(name); at != profiles.end()) {
            const auto& record = catalog.records().at(at->second);
            if (record.metadata.at("settings") == Json(settings) &&
                navigation_geometry_digest(root, effective, record.metadata) ==
                    record.metadata.at("geometry_sha256") &&
                catalog.resolve(AssetRef<NavMeshAsset>{record.id}).state == AssetState::Available)
                current = record;
        }
        const auto nav =
            current ? current->id
                    : prepare_navigation(root, effective, settings, worker).publish(effective).id;
        const auto document = scene.document();
        for (const auto& row : document.at("entities")) {
            if (row.at("name") != (std::string_view(name) == "Loop" ? loop_name : one_way_name))
                continue;
            const auto entity = scene.entity(row.at("id"));
            auto agent = entity.get<NavigationAgent>();
            agent.navmesh = {nav};
            entity.set<NavigationAgent>(agent);
        }
    }
    scene.save(root / scene_path);
    const std::array roots{scene.asset_id()};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
    std::cout << "Gallery140 links prepared through ordinary Scene/navigation/resource owners\n";
}
void exercise(const std::filesystem::path& root, const std::filesystem::path& source) {
    const auto saved = read(root / source);
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
        check(std::chrono::steady_clock::now() < deadline, "Link Gallery readiness timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    session.activate(candidate, {}, false);
    auto& world = session.active();
    flecs::entity loop, one_way;
    for (const auto& row : saved.at("entities")) {
        if (row.at("name") == loop_name)
            loop = world.scene.entity(row.at("id"));
        if (row.at("name") == one_way_name)
            one_way = world.scene.entity(row.at("id"));
    }
    check(loop && one_way, "Link Gallery travelers missing");
    auto navigation =
        std::static_pointer_cast<NavigationRuntime>(world.engine.services().navigation());
    bool forward = false, reverse = false, orange_crossed = false;
    double minx = 100, maxx = -100;
    for (unsigned tick = 0; tick != 1200; ++tick) {
        const auto previous = loop.get<LocalTranslation>();
        session.step();
        const auto at = loop.get<LocalTranslation>(), other = one_way.get<LocalTranslation>();
        minx = std::min(minx, at.x);
        maxx = std::max(maxx, at.x);
        check(at.z == -1 && other.z == -1 && std::abs(at.y) < .001 && std::abs(other.y + 6) < .001,
              "Link Gallery changed lane or visual depth");
        if (std::abs(at.x) < .75) {
            check(!navigation->debug(loop.id()).at("link").is_null(),
                  "Patrol crossed the gap without its admitted link");
            forward |= at.x > previous.x;
            reverse |= at.x < previous.x;
        }
        if (std::abs(other.x) < .75) {
            check(!navigation->debug(one_way.id()).at("link").is_null(),
                  "One-way traveler crossed without an explicit span");
            orange_crossed = true;
        }
        for (const auto actor : {loop, one_way}) {
            const auto observation = navigation->agent_state(
                world.scene.reference(actor.get<PersistentEntityId>().value.str()));
            check(observation && observation->phase != NavigationAgentPhase::Failed,
                  "Gallery traveler failed: " + navigation->debug(actor.id()).dump());
        }
    }
    check(forward && reverse && orange_crossed && minx < -3.8 && maxx > 3.8 &&
              navigation
                      ->agent_state(
                          world.scene.reference(one_way.get<PersistentEntityId>().value.str()))
                      ->phase == NavigationAgentPhase::Arrived,
          "Gallery did not demonstrate repeated bidirectional behavior and one-way arrival");
    const auto selected = one_way.get<NavigationAgent>().navmesh;
    check(navigation->find_path(selected, {4, -6, 0}, {-4, -6, 0}).status != NavStatus::Success,
          "One-way Gallery profile permits reverse travel");
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string_view(argv[1]) == "--prepare") {
            prepare(std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]));
            return 0;
        }
        check(argc == 3, "Need Gallery and link scratch root");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto root = scratch / "Gallery";
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        exercise(root, scene_path);
        const auto saved = read(root / scene_path);
        const std::array roots{saved.at("asset_id").get<AssetId>()};
        package_runtime_content(root, scratch / "Export", roots, {"windows", "d3d12"});
        std::filesystem::rename(scratch / "Export", scratch / "Relocated");
        std::filesystem::rename(root, scratch / "SourcesOffline");
        const auto catalog = open_runtime_content(scratch / "Relocated", {"windows", "d3d12"});
        exercise(scratch / "Relocated", catalog.records().at(roots[0]).source);
        std::cout << "Gallery140 actual two-way patrol/one-way movement and source-hidden "
                     "relocated export PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
