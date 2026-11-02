#include "runtime_package.hpp"
#include <forge/authoring.hpp>
#include <forge/game_session.hpp>
#include <forge/navigation.hpp>
#include <forge/navigation_build.hpp>
#include <forge/primitive_catalog.hpp>
#include <forge/project_lease.hpp>
#include <forge/render_scene.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
constexpr const char* scene_path = "Scenes/136-navigation-polygons.scene.json";
constexpr const char* left_agent = "Cyan - concave floor route";
constexpr const char* right_agent = "Amber - concave obstacle route";
void check(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
Json read(const std::filesystem::path& path) {
    Json result;
    std::ifstream(path) >> result;
    return result;
}
flecs::entity create(Scene& scene, const char* name, Double3 at,
                     unsigned primitive = no_primitive) {
    const auto id = authoring_command(
                        scene, "entity.create",
                        {{"name", name}, {"position", {{"x", at[0]}, {"y", at[1]}, {"z", at[2]}}}})
                        .at("selected")
                        .get<std::string>();
    const auto entity = scene.entity(id);
    entity.set<Primitive>({primitive});
    return entity;
}
void visual(Scene& scene, const char* name, Double3 center, LocalScale size, Tint tint,
            unsigned primitive = 0) {
    const auto entity = create(scene, name, center, primitive);
    entity.set<LocalScale>(size);
    entity.set<Tint>(tint);
}
void prepare(const std::filesystem::path& root, const std::filesystem::path& worker) {
    auto lease = std::make_shared<ProjectLease>(root);
    EngineContext author;
    Scene scene(author.world());
    if (std::filesystem::exists(root / scene_path))
        scene.load(root / scene_path);
    else {
        const auto floor = create(scene, "Walkable L - edit polygon Points", {-12, 0, 0});
        NavigationRegion2D region;
        region.points = {{-8, -8}, {8, -8}, {8, -2}, {-2, -2}, {-2, 8}, {-8, 8}};
        floor.set<NavigationRegion2D>(region);
        const auto second = create(scene, "Right rectangle floor", {12, 0, 0});
        second.set<NavigationRegion2D>({true, false, 20, 20});
        const auto obstacle = create(scene, "Concave L obstacle - edit polygon Points", {12, 0, 0});
        NavigationRegion2D mask;
        mask.obstacle = true;
        mask.points = {{-3, -3}, {3, -3}, {3, -1}, {-1, -1}, {-1, 3}, {-3, 3}};
        obstacle.set<NavigationRegion2D>(mask);
        visual(scene, "Left floor lower arm", {-12, -5, 2}, {16, 6, .1f}, {.045f, .12f, .16f});
        visual(scene, "Left floor upright arm", {-17, 3, 2}, {6, 10, .1f}, {.045f, .12f, .16f});
        visual(scene, "Right floor visual", {12, 0, 2}, {20, 20, .1f}, {.11f, .085f, .14f});
        visual(scene, "Obstacle lower arm", {12, -2, .6}, {6, 2, .8f}, {.55f, .18f, .12f});
        visual(scene, "Obstacle upright arm", {10, 1, .6}, {2, 4, .8f}, {.55f, .18f, .12f});
        const std::array names{left_agent, right_agent};
        const std::array<Double3, 2> starts{{{-17, 5, 0}, {5, 0, 0}}};
        const std::array<Double3, 2> goals{{{-7, -5, 0}, {19, 0, 0}}};
        for (unsigned i = 0; i < 2; ++i) {
            const auto agent = create(scene, names[i], starts[i]);
            agent.set<NavigationAgent>({{}, true, true, 3, .06f, goals[i][0], goals[i][1], -19});
            const auto body = create(scene, i ? "Amber agent visual" : "Cyan agent visual", {}, 1);
            body.set<LocalScale>({.8f, .8f, .8f});
            body.set<Tint>(i ? Tint{1, .62f, .08f} : Tint{.06f, .82f, 1});
            scene.reparent_entity(body.get<PersistentEntityId>().value.str(),
                                  agent.get<PersistentEntityId>().value.str(),
                                  ReparentMode::KeepLocal);
            visual(scene, i ? "Amber destination" : "Cyan destination",
                   {goals[i][0], goals[i][1], -.2}, {.45f, .45f, .45f},
                   i ? Tint{1, .8f, .35f} : Tint{.35f, .95f, 1}, 1);
        }
        const auto camera = create(scene, "Overview Camera", {1, 0, -40});
        Camera projection;
        projection.projection = unsigned(CameraProjection::Orthographic);
        projection.orthographic_height = 27;
        projection.aspect = 16.0 / 9.0;
        camera.set<Camera>(projection);
        create(scene, "Sunlight", {}).set<Light>({});
    }
    NavigationSettings settings;
    settings.plane = NavigationPlane::PlanarXY;
    const auto record = prepare_navigation(root, scene.effective_document(), settings, worker)
                            .publish(scene.effective_document());
    author.world().world().each([&](flecs::entity entity, const NavigationAgent& value) {
        auto next = value;
        next.navmesh = {record.id};
        entity.set<NavigationAgent>(next);
    });
    scene.save(root / scene_path);
    const std::array roots{scene.asset_id()};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
    std::cout << "Gallery136 polygon floor/obstacle and navigation publication ready\n";
}
void exercise(const std::filesystem::path& root, const std::filesystem::path& path) {
    const auto saved = read(root / path);
    GameSessionConfig config;
    config.content_root = root;
    GameSession session(config);
    const auto ticket = session.prepare(saved);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!session.poll_preparation(ticket).ready) {
        check(std::chrono::steady_clock::now() < deadline, "Polygon Gallery preparation timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    session.activate(ticket, {}, false);
    auto& world = session.active();
    const auto nav =
        std::static_pointer_cast<NavigationRuntime>(world.engine.services().navigation());
    std::array<flecs::entity, 2> actors;
    for (const auto& row : saved.at("entities")) {
        if (row.at("name") == left_agent)
            actors[0] = world.scene.entity(row.at("id"));
        if (row.at("name") == right_agent)
            actors[1] = world.scene.entity(row.at("id"));
    }
    check(bool(actors[0]) && bool(actors[1]), "Missing polygon Gallery agents");
    for (unsigned tick = 1; tick <= 600; ++tick) {
        session.step();
        world.engine.world().evaluate_world_transforms();
        for (unsigned index = 0; index < 2; ++index) {
            const auto position = actors[index].get<WorldTransform>().affine.point({0, 0, 0});
            check(position[2] == 0, "Planar route lost its independent visual depth");
            if (index == 0)
                check(position[0] + 12 <= -2 || position[1] <= -2,
                      "Agent crossed the concave walkable notch");
            else {
                const auto x = position[0] - 12, y = position[1];
                check(!((x > -3 && x < -1 && y > -3 && y < 3) ||
                        (x > -3 && x < 3 && y > -3 && y < -1)),
                      "Agent crossed the analytic concave obstacle");
            }
        }
        if (tick == 1 || tick == 240 || tick == 600) {
            const auto frame = extract_render_scene(session.presentation());
            check(frame.cameras.size() == 1 && frame.lights.size() == 1 && frame.meshes.size() >= 9,
                  "Polygon Gallery lost ordinary Game render submissions");
        }
    }
    for (const auto actor : actors) {
        const auto observation =
            nav->agent_state(world.scene.reference(actor.get<PersistentEntityId>().value.str()));
        check(observation && observation->phase == NavigationAgentPhase::Arrived,
              "Polygon Gallery agent did not reach its authored destination");
    }
    check(session.status().at("clock").at("tick") == 600 && read(root / path) == saved,
          "Gallery stepping changed source bytes or clock ownership");
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string_view(argv[1]) == "--prepare") {
            prepare(std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]));
            return 0;
        }
        check(argc == 3, "Need Gallery and polygon scratch root");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto root = scratch / "Gallery";
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        exercise(root, scene_path);
        const auto scene = read(root / scene_path);
        const std::array roots{scene.at("asset_id").get<AssetId>()};
        const auto output = scratch / "Export";
        (void)package_runtime_content(root, output, roots, {"windows", "d3d12"});
        const auto relocated = scratch / "Relocated", hidden = scratch / "SourcesOffline";
        std::filesystem::rename(output, relocated);
        std::filesystem::rename(root, hidden);
        const auto catalog = open_runtime_content(relocated, {"windows", "d3d12"});
        check(catalog.records().size() == 2,
              "Polygon Gallery package has incomplete/extra closure");
        exercise(relocated, catalog.records().at(roots[0]).source);
        std::filesystem::remove_all(scratch);
        std::cout << "Gallery136 actual GameSession routes, geometry exclusion, render submissions "
                     "and source-free relocated export PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
