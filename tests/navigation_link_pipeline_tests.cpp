#include "editor/document.hpp"
#include "navigation_asset.hpp"
#include "navigation_sources.hpp"
#include <forge/authoring.hpp>
#include <forge/character_components.hpp>
#include <forge/navigation.hpp>
#include <forge/physics.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
#include <string_view>
using namespace forge;
namespace {
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
std::string read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
Json record(const AssetCatalog& catalog, AssetId id) {
    const auto document = catalog.document();
    for (const auto& row : document.at("assets"))
        if (row.at("id").get<AssetId>() == id)
            return row;
    throw std::runtime_error("Published navigation record missing");
}
#include "navigation_character_jump_tests.hpp"
#include "navigation_link_collision_tests.hpp"
#include "navigation_link_runtime_tests.hpp"
void run(const std::filesystem::path& root, const std::filesystem::path& worker,
         NavigationPlane plane, bool dynamic, bool arc, bool collision = false,
         bool character = false) {
    using namespace navigation_detail;
    SceneDocument::create_project(root, "Authored navigation connections");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    std::string left;
    for (float x : {-3.f, 3.f}) {
        const auto id =
            authoring_command(scene, "entity.create", {{"name", "Walkable island"}, {"kind", 4}})
                .at("selected")
                .get<std::string>();
        auto entity = scene.entity(id);
        entity.set<LocalTranslation>(
            {x, plane == NavigationPlane::Spatial3D && x > 0 ? 2. : 0., 0});
        if (plane == NavigationPlane::PlanarXY)
            entity.set<NavigationRegion2D>({true, false, 4, 6});
        else
            entity.set<Primitive>({3}).set<LocalScale>({4, 1, 6}).set<NavigationSurface>({});
        if (x < 0)
            left = id;
    }
    project.save();
    NavigationSettings settings;
    settings.plane = plane;
    settings.dynamic_obstacles = dynamic;
    settings.cell_size = .2f;
    settings.profile = dynamic ? "Bridge" : "default";
    const auto publish = [&](const NavigationSettings& value) {
        const auto document = scene.effective_document();
        return prepare_navigation(root, document, value, worker).publish(document);
    };
    auto other = settings;
    other.profile = "Other";
    const auto untouched = publish(other);
    const auto old_row = record(AssetCatalog::open_project(root), untouched.id);
    const auto old_bytes = read(root / untouched.source);
    const auto before = capture(scene.effective_document(), plane, settings.profile);
    const auto before_digest = source_digest(before, Json::array());
    NavigationLink link;
    link.plane = unsigned(plane);
    link.profile = settings.profile;
    link.arc_height = arc ? 1.f : 0.f;
    link.bidirectional = false;
    link.end_y = plane == NavigationPlane::Spatial3D ? 2.f : 0.f;
    link.start_x = plane == NavigationPlane::PlanarXY ? 1.f : .25f;
    link.end_x = plane == NavigationPlane::PlanarXY ? 5.f : 1.25f;
    scene.entity(left).set<NavigationLink>(link);
    project.save();
    const auto effective = scene.effective_document();
    const auto selected = capture(effective, plane, settings.profile);
    check(selected.identities == before.identities && selected.connections.size() == 1 &&
              source_digest(selected, Json::array()) != before_digest,
          "A link on an existing source did not change its digest without changing source IDs");
    check(source_digest(capture(effective, plane, "Other"), Json::array()) == before_digest,
          "Link from another profile changed unselected source bytes");
    auto candidates = engine.world()
                          .world()
                          .query_builder<const PersistentEntityId>()
                          .cache_kind(flecs::QueryCacheNone)
                          .query_flags(EcsQueryMatchPrefab | EcsQueryMatchDisabled)
                          .build();
    const auto native = capture_native(engine.world(), scene, candidates, plane, settings.profile);
    check(selected.identities == native.identities && selected.connections == native.connections &&
              source_digest(selected, Json::array()) == source_digest(native, Json::array()),
          "Native capture disagrees with effective Scene capture for a selected link");
    check(selected.connections.front().start[0] == -2 && selected.connections.front().end[0] == 2,
          "Authored endpoints did not follow their source transform");
    const auto baked = publish(settings);
    const auto catalog = AssetCatalog::open_project(root);
    check(catalog.resolve(AssetRef<NavMeshAsset>{baked.id}).state == AssetState::Available &&
              baked.schema_version == (arc ? 8u : 6u) &&
              baked.metadata.at("version") == (arc ? 8 : 6) &&
              baked.metadata.at("links").at(0).at("entity").get<EntityId>() ==
                  EntityId::parse(left),
          "Normal candidate/catalog did not publish the matching linked Nav profile");
    check(record(catalog, untouched.id) == old_row && read(root / untouched.source) == old_bytes,
          "Publishing a linked profile changed another profile's record or immutable artifact");
    Query query(load(root, baked).mesh);
    const Double3 start =
        plane == NavigationPlane::PlanarXY ? Double3{-3, 0, 11} : Double3{-3, 0, 0};
    const Double3 end = plane == NavigationPlane::PlanarXY ? Double3{3, 0, -8} : Double3{3, 2, 0};
    const auto route = query.route(start, end);
    check(route.path.status == NavStatus::Success && route.links.size() == 1 &&
              route.links.front().source == EntityId::parse(left) &&
              route.links.front().arc_height == link.arc_height &&
              query.path(end, start).status != NavStatus::Success,
          "Published link lost source identity, reachability or one-way direction");
    const auto actor_id =
        authoring_command(scene, "entity.create", {{"name", "Link traveler"}, {"kind", 4}})
            .at("selected")
            .get<std::string>();
    const auto projected = query.project(start);
    check(projected.status == NavStatus::Success, "Agent start did not project to the floor");
    scene.entity(actor_id)
        .set<LocalTranslation>(
            {projected.points[0][0], projected.points[0][1], projected.points[0][2]})
        .set<NavigationAgent>(
            {{baked.id}, true, true, 3, .05f, float(end[0]), float(end[1]), float(end[2])});
    const auto obstacle_id =
        authoring_command(scene, "entity.create", {{"name", "Endpoint blocker"}, {"kind", 4}})
            .at("selected")
            .get<std::string>();
    scene.entity(obstacle_id)
        .set<LocalTranslation>({2, plane == NavigationPlane::Spatial3D ? 2. : 0., 0})
        .set<NavigationObstacle>({false, 1, 2});
    if (character)
        character_jumps(scene.snapshot(), root, EntityId::parse(actor_id));
    else if (collision)
        collision_movement(scene.snapshot(), root, EntityId::parse(actor_id), plane,
                           link.arc_height);
    else {
        movement(scene.snapshot(), root, EntityId::parse(actor_id), baked, untouched,
                 EntityId::parse(left), projected.points[0], plane, link.arc_height);
        link_lifecycle(scene.snapshot(), root, EntityId::parse(actor_id), baked, untouched,
                       EntityId::parse(left), EntityId::parse(obstacle_id), projected.points[0],
                       dynamic);
    }
    const auto digest = navigation_geometry_digest(root, effective, baked.metadata);
    link.end_x += .1f;
    scene.entity(left).set<NavigationLink>(link);
    const auto edited = scene.effective_document();
    check(navigation_geometry_digest(root, edited, baked.metadata) != digest &&
              navigation_geometry_digest(root, edited, untouched.metadata) ==
                  untouched.metadata.at("geometry_sha256").get<std::string>(),
          "Link edit did not stale only its selected profile");
    link.enabled = false;
    scene.entity(left).set<NavigationLink>(link);
    check(source_digest(capture(scene.effective_document(), plane, settings.profile),
                        Json::array()) == before_digest,
          "Disabling a link did not restore the exact old no-link source digest");
    link.enabled = true;
    link.plane = unsigned(plane == NavigationPlane::PlanarXY ? NavigationPlane::Spatial3D
                                                             : NavigationPlane::PlanarXY);
    scene.entity(left).set<NavigationLink>(link);
    check(source_digest(capture(scene.effective_document(), plane, settings.profile),
                        Json::array()) == before_digest,
          "Unselected-plane link changed source provenance");
    if (collision && arc) {
        // Continue through a second, taller arc in one tick. Its bounded sweeps
        // must not discard a continuation on the first arc if that tick fails.
        link.plane = unsigned(plane);
        scene.entity(left).set(link);
        const auto third =
            authoring_command(scene, "entity.create", {{"name", "Third island"}, {"kind", 4}})
                .at("selected")
                .get<std::string>();
        auto island = scene.entity(third);
        island.set<LocalTranslation>({9, plane == NavigationPlane::Spatial3D ? 2. : 0., 0});
        if (plane == NavigationPlane::PlanarXY)
            island.set<NavigationRegion2D>({true, false, 4, 6});
        else
            island.set<Primitive>({3}).set<LocalScale>({4, 1, 6}).set<NavigationSurface>({});
        NavigationLink second;
        second.plane = unsigned(plane);
        second.profile = settings.profile;
        second.arc_height = 10;
        second.start_x = 4;
        second.end_x = 10;
        second.start_y = second.end_y = plane == NavigationPlane::Spatial3D ? 2.f : 0.f;
        const auto owner = authoring_command(scene, "entity.create", {{"name", "Second arc"}})
                               .at("selected")
                               .get<std::string>();
        scene.entity(owner).set<LocalTranslation>({0, 0, 0}).set(second);
        const auto extended = publish(settings);
        auto agent = scene.entity(actor_id).get<NavigationAgent>();
        agent.navmesh = {extended.id};
        agent.destination_x = 9;
        scene.entity(actor_id).set(agent);
        collision_budget_continuation(scene.snapshot(), root, EntityId::parse(actor_id));
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3 || argc == 4,
              "Need scratch root, matching navigation worker and optional profile");
        const std::string_view profile = argc == 4 ? argv[3] : "all";
        const bool collision = profile == "collision-straight" || profile == "collision-arc";
        const bool character = profile == "character-jump";
        check(profile == "all" || profile == "straight" || profile == "arc" || collision ||
                  character,
              "Unknown navigation pipeline profile");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        for (auto plane : {NavigationPlane::Spatial3D, NavigationPlane::PlanarXY})
            for (bool dynamic : {false, true})
                for (bool arc : {false, true}) {
                    if (character && (plane != NavigationPlane::Spatial3D || dynamic || !arc))
                        continue;
                    if (((profile == "straight" || profile == "collision-straight") && arc) ||
                        ((profile == "arc" || profile == "collision-arc") && !arc))
                        continue;
                    const auto label = std::to_string(unsigned(plane)) +
                                       (dynamic ? "-dynamic" : "-static") +
                                       (arc ? "-arc" : "-straight");
                    std::cout << "Navigation pipeline " << label << " begin" << std::endl;
                    run(root / label, std::filesystem::absolute(argv[2]), plane, dynamic, arc,
                        collision, character);
                    std::cout << "Navigation pipeline " << label << " PASS" << std::endl;
                }
        std::filesystem::remove_all(root);
        std::cout
            << "Authored link capture, profile isolation, straight/arc publication and native "
               "route ("
            << profile << ") PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
