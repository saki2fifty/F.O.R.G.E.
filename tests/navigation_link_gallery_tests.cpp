#include "navigation_asset.hpp"
#include "runtime_package.hpp"
#include <forge/authoring.hpp>
#include <forge/character_components.hpp>
#include <forge/game_session.hpp>
#include <forge/navigation.hpp>
#include <forge/navigation_build.hpp>
#include <forge/physics_components.hpp>
#include <forge/render_components.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
constexpr const char* scene_path = "Scenes/140-navigation-links.scene.json";
constexpr const char* arc_scene_path = "Scenes/218-navigation-link-arcs.scene.json";
constexpr std::array collision_scene_paths{"Scenes/222-navigation-link-collision-3d.scene.json",
                                           "Scenes/223-navigation-link-collision-xy.scene.json"};
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
void prepare(const std::filesystem::path& root, const std::filesystem::path& worker,
             bool arcs = false) {
    const auto source = arcs ? arc_scene_path : scene_path;
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
    if (std::filesystem::exists(root / source)) {
        scene.load(root / source);
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
            link.arc_height = arcs ? (loop ? 1.5f : 1.f) : 0.f;
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
                Json(navigation_geometry_digest(root, effective, record.metadata)) ==
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
    scene.save(root / source);
    const std::array roots{scene.asset_id()};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
    std::cout << (arcs ? "Gallery218 arcs" : "Gallery140 links")
              << " prepared through ordinary Scene/navigation/resource owners\n";
}
GameSessionConfig session_config(const std::filesystem::path& root) {
    GameSessionConfig config;
    config.content_root = root;
    struct Preparation final : GameScenePreparation {
        GamePreparationProgress poll(RuntimeWorld& world) override {
            return {world.prepare_scene_resources(), "scene resources"};
        }
        void activate() noexcept override {}
    };
    config.preparation = [](std::uint64_t) { return std::make_unique<Preparation>(); };
    return config;
}
void prepare_collision(const std::filesystem::path& root, const std::filesystem::path& worker,
                       bool xy) {
    const auto source = collision_scene_paths[xy ? 1 : 0];
    const auto lease = std::make_shared<ProjectLease>(root);
    EngineContext engine;
    Scene scene(engine.world());
    if (std::filesystem::exists(root / source)) {
        scene.load(root / source);
    } else {
        for (bool arc : {false, true}) {
            const double lane = arc ? -6 : 0;
            for (double x : {-3., 3.}) {
                const auto floor = create(scene, "Walkable island",
                                          xy ? Double3{x, lane, 0} : Double3{x, 0, lane});
                if (xy) {
                    floor.set<NavigationRegion2D>({true, false, 4, 4});
                    create(scene, "Island visual", {x, lane, 2}, 0)
                        .set<LocalScale>({4, 4, .15f})
                        .set<Tint>({.045f, .13f, .18f});
                } else
                    floor.set<Primitive>({3})
                        .set<LocalScale>({4, 1, 4})
                        .set<NavigationSurface>({})
                        .set<Tint>({.045f, .13f, .18f});
            }
            NavigationLink link;
            link.plane = unsigned(xy ? NavigationPlane::PlanarXY : NavigationPlane::Spatial3D);
            link.profile = arc ? "Arc" : "Straight";
            link.bidirectional = false;
            link.start_x = -2;
            link.end_x = 2;
            link.arc_height = arc ? 1.5f : 0;
            create(scene, arc ? "Arced crossing" : "Straight crossing",
                   xy ? Double3{0, lane, 0} : Double3{0, 0, lane})
                .set(link);
            const auto actor = create(scene, arc ? "Arc traveler" : "Straight traveler",
                                      xy ? Double3{-3, lane, -1} : Double3{-3, 0, lane});
            actor.set<NavigationAgent>({{}, true, true, 2, .05f, 3, xy ? lane : 0, xy ? 0 : lane})
                .set<NavigationLinkCollision>({});
            const auto body =
                create(scene, "Traveler visual", xy ? Double3{} : Double3{0, .5, 0}, 0);
            body.set<LocalScale>({.6f, xy ? .6f : 1.f, .6f})
                .set<Tint>(arc ? Tint{1.f, .48f, .08f} : Tint{.06f, .82f, 1.f});
            scene.reparent_entity(body.get<PersistentEntityId>().value.str(),
                                  actor.get<PersistentEntityId>().value.str(),
                                  ReparentMode::KeepLocal);
            const auto wall = create(scene, "Red physical blocker",
                                     xy ? Double3{0, lane, 0} : Double3{0, 4, lane}, 0);
            wall.set<Tint>({.8f, .08f, .06f});
            if (xy)
                wall.set<LocalScale>({.4f, 4, .2f})
                    .set<PhysicsBody2D>({})
                    .set<BoxCollider2D>({1, 1});
            else
                wall.set<LocalScale>({.4f, 8, 2}).set<PhysicsBody>({}).set<BoxCollider>({1, 1, 1});
        }
        Camera camera;
        camera.projection = unsigned(CameraProjection::Orthographic);
        camera.orthographic_height = 16;
        camera.aspect = 16.0 / 9;
        const auto view =
            create(scene, "Overview Camera", xy ? Double3{0, -3, -25} : Double3{0, 12, -23});
        view.set(camera);
        if (!xy)
            view.set<LocalRotation>({.258819f, 0, 0, .9659258f});
        create(scene, "Sunlight").set<Light>({});
    }
    for (const auto* profile : {"Straight", "Arc"}) {
        NavigationSettings settings;
        settings.plane = xy ? NavigationPlane::PlanarXY : NavigationPlane::Spatial3D;
        settings.profile = profile;
        settings.cell_size = .2f;
        const auto effective = scene.effective_document();
        const auto nav = prepare_navigation(root, effective, settings, worker).publish(effective);
        navigation_detail::Query query(navigation_detail::load(root, nav).mesh);
        const auto document = scene.document();
        for (const auto& row : document.at("entities")) {
            if (row.at("name") !=
                (std::string_view(profile) == "Arc" ? "Arc traveler" : "Straight traveler"))
                continue;
            const auto actor = scene.entity(row.at("id"));
            const auto start = actor.get<LocalTranslation>();
            const auto projected = query.project({start.x, start.y, start.z});
            check(projected.status == NavStatus::Success,
                  "Collision Gallery start is off its floor");
            actor.set<LocalTranslation>(
                {projected.points[0][0], projected.points[0][1], projected.points[0][2]});
            auto agent = actor.get<NavigationAgent>();
            agent.navmesh = {nav.id};
            actor.set(agent);
        }
    }
    scene.save(root / source);
    const std::array roots{scene.asset_id()};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
    std::cout << "Collision Gallery prepared through normal navigation/physics/content owners\n";
}
void exercise_collision(const std::filesystem::path& root, const std::filesystem::path& source,
                        bool xy) {
    GameSession session(session_config(root));
    const auto saved = read(root / source);
    const auto candidate = session.prepare(saved);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!session.poll_preparation(candidate).ready) {
        check(std::chrono::steady_clock::now() < deadline, "Collision Gallery readiness timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    session.activate(candidate, {}, false);
    auto& world = session.active();
    auto navigation =
        std::static_pointer_cast<NavigationRuntime>(world.engine.services().navigation());
    std::vector<flecs::entity> actors;
    for (const auto& row : saved.at("entities"))
        if (row.at("components").contains("forge.navigation_link_collision"))
            actors.push_back(world.scene.entity(row.at("id")));
    check(actors.size() == 2, "Collision Gallery travelers are missing");
    for (unsigned tick = 0; tick != 240; ++tick)
        session.step();
    for (const auto actor : actors) {
        const auto reference = world.scene.reference(actor.get<PersistentEntityId>().value.str());
        const auto state = navigation->agent_state(reference);
        check(state && state->phase == NavigationAgentPhase::Blocked && state->sampled &&
                  actor.get<LocalTranslation>().x < 0,
              "Collision Gallery did not block: " + navigation->debug(actor.id()).dump());
        if (xy)
            check(actor.get<LocalTranslation>().z == -1, "Collision Gallery lost XY visual depth");
        navigation->tune(reference, "forge.navigation_link_collision", "mask", 0u);
    }
    for (unsigned tick = 0; tick != 240; ++tick)
        session.step();
    for (const auto actor : actors)
        check(navigation
                      ->agent_state(
                          world.scene.reference(actor.get<PersistentEntityId>().value.str()))
                      ->phase == NavigationAgentPhase::Arrived,
              "Collision Gallery could not resume through its live filter");
}
void exercise(const std::filesystem::path& root, const std::filesystem::path& source,
              bool arcs = false) {
    const auto saved = read(root / source);
    GameSession session(session_config(root));
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
        const auto lift = [&](double x, double height) {
            return arcs && std::abs(x) < 2 ? height * (1 - x * x / 4) : 0;
        };
        check(at.z == -1 && other.z == -1 && std::abs(at.y - lift(at.x, 1.5)) < .001 &&
                  std::abs(other.y + 6 - lift(other.x, 1)) < .001,
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
#include "navigation_character_gallery.hpp"
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 4 && (std::string_view(argv[1]) == "--prepare-characters" ||
                          std::string_view(argv[1]) == "--prepare-jump-characters" ||
                          std::string_view(argv[1]) == "--prepare-planar-jump-characters" ||
                          std::string_view(argv[1]) == "--prepare-planar-characters" ||
                          std::string_view(argv[1]) == "--prepare-grounded-characters")) {
            prepare_character_gallery(
                std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]),
                std::string_view(argv[1]) == "--prepare-planar-characters" ||
                    std::string_view(argv[1]) == "--prepare-planar-jump-characters" ||
                    std::string_view(argv[1]) == "--prepare-grounded-characters",
                std::string_view(argv[1]) == "--prepare-jump-characters" ||
                    std::string_view(argv[1]) == "--prepare-planar-jump-characters",
                std::string_view(argv[1]) == "--prepare-grounded-characters" ||
                    std::string_view(argv[1]) == "--prepare-planar-jump-characters");
            return 0;
        }
        if (argc == 4 && (std::string_view(argv[1]) == "--prepare-collision-3d" ||
                          std::string_view(argv[1]) == "--prepare-collision-xy")) {
            prepare_collision(std::filesystem::absolute(argv[2]),
                              std::filesystem::absolute(argv[3]),
                              std::string_view(argv[1]) == "--prepare-collision-xy");
            return 0;
        }
        if (argc == 4 && (std::string_view(argv[1]) == "--prepare" ||
                          std::string_view(argv[1]) == "--prepare-arcs")) {
            prepare(std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]),
                    std::string_view(argv[1]) == "--prepare-arcs");
            return 0;
        }
        check(argc == 3 || (argc == 4 && (std::string_view(argv[3]) == "--collision" ||
                                          std::string_view(argv[3]) == "--characters" ||
                                          std::string_view(argv[3]) == "--jump-characters" ||
                                          std::string_view(argv[3]) == "--planar-jump-characters" ||
                                          std::string_view(argv[3]) == "--planar-characters" ||
                                          std::string_view(argv[3]) == "--grounded-characters")),
              "Need Gallery, scratch root and optional collision or character profile");
        const bool planar_jump_characters =
            argc == 4 && std::string_view(argv[3]) == "--planar-jump-characters";
        const bool grounded_characters =
            planar_jump_characters ||
            (argc == 4 && std::string_view(argv[3]) == "--grounded-characters");
        const bool planar_characters =
            grounded_characters ||
            (argc == 4 && std::string_view(argv[3]) == "--planar-characters");
        const bool jump_characters =
            planar_jump_characters ||
            (argc == 4 && std::string_view(argv[3]) == "--jump-characters");
        const bool characters = jump_characters || planar_characters ||
                                (argc == 4 && std::string_view(argv[3]) == "--characters");
        const bool collision = argc == 4 && !characters;
        const unsigned count = characters ? 1 : 2;
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto root = scratch / "Gallery";
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        std::array<AssetId, 2> roots;
        for (unsigned i = 0; i != count; ++i) {
            const auto source = planar_jump_characters ? planar_jump_character_scene_path
                                : grounded_characters  ? grounded_character_scene_path
                                : jump_characters      ? jump_character_scene_path
                                : planar_characters    ? planar_character_scene_path
                                : characters           ? character_scene_path
                                : collision            ? collision_scene_paths[i]
                                : i                    ? arc_scene_path
                                                       : scene_path;
            if (characters)
                exercise_character_gallery(root, source, planar_characters, jump_characters,
                                           grounded_characters);
            else if (collision)
                exercise_collision(root, source, i != 0);
            else
                exercise(root, source, i != 0);
            roots[i] = read(root / source).at("asset_id").get<AssetId>();
            const std::array selected{roots[i]};
            const auto exported = scratch / ("Export" + std::to_string(i));
            package_runtime_content(root, exported, selected, {"windows", "d3d12"});
            std::filesystem::rename(exported, scratch / ("Relocated" + std::to_string(i)));
        }
        std::filesystem::rename(root, scratch / "SourcesOffline");
        for (unsigned i = 0; i != count; ++i) {
            const auto relocated = scratch / ("Relocated" + std::to_string(i));
            const auto catalog = open_runtime_content(relocated, {"windows", "d3d12"});
            if (characters)
                exercise_character_gallery(relocated, catalog.records().at(roots[i]).source,
                                           planar_characters, jump_characters, grounded_characters);
            else if (collision)
                exercise_collision(relocated, catalog.records().at(roots[i]).source, i != 0);
            else
                exercise(relocated, catalog.records().at(roots[i]).source, i != 0);
        }
        std::cout << (planar_jump_characters ? "Gallery228 physical XY navigation jumps"
                      : grounded_characters  ? "Gallery227 grounded XY navigation characters"
                      : jump_characters      ? "Gallery226 physical navigation jumps"
                      : planar_characters    ? "Gallery225 Floating XY navigation characters"
                      : characters           ? "Gallery224 physics-driven navigation characters"
                      : collision ? "Gallery222/223 native collision blocking/live filtering"
                                  : "Gallery140/218 actual two-way patrol/one-way movement")
                  << " and source-hidden relocated export PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
