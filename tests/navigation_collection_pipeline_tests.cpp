#include "asset_bytes.hpp"
#include "editor/document.hpp"
#include "navigation_asset.hpp"
#include "navigation_sources.hpp"
#include <cmath>
#include <forge/authoring.hpp>
#include <forge/navigation.hpp>
#include <forge/physics_components.hpp>
#include <forge/runtime.hpp>
#include <iostream>

using namespace forge;
using namespace forge::navigation_detail;
namespace {
void require(bool value, const char* reason) {
    if (!value)
        throw std::runtime_error(reason);
}
template <class F> void rejects(F&& call) {
    try {
        call();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid navigation collection candidate was published");
}
void admission(const std::filesystem::path& root, const AssetRecord& record) {
    const auto bytes = asset_detail::read_bytes(root / record.source, max_nav_bytes + 65556);
    const auto canonical = admit(bytes).metadata;
    std::size_t metadata_size = 0;
    for (unsigned index = 0; index < 4; ++index)
        metadata_size |= std::size_t(std::to_integer<unsigned>(bytes[12 + index])) << (index * 8);
    const auto payload = std::span(bytes).subspan(20 + metadata_size);
    const auto forged = [&](const Json& replacement) {
        const auto encoded = replacement.dump();
        auto result = std::vector<std::byte>(bytes.begin(), bytes.begin() + 20);
        const auto version = replacement.at("version").get<unsigned>();
        for (unsigned index = 0; index < 4; ++index) {
            result[8 + index] = std::byte((version >> (index * 8)) & 255);
            result[12 + index] = std::byte((encoded.size() >> (index * 8)) & 255);
        }
        const auto values = std::as_bytes(std::span(encoded));
        result.insert(result.end(), values.begin(), values.end());
        result.insert(result.end(), payload.begin(), payload.end());
        return result;
    };
    require(admit(forged(canonical)).metadata == canonical,
            "Admission fixture changed the genuine native payload");
    const auto bad = [&](const auto& edit) {
        auto changed = canonical;
        edit(changed);
        rejects([&] { (void)admit(forged(changed)); });
    };
    bad([](auto& meta) { meta["settings"].erase("collect_static_colliders"); });
    bad([](auto& meta) { meta["settings"]["collect_static_colliders"] = false; });
    bad([](auto& meta) { meta["settings"]["collision_layers"] = std::uint64_t(1) << 32; });
    bad([](auto& meta) { meta["geometry_version"] = 1; });
    bad([](auto& meta) { meta["links"] = Json::array(); });
    for (unsigned version : {1u, 2u, 3u, 4u, 5u, 6u})
        bad([&](auto& meta) { meta["version"] = version; });
    if (canonical.at("settings").get<NavigationSettings>().plane == NavigationPlane::Spatial3D) {
        bad([](auto& meta) { meta.erase("collision_inputs"); });
        const auto input =
            Json{{"asset_id", AssetId::generate()}, {"revision", std::string(64, 'a')}};
        for (const auto& invalid : {Json(nullptr), Json::object(), Json::array({input, input})})
            bad([&](auto& meta) { meta["collision_inputs"] = invalid; });
        for (const auto& patch :
             {Json{{"asset_id", "00000000-0000-0000-0000-000000000000"}},
              Json{{"revision", std::string(64, 'A')}}, Json{{"revision", std::string(63, 'a')}},
              Json{{"revision", 7}}, Json{{"extra", true}}}) {
            auto invalid = input;
            invalid.update(patch);
            bad([&](auto& meta) { meta["collision_inputs"] = Json::array({invalid}); });
        }
        auto excessive = Json::array();
        for (unsigned index = 0; index < 33; ++index)
            excessive.push_back(
                {{"asset_id", AssetId::generate()}, {"revision", std::string(64, 'a')}});
        bad([&](auto& meta) { meta["collision_inputs"] = excessive; });
    } else
        bad([](auto& meta) { meta["collision_inputs"] = Json::array(); });
}
void movement(const Json& snapshot, const std::filesystem::path& root, AssetId nav,
              const std::string& actor_id, const std::string& body_id, NavigationPlane plane,
              Double3 start, Double3 end) {
    EngineContext engine(WorldRole::Runtime, false, {navigation_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(snapshot);
    RuntimeSimulation simulation(engine.world(), scene);
    const auto actor = scene.entity(actor_id), body = scene.entity(body_id);
    const auto navigation =
        std::static_pointer_cast<NavigationRuntime>(engine.world().services().navigation());
    require(navigation->find_path({nav}, start, end).status == NavStatus::Success,
            "Runtime collection profile fell back to explicit-only source validity");
    const auto set_layer = [&](unsigned layer) {
        if (plane == NavigationPlane::PlanarXY) {
            auto config = body.get<PhysicsBody2D>();
            config.layer = layer;
            body.set(config);
        } else {
            auto config = body.get<PhysicsBody>();
            config.layer = layer;
            body.set(config);
        }
    };
    for (unsigned step = 0; step < 400; ++step) {
        const auto previous = actor.get<LocalTranslation>();
        simulation.tick(1.f / 60);
        const auto position = actor.get<LocalTranslation>();
        require(std::hypot(position.x - previous.x, position.y - previous.y,
                           position.z - previous.z) <= 3. / 60 + .00001,
                "Collected-source Agent exceeded its fixed-tick distance budget");
        if (plane == NavigationPlane::PlanarXY)
            require(position.z == start[2], "Collected-source Agent changed XY visual depth");
        if (step == 80) {
            set_layer(1);
            for (unsigned held = 0; held < 3; ++held) {
                simulation.tick(1.f / 60);
                require(actor.get<LocalTranslation>() == position,
                        "Agent moved on navigation after its selected collision layer changed");
            }
            require(navigation->find_path({nav}, start, end).status != NavStatus::Success,
                    "Unmatched current collision sources were treated as admitted geometry");
            set_layer(0);
        }
        if (step == 120) {
            const auto checkpoint = navigation->checkpoint();
            EngineContext resumed(WorldRole::Runtime, false, {navigation_module(root)});
            Scene restored(resumed.world());
            restored.restore_snapshot(scene.snapshot());
            RuntimeSimulation continuation(resumed.world(), restored);
            const auto service = std::static_pointer_cast<NavigationRuntime>(
                resumed.world().services().navigation());
            service->restore(checkpoint);
            for (unsigned next = 0; next < 2; ++next) {
                continuation.tick(1.f / 60);
                simulation.tick(1.f / 60);
                require(restored.entity(actor_id).get<LocalTranslation>() ==
                            actor.get<LocalTranslation>(),
                        "Collected-source recovery changed deterministic continuation");
            }
        }
    }
    const auto finish = actor.get<LocalTranslation>();
    if (std::hypot(finish.x - end[0], finish.y - end[1], finish.z - end[2]) >= .08)
        throw std::runtime_error("Collected-source Agent did not resume and arrive: " +
                                 Json{{"position", {finish.x, finish.y, finish.z}},
                                      {"expected", end},
                                      {"state", navigation->debug(actor.id())}}
                                     .dump());
}
void exercise(const std::filesystem::path& root, const std::filesystem::path& worker,
              NavigationPlane plane, bool dynamic) {
    SceneDocument::create_project(root, "Physical navigation sources");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    const auto add = [&](const char* name) {
        return authoring_command(scene, "entity.create", {{"name", name}, {"kind", 4}})
            .at("selected")
            .get<std::string>();
    };
    const auto body_id = add("Unmarked physical source");
    auto body = scene.entity(body_id);
    if (plane == NavigationPlane::PlanarXY) {
        scene.entity(add("Walkable area"))
            .set<LocalTranslation>({0, 0, 0})
            .set<NavigationRegion2D>({true, false, 20, 12});
        body.set<LocalTranslation>({0, 0, 0});
        body.set<PhysicsBody2D>({});
        body.set<BoxCollider2D>({2, 4});
    } else {
        body.set<PhysicsBody>({});
        body.set<BoxCollider>({12, 1, 12});
        body.set<LocalTranslation>({0, -.5, 0});
    }
    project.save();
    NavigationSettings settings;
    settings.plane = plane;
    settings.dynamic_obstacles = dynamic;
    settings.collect_static_colliders = true;
    settings.radius = .2f;
    settings.profile = "Physical";
    settings.collision_layers = 1;
    const auto document = scene.effective_document();
    const auto record = prepare_navigation(root, document, settings, worker).publish(document);
    require(record.schema_version == 7 && record.metadata.at("settings") == Json(settings),
            "Collection policy did not reach the worker, envelope and catalog");
    require(Json(navigation_geometry_digest(root, document, record.metadata)) ==
                record.metadata.at("geometry_sha256"),
            "Published collected source does not match current selected provenance");
    const auto admitted = load(root, record);
    admission(root, record);
    Query query(admitted.mesh);
    const bool xy = plane == NavigationPlane::PlanarXY;
    const auto route =
        query.path({xy ? -6. : -4., 0, xy ? 17. : 0.}, {xy ? 6. : 4., 0, xy ? 17. : 0.});
    require(route.status == NavStatus::Success, "Collected sources did not produce a real route");
    if (xy) {
        require(route.points.size() > 2, "Automatic obstacle did not divert the XY route");
        bool diverted = false;
        for (const auto& point : route.points) {
            require(point[2] == 17, "Collision collection changed visual depth");
            diverted |= std::abs(point[1]) > 2;
        }
        require(diverted, "XY route crossed the physical obstacle");
    } else {
        for (const auto& point : route.points)
            require(std::abs(point[1]) < .21, "Physical floor supplied the wrong route height");
        require(record.metadata.at("collision_inputs").empty(),
                "Inline shapes invented external Collision provenance");
    }
    const auto actor_id = add("Route follower");
    auto actor = scene.entity(actor_id);
    const auto start = route.points.front(), end = route.points.back();
    actor.set<LocalTranslation>({start[0], start[1], start[2]});
    NavigationAgent agent;
    agent.navmesh = {record.id};
    agent.speed = 3;
    agent.stopping_distance = .05f;
    agent.destination_x = end[0];
    agent.destination_y = end[1];
    agent.destination_z = end[2];
    agent.has_destination = true;
    actor.set(agent);
    movement(scene.snapshot(), root, record.id, actor_id, body_id, plane, start, end);
    auto prepared = prepare_navigation(root, document, settings, worker);
    if (xy)
        body.set<BoxCollider2D>({3, 4});
    else
        body.set<BoxCollider>({14, 1, 12});
    const auto changed = scene.effective_document();
    require(Json(navigation_geometry_digest(root, changed, record.metadata)) !=
                record.metadata.at("geometry_sha256"),
            "Collected shape edit did not invalidate the saved navigation selection");
    rejects([&] { (void)prepared.publish(changed); });
    require(AssetCatalog::open_project(root).records().at(record.id).metadata == record.metadata,
            "Rejected stale candidate replaced the prior asset");
    require(load(root, record).metadata == admitted.metadata,
            "Rejected candidate changed admitted immutable bytes");
}
void linked(const std::filesystem::path& root, const std::filesystem::path& worker,
            NavigationPlane plane, bool dynamic) {
    SceneDocument::create_project(root, "Collected physical islands and an authored connection");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    const auto add = [&](const char* name) {
        return authoring_command(scene, "entity.create", {{"name", name}, {"kind", 4}})
            .at("selected")
            .get<std::string>();
    };
    const bool xy = plane == NavigationPlane::PlanarXY;
    std::string body_id;
    for (double x : {-3., 3.}) {
        const auto id = add("Physical island");
        auto entity = scene.entity(id);
        entity.set<LocalTranslation>({x, xy ? 0 : (x < 0 ? -.5 : 1.5), 0});
        if (xy)
            entity.set<NavigationRegion2D>({true, false, 4, 6});
        else
            entity.set<PhysicsBody>({}).set<BoxCollider>({4, 1, 6});
        if (x < 0)
            body_id = id;
    }
    if (xy) {
        body_id = add("Unmarked physical obstacle on the left island");
        scene.entity(body_id)
            .set<LocalTranslation>({-3, 2, 0})
            .set<PhysicsBody2D>({})
            .set<BoxCollider2D>({.5f, .5f});
    }
    NavigationSettings settings;
    settings.plane = plane;
    settings.dynamic_obstacles = dynamic;
    settings.collect_static_colliders = true;
    settings.collision_layers = 1;
    settings.cell_size = .2f;
    settings.profile = "Physical";
    NavigationLink connection;
    connection.plane = unsigned(plane);
    connection.profile = settings.profile;
    connection.start_x = -2;
    connection.end_x = 2;
    connection.end_y = xy ? 0 : 2;
    connection.bidirectional = false;
    scene.entity(add("Authored one-way connection"))
        .set<LocalTranslation>({0, 0, 0})
        .set<NavigationLink>(connection);
    project.save();
    const auto document = scene.effective_document();
    const auto publish = [&](const NavigationSettings& profile) {
        return prepare_navigation(root, document, profile, worker).publish(document);
    };
    auto other_settings = settings;
    other_settings.profile = "Other";
    const auto other = publish(other_settings);
    const auto old_bytes = asset_detail::read_bytes(root / other.source, max_nav_bytes + 65556);
    const auto selected = publish(settings);
    const auto catalog = AssetCatalog::open_project(root);
    require(catalog.records().at(other.id).metadata == other.metadata &&
                asset_detail::read_bytes(root / other.source, max_nav_bytes + 65556) == old_bytes,
            "Publishing collected linked navigation changed another profile");
    const auto sources = capture(document, settings);
    auto candidates = engine.world().world().query<const PersistentEntityId>();
    const auto native = capture_native(engine.world(), scene, candidates, settings);
    require(sources.connections.size() == 1 && sources.connections == native.connections &&
                source_digest(sources, Json::array()) == source_digest(native, Json::array()),
            "Collected sources and authored links diverged between capture paths");
    candidates.destruct();
    const Double3 start{-3, 0, xy ? 13. : 0.}, end{3, xy ? 0. : 2., xy ? 13. : 0.};
    Query connected(load(root, selected).mesh), isolated(load(root, other).mesh);
    const auto route = connected.route(start, end);
    require(route.links.size() == 1 && route.path.status == NavStatus::Success &&
                connected.path(end, start).status != NavStatus::Success &&
                isolated.path(start, end).status != NavStatus::Success,
            "Collected linked profile lost direction, reachability or profile isolation");
    const auto actor_id = add("Physical link traveler");
    const auto projected = connected.project(start);
    require(projected.status == NavStatus::Success, "Physical link start is not walkable");
    const auto at = projected.points.front();
    scene.entity(actor_id)
        .set<LocalTranslation>({at[0], at[1], at[2]})
        .set<NavigationAgent>({{selected.id}, true, true, 3, .05f, end[0], end[1], end[2]});
    movement(scene.snapshot(), root, selected.id, actor_id, body_id, plane, at,
             route.path.points.back());
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need collision collection scratch directory and native worker");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        const auto worker = std::filesystem::absolute(argv[2]);
        for (auto plane : {NavigationPlane::Spatial3D, NavigationPlane::PlanarXY})
            for (bool dynamic : {false, true}) {
                std::cout << "Collection plane " << unsigned(plane)
                          << (dynamic ? " dynamic" : " static") << std::endl;
                exercise(root /
                             (std::to_string(unsigned(plane)) + (dynamic ? "-dynamic" : "-static")),
                         worker, plane, dynamic);
                linked(root / (std::to_string(unsigned(plane)) +
                               (dynamic ? "-linked-dynamic" : "-linked-static")),
                       worker, plane, dynamic);
            }
        std::filesystem::remove_all(root);
        std::cout << "Collected physical sources: XY/3D static/dynamic publication and stale "
                     "rejection pass\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
