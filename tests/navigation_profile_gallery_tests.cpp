#include "runtime_package.hpp"
#include <forge/authoring.hpp>
#include <forge/game_session.hpp>
#include <forge/navigation_build.hpp>
#include <forge/primitive_catalog.hpp>
#include <forge/project_lease.hpp>
#include <forge/render_components.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
constexpr const char* scene_path = "Scenes/138-navigation-profiles.scene.json";
constexpr std::array<const char*, 2> actor_names{"Cyan - default profile passage",
                                                 "Amber - Large profile left room"};
constexpr std::array<Double3, 2> starts{{{-7, 0, -1}, {-7, 3, -1}}};
constexpr std::array<Double3, 2> goals{{{7, 0, -1}, {-4, 3, -1}}};
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
Json read(const std::filesystem::path& path) {
    Json value;
    std::ifstream(path) >> value;
    return value;
}
flecs::entity create(Scene& scene, const char* name, Double3 at = {},
                     unsigned primitive = no_primitive) {
    const auto id = authoring_command(scene, "entity.create", {{"name", name}})
                        .at("selected")
                        .get<std::string>();
    const auto entity = scene.entity(id);
    entity.set<LocalTranslation>({at[0], at[1], at[2]});
    entity.set<Primitive>({primitive});
    return entity;
}
void visual(Scene& scene, const char* name, Double3 at, LocalScale scale, Tint tint,
            unsigned primitive = 0) {
    create(scene, name, at, primitive).set<LocalScale>(scale).set<Tint>(tint);
}
void prepare(const std::filesystem::path& root, const std::filesystem::path& worker) {
    ProjectLease lease(root);
    EngineContext engine;
    Scene scene(engine.world());
    if (std::filesystem::exists(root / scene_path))
        scene.load(root / scene_path);
    else {
        create(scene, "Walkable floor - both profiles")
            .set<NavigationRegion2D>({true, false, 20, 12});
        visual(scene, "Floor visual", {0, 0, 3}, {20, 12, .1f}, {.045f, .12f, .16f});
        for (const float sign : {-1.f, 1.f}) {
            create(scene, sign < 0 ? "Lower wall" : "Upper wall", {0, sign * 3.75, 0})
                .set<NavigationRegion2D>({true, true, 2, 4.5f});
            visual(scene, sign < 0 ? "Lower wall visual" : "Upper wall visual", {0, sign * 3.75, 1},
                   {2, 4.5f, .8f}, {.8f, .22f, .08f});
        }
        for (unsigned i = 0; i < 2; ++i) {
            const auto actor = create(scene, actor_names[i], starts[i]);
            actor.set<NavigationAgent>({{}, true, true, 3, .06f, goals[i][0], goals[i][1], 0});
            const auto body = create(scene, i ? "Large amber body" : "Small cyan body", {}, 1);
            const float diameter = i ? 3.6f : .4f;
            body.set<LocalScale>({diameter, diameter, .4f});
            body.set<Tint>(i ? Tint{1, .62f, .08f} : Tint{.06f, .82f, 1});
            scene.reparent_entity(body.get<PersistentEntityId>().value.str(),
                                  actor.get<PersistentEntityId>().value.str(),
                                  ReparentMode::KeepLocal);
            visual(scene, i ? "Large destination" : "Small destination", goals[i], {.3f, .3f, .3f},
                   i ? Tint{1, .8f, .35f} : Tint{.35f, .95f, 1}, 1);
        }
        Camera camera;
        camera.projection = unsigned(CameraProjection::Orthographic);
        camera.orthographic_height = 15;
        camera.aspect = 16.0 / 9;
        create(scene, "Overview Camera", {0, 0, -25}).set<Camera>(camera);
        create(scene, "Sunlight").set<Light>({});
    }
    NavigationSettings small;
    small.plane = NavigationPlane::PlanarXY;
    small.radius = .2f;
    small.cell_size = .1f;
    auto large = small;
    large.profile = "Large";
    large.radius = 1.8f;
    std::array<AssetId, 2> ids;
    const std::array settings{small, large};
    for (unsigned i = 0; i < 2; ++i) {
        const auto catalog = AssetCatalog::open_project(root);
        const auto profiles = navigation_profiles(catalog, scene.asset_id());
        std::optional<AssetRecord> current;
        const auto document = scene.effective_document();
        if (const auto found = profiles.find(settings[i].profile); found != profiles.end()) {
            const auto& record = catalog.records().at(found->second);
            if (record.metadata.at("settings") == Json(settings[i]) &&
                Json(navigation_geometry_digest(root, document, record.metadata)) ==
                    record.metadata.at("geometry_sha256") &&
                catalog.resolve(AssetRef<NavMeshAsset>{record.id}).state == AssetState::Available)
                current = record;
        }
        ids[i] = current
                     ? current->id
                     : prepare_navigation(root, document, settings[i], worker).publish(document).id;
    }
    std::array<flecs::entity, 2> actors;
    engine.world().world().each(
        [&](flecs::entity entity, const AuthoredName& name, const NavigationAgent&) {
            for (unsigned i = 0; i < actors.size(); ++i)
                if (name.value == actor_names[i])
                    actors[i] = entity;
        });
    for (unsigned i = 0; i < actors.size(); ++i) {
        check(bool(actors[i]), "Missing profile Gallery actor during preparation");
        actors[i].set<LocalTranslation>({starts[i][0], starts[i][1], starts[i][2]});
        actors[i].set<NavigationAgent>(
            {{ids[i]}, true, true, 3, .06f, goals[i][0], goals[i][1], 0});
        actors[i].set<NavigationAvoidance>({true, 2, 1});
    }
    scene.save(root / scene_path);
    const std::array roots{scene.asset_id()};
    prepare_runtime_content_catalog(lease, roots, {}, scene.schema());
    std::cout << "Gallery138 default and Large navigation profiles published\n";
}
void exercise(const std::filesystem::path& root, const std::filesystem::path& source) {
    const auto saved = read(root / source);
    GameSessionConfig config;
    config.content_root = root;
    GameSession session(config);
    const auto candidate = session.prepare(saved);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!session.poll_preparation(candidate).ready) {
        check(std::chrono::steady_clock::now() < deadline, "Profile Gallery preparation timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    session.activate(candidate, {}, false);
    auto& world = session.active();
    std::array<flecs::entity, 2> actors;
    for (const auto& row : saved.at("entities"))
        for (unsigned i = 0; i < 2; ++i)
            if (row.at("name") == actor_names[i])
                actors[i] = world.scene.entity(row.at("id"));
    check(actors[0] && actors[1], "Profile Gallery agents missing");
    for (const auto actor : actors)
        check(actor.has<NavigationAvoidance>() &&
                  actor.get<NavigationAvoidance>().shared_group == 1,
              "Profile Gallery agents must share an avoidance group");
    const auto small = actors[0].get<NavigationAgent>().navmesh;
    const auto large = actors[1].get<NavigationAgent>().navmesh;
    const auto nav = world.engine.services().navigation();
    session.step();
    check(small != large &&
              nav->find_path(small, {-7, 0, -1}, {7, 0, -1}).status == NavStatus::Success &&
              nav->find_path(large, {-7, 0, -1}, {7, 0, -1}).status != NavStatus::Success,
          "Independent profiles lost their clearance difference");
    for (unsigned tick = 0; tick < 600; ++tick) {
        session.step();
        const auto cyan = actors[0].get<LocalTranslation>();
        const auto amber = actors[1].get<LocalTranslation>();
        const bool cyan_hits_wall = std::abs(cyan.x) < 1.2 && std::abs(cyan.y) > 1.3;
        check(cyan.z == -1 && amber.z == -1 && std::abs(cyan.x) < 9.8 && std::abs(cyan.y) < 5.8 &&
                  !cyan_hits_wall && amber.x < -2.8 && amber.x > -8.2 && std::abs(amber.y) < 4.2 &&
                  std::hypot(cyan.x - amber.x, cyan.y - amber.y) > 1.8,
              "Profile route crossed a wall or lost its independent depth/room");
    }
    for (const auto actor : actors) {
        const auto state =
            nav->agent_state(world.scene.reference(actor.get<PersistentEntityId>().value.str()));
        const auto position = actor.get<LocalTranslation>();
        check(state && state->phase == NavigationAgentPhase::Arrived,
              "Profile Gallery agent did not arrive: " + actor.get<AuthoredName>().value + " at " +
                  Json::array({position.x, position.y, position.z}).dump() + "; state " +
                  (state ? std::to_string(unsigned(state->phase)) : "missing"));
    }
    check(read(root / source) == saved, "Profile gameplay modified saved source");
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string_view(argv[1]) == "--prepare") {
            prepare(std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]));
            return 0;
        }
        check(argc == 3, "Need Gallery and profile scratch root");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto root = scratch / "Gallery";
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        exercise(root, scene_path);
        const auto saved = read(root / scene_path);
        const std::array roots{saved.at("asset_id").get<AssetId>()};
        const auto output = scratch / "Export";
        package_runtime_content(root, output, roots, {"windows", "d3d12"});
        const auto moved = scratch / "Relocated", hidden = scratch / "SourcesOffline";
        std::filesystem::rename(output, moved);
        std::filesystem::rename(root, hidden);
        const auto catalog = open_runtime_content(moved, {"windows", "d3d12"});
        exercise(moved, catalog.records().at(roots.front()).source);
        std::cout << "Profile Gallery routes and source-hidden relocated export PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
