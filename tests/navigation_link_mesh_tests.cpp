#include "navigation_asset.hpp"
#include "navigation_geometry.hpp"
#include "navigation_mesh.hpp"
#include <DetourNavMesh.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
using namespace forge;
using namespace forge::navigation_detail;
namespace {
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F call) {
    try {
        call();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid navigation link data was admitted");
}
Geometry islands(bool xy) {
    auto doc =
        nlohmann::json{{"asset_id", AssetId::generate()}, {"entities", nlohmann::json::array()}};
    for (const double x : {-3., 3.}) {
        LocalTransform transform{{x, 0, 0}, {}, {4, 1, 6}};
        nlohmann::json components{{"forge.primitive", {{"kind", 3}}},
                                  {"forge.navigation_surface", {{"enabled", true}}}};
        if (xy) {
            transform.scale = {1, 1, 1};
            components = {{"forge.navigation_region_2d",
                           {{"enabled", true}, {"obstacle", false}, {"width", 4}, {"height", 6}}}};
        }
        doc["entities"].push_back({{"id", EntityId::generate()},
                                   {"spatial_resolved", true},
                                   {"world_affine", affine_transform(transform).m},
                                   {"components", components}});
    }
    return geometry(capture(doc, xy ? MeshSpace::PlanarXY : MeshSpace::Spatial3D),
                    nlohmann::json::array(), [](AssetId) -> const MeshResourceData& {
                        throw std::runtime_error("Island fixture has no external Mesh");
                    });
}
void exercise(bool xy, bool dynamic) {
    const auto space = xy ? MeshSpace::PlanarXY : MeshSpace::Spatial3D;
    auto geometry = islands(xy);
    geometry.connections.push_back({EntityId::generate(), {-2, 0, 0}, {2, 0, 0}, .5f, false});
    NavigationSettings settings;
    settings.plane = space;
    settings.radius = .2f;
    settings.cell_size = dynamic ? .2f : .1f;
    settings.dynamic_obstacles = dynamic;
    const auto data = [&] {
        return dynamic ? encode_layers(build_layers(geometry, settings))
                       : build_tile(geometry, settings);
    };
    const auto bytes = data();
    auto identities = geometry.sources;
    identities.push_back(geometry.connections[0].entity);
    auto meta = nlohmann::json{{"asset_id", AssetId::generate()},
                               {"source_scene", geometry.scene},
                               {"version", 6},
                               {"geometry_version", xy ? 3 : 2},
                               {"mesh_inputs", nlohmann::json::array()},
                               {"sources", identities},
                               {"geometry_sha256", geometry.digest},
                               {"settings", settings},
                               {"links", navigation_link_values(geometry.connections, space)}};
    const auto admitted = admit(envelope(meta, bytes));
    check(admitted.metadata.at("version") == 6 && admitted.mesh->space() == space &&
              admitted.mesh->dynamic_obstacles() == dynamic,
          "Linked envelope lost its coordinate/dynamic contract");
    auto downgraded = meta;
    downgraded["version"] = dynamic ? 4 : (xy ? 3 : 2);
    rejects([&] { (void)envelope(downgraded, bytes); });
    auto empty = meta;
    empty["links"] = nlohmann::json::array();
    rejects([&] { (void)envelope(empty, bytes); });
    auto missing = meta;
    missing["sources"] = geometry.sources;
    rejects([&] { (void)admit(envelope(missing, bytes)); });
    if (dynamic) {
        auto finer = settings;
        finer.cell_size = .1f;
        const auto nonadjacent = encode_layers(build_layers(geometry, finer));
        try {
            (void)Mesh(nonadjacent, space, true, geometry.connections);
            throw std::runtime_error("Nonadjacent dynamic link was accepted");
        } catch (const std::exception& error) {
            check(std::string_view(error.what()).find("same or adjoining") !=
                      std::string_view::npos,
                  "Unsupported dynamic tile span lacked its specific diagnostic");
        }
    }
    if (!dynamic) {
        rejects([&] { validate_tile(bytes, space); });
        validate_tile(bytes, space, geometry.connections);
        for (unsigned mutation = 0; mutation < 6; ++mutation) {
            auto corrupt = bytes;
            dtOffMeshConnection link;
            const auto at = corrupt.size() - sizeof(link);
            std::memcpy(&link, corrupt.data() + at, sizeof(link));
            switch (mutation) {
            case 0:
                link.userId = 2;
                break;
            case 1:
                link.rad = 0;
                break;
            case 2:
                link.side = 0;
                break;
            case 3:
                link.flags = 2;
                break;
            case 4:
                link.pos[0] += 1;
                break;
            default:
                link.pos[2] = std::numeric_limits<float>::quiet_NaN();
                break;
            }
            std::memcpy(corrupt.data() + at, &link, sizeof(link));
            rejects([&] { validate_tile(corrupt, space, geometry.connections); });
        }
        auto wrong = geometry.connections;
        wrong[0].bidirectional = true;
        rejects([&] { validate_tile(bytes, space, wrong); });
    }
    auto mesh = std::make_shared<Mesh>(bytes, space, dynamic, geometry.connections);
    Query query(mesh);
    const Double3 start{-4, 0, xy ? 37.125 : 0.}, end{4, 0, xy ? -91. : 0.};
    const auto route = query.route(start, end);
    const auto& forward = route.path;
    check(forward.status == NavStatus::Success && forward.points.size() >= 4,
          "Directed link did not connect both walkable islands");
    bool start_corner = false, end_corner = false;
    for (const auto& point : forward.points) {
        start_corner |= std::abs(point[0] + 2) < .001;
        end_corner |= std::abs(point[0] - 2) < .001;
        check(!xy || point[2] == start[2], "XY link route changed the caller's visual depth");
    }
    check(start_corner && end_corner, "Link route omitted a traversal boundary");
    check(route.links.size() == 1 && route.links[0].source == geometry.connections[0].entity &&
              route.links[0].exit == route.links[0].entry + 1 &&
              forward.points[route.links[0].entry][0] == -2 &&
              forward.points[route.links[0].exit][0] == 2,
          "Agent route lost source identity or its explicit traversal segment");
    check(query.path(end, start).status != NavStatus::Success,
          "One-way link allowed reverse routing");
    const auto boundary =
        query.route(forward.points[route.links[0].entry], forward.points[route.links[0].exit]);
    check(boundary.path.status == NavStatus::Success && boundary.links.size() == 1,
          "Exact entry/exit query lost its link span: " +
              std::string(nav_status_name(boundary.path.status)) + " / " +
              std::to_string(boundary.links.size()));
    const auto entry = forward.points[route.links[0].entry],
               exit = forward.points[route.links[0].exit];
    check(query.link_available(route.links[0].source, entry, exit) &&
              !query.link_available(route.links[0].source, exit, entry),
          "Direct connection validation lost its admitted one-way orientation");
    if (dynamic) {
        const std::array obstacles{CylinderObstacle{EntityId::generate(), {-2, 0, 0}, 1, 2}};
        check(query.obstacles(obstacles), "Link endpoint obstacle was not adopted");
        check(query.path(start, end).status != NavStatus::Success &&
                  !query.link_available(route.links[0].source, entry, exit),
              "Carved link endpoint retained an impossible route or admitted span");
        check(query.obstacles({}) && query.path(start, end).status == NavStatus::Success,
              "Removing an endpoint obstacle did not restore its link");
    }
    geometry.connections[0].bidirectional = true;
    auto both = std::make_shared<Mesh>(data(), space, dynamic, geometry.connections);
    const auto reverse = Query(both).route(end, start);
    check(reverse.path.status == NavStatus::Success && reverse.links.size() == 1 &&
              reverse.path.points[reverse.links[0].entry][0] == 2 &&
              reverse.path.points[reverse.links[0].exit][0] == -2,
          "Bidirectional link did not enable the return route");
    const auto original = geometry.connections;
    auto parallel = original[0];
    do {
        parallel.entity = EntityId::generate();
    } while (parallel.entity == original[0].entity);
    geometry.connections.push_back(parallel);
    std::sort(geometry.connections.begin(), geometry.connections.end(),
              [](const auto& left, const auto& right) { return left.entity < right.entity; });
    auto parallel_mesh = std::make_shared<Mesh>(data(), space, dynamic, geometry.connections);
    Query parallel_query(parallel_mesh);
    const auto chosen = parallel_query.route(entry, exit);
    check(chosen.path.status == NavStatus::Success && chosen.links.size() == 1,
          "Parallel connection route is unavailable");
    bool alternative = false;
    for (const auto& link : geometry.connections) {
        check(parallel_query.link_available(link.entity, entry, exit) &&
                  parallel_query.link_available(link.entity, exit, entry),
              "Validating an admitted connection incorrectly depended on which parallel route won");
        alternative |= link.entity != chosen.links[0].source;
    }
    auto spoofed_entry = entry;
    spoofed_entry[0] += .1;
    check(alternative && !parallel_query.link_available(EntityId::generate(), entry, exit) &&
              !parallel_query.link_available(original[0].entity, spoofed_entry, exit),
          "Connection identity or endpoint validation accepted a spoofed span");
    geometry.connections = original;
    geometry.connections[0].end = {0, 0, 0};
    const auto orphan = data();
    rejects([&] { (void)Mesh(orphan, space, dynamic, geometry.connections); });
}
} // namespace
int main() {
    try {
        for (const bool xy : {false, true})
            for (const bool dynamic : {false, true}) {
                try {
                    exercise(xy, dynamic);
                } catch (const std::exception& error) {
                    throw std::runtime_error(std::string(xy ? "XY" : "3D") +
                                             (dynamic ? " dynamic: " : " static: ") + error.what());
                }
            }
        std::cout << "Native link admission/direction/endpoint attachment/dynamic rebuild on both "
                     "planes PASS; Agent traversal pending\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
