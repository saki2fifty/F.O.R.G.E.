#include "navigation_asset.hpp"
#include "navigation_geometry.hpp"
#include <DetourNavMesh.h>
#include <cmath>
#include <cstring>
#include <forge/engine_assets.hpp>
#include <iostream>
#include <limits>
using namespace forge;
using Json = nlohmann::json;
using namespace forge::navigation_detail;
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid fixture was admitted");
}
Json scene(bool obstacle = true) {
    Json doc = {{"asset_id", AssetId::generate()}, {"entities", Json::array()}};
    auto add = [&](unsigned kind, Double3 at, Double3 scale) {
        LocalTransform t{
            {at[0], at[1], at[2]}, {}, {float(scale[0]), float(scale[1]), float(scale[2])}};
        doc["entities"].push_back({{"id", EntityId::generate()},
                                   {"spatial_resolved", true},
                                   {"world_affine", affine_transform(t).m},
                                   {"components",
                                    {{"forge.primitive", {{"kind", kind}}},
                                     {"forge.navigation_surface", {{"enabled", true}}}}}});
    };
    add(3, {0, 0, 0}, {20, 1, 20});
    if (obstacle)
        add(0, {0, 2, 0}, {2, 4, 4});
    return doc;
}
#include "navigation_static_mesh_tests.hpp"
void planar_region_tests() {
    auto doc = Json{{"asset_id", AssetId::generate()}, {"entities", Json::array()}};
    const auto add = [&](float width, float height, bool obstacle, LocalTransform transform = {}) {
        const auto id = EntityId::generate();
        doc["entities"].push_back({{"id", id},
                                   {"spatial_resolved", true},
                                   {"world_affine", affine_transform(transform).m},
                                   {"components",
                                    {{"forge.navigation_region_2d",
                                      {{"enabled", true},
                                       {"obstacle", obstacle},
                                       {"width", width},
                                       {"height", height}}}}}});
    };
    add(20, 20, false);
    add(2, 4, true);
    const auto source = capture(doc, NavigationPlane::PlanarXY);
    const auto g = geometry(source);
    check(g.vertices.size() == 18 && g.obstacles.size() == 1 && source.meshes.empty(),
          "XY regions did not separate walkable input from obstacle masks");
    NavigationSettings settings;
    settings.plane = NavigationPlane::PlanarXY;
    const auto tile = build_tile(g, settings);
    auto meta = Json{
        {"asset_id", AssetId::generate()}, {"source_scene", g.scene},     {"sources", g.sources},
        {"geometry_sha256", g.digest},     {"settings", settings},        {"version", 3},
        {"geometry_version", 3},           {"mesh_inputs", Json::array()}};
    const auto file = envelope(meta, tile);
    check(admit(file).mesh->space() == MeshSpace::PlanarXY,
          "Planar asset did not retain its explicit query basis");
    // Forge metadata/header together, preserving the genuine tile and its checksum.
    // Admission must reject incompatible semantics independently of the writer.
    const auto forged = [&](const Json& replacement) {
        const auto text = replacement.dump();
        auto bytes = std::vector<std::byte>(file.begin(), file.begin() + 20);
        const auto version = replacement.at("version").get<unsigned>();
        for (unsigned i = 0; i < 4; ++i) {
            bytes[8 + i] = std::byte((version >> (8 * i)) & 255);
            bytes[12 + i] = std::byte((text.size() >> (8 * i)) & 255);
        }
        const auto encoded = std::as_bytes(std::span(text));
        bytes.insert(bytes.end(), encoded.begin(), encoded.end());
        bytes.insert(bytes.end(), tile.begin(), tile.end());
        return bytes;
    };
    const auto canonical = admit(file).metadata;
    for (unsigned version : {1u, 2u}) {
        auto invalid = canonical;
        invalid["version"] = version;
        invalid["geometry_version"] = version;
        rejects([&] { admit(forged(invalid)); });
    }
    for (const auto& key : {"plane", "geometry_version", "mesh_inputs"}) {
        auto invalid = canonical;
        if (std::string_view(key) == "plane")
            invalid["settings"].erase(key);
        else
            invalid.erase(key);
        rejects([&] { admit(forged(invalid)); });
    }
    auto invalid = canonical;
    invalid["mesh_inputs"].push_back(
        {{"asset_id", AssetId::generate()}, {"revision", std::string(64, 'a')}});
    rejects([&] { admit(forged(invalid)); });
    invalid = canonical;
    invalid["geometry_version"] = 3.;
    rejects([&] { admit(forged(invalid)); });
    Query query(std::make_shared<Mesh>(tile, MeshSpace::PlanarXY));
    const auto path = query.path({-8, 0, 7}, {8, 0, -19});
    check(path.status == NavStatus::Success && path.points.size() > 2,
          "XY region obstacle did not create a routed opening");
    bool around = false;
    for (const auto& point : path.points) {
        check(point[2] == 7, "Region path lost visual depth");
        around |= std::abs(point[1]) > 2.3;
    }
    check(around, "XY route crossed the obstacle region");
    check(query.project({0, 0, 7}).status == NavStatus::StartOutside,
          "Obstacle center remained navigable");
    auto depth_only = doc;
    depth_only["entities"][0]["world_affine"][11] = 19.;
    check(geometry(capture(depth_only, NavigationPlane::PlanarXY)).digest == g.digest,
          "Visual Z reordered XY region provenance");
    auto changed = doc;
    changed["entities"][1]["components"]["forge.navigation_region_2d"]["obstacle"] = false;
    check(geometry(capture(changed, NavigationPlane::PlanarXY)).digest != g.digest,
          "Region role change did not invalidate geometry");
    for (auto invalid : {Json(-1), Json(2), Json(.5), Json("xy")}) {
        auto value = Json(settings);
        value["plane"] = invalid;
        rejects([&] { (void)value.get<NavigationSettings>(); });
    }
    check(Json(NavigationSettings{}).size() == 6 && !Json(NavigationSettings{}).contains("plane"),
          "Default3D settings changed their legacy JSON representation");
    for (float sign : {-1.f, 1.f}) {
        auto reflected = doc;
        reflected["entities"][1]["world_affine"] = affine_transform({{}, {}, {sign, 1, 0}}).m;
        Query same(std::make_shared<Mesh>(
            build_tile(geometry(capture(reflected, NavigationPlane::PlanarXY)), settings),
            MeshSpace::PlanarXY));
        check(same.path({-8, 0, 7}, {8, 0, -19}).points == path.points,
              "Reflected or zero-depth region changed its mask footprint");
    }
    auto bad = g;
    bad.obstacles[0][3] = bad.obstacles[0][0];
    bad.obstacles[0][5] = bad.obstacles[0][2];
    rejects([&] { build_tile(bad, settings); });
    bad = g;
    bad.obstacles[0][1] = 1;
    rejects([&] { build_tile(bad, settings); });
    rejects([&] { build_tile(g, {}); });
    auto wrong = doc;
    wrong["entities"][0]["components"]["forge.character_controller_2d"] = {{"enabled", true}};
    rejects([&] { capture(wrong, NavigationPlane::PlanarXY); });
    std::cout << "XY rectangular source/mask/provenance/settings PASS\n";
}
void planar_space_tests() {
    for (double altitude : {0., 7.}) {
        auto doc = scene(false);
        doc["entities"][0]["world_affine"] =
            affine_transform({{0, altitude, 0}, {}, {20, 1, 20}}).m;
        const auto tile = build_tile(geometry(doc), {});
        const auto mesh = std::make_shared<Mesh>(tile, MeshSpace::PlanarXY);
        check(mesh->space() == MeshSpace::PlanarXY, "Mesh lost its explicit coordinate space");
        Query xy(mesh);
        const auto projected = xy.project({-8, 2, 37.125});
        check(projected.status == NavStatus::Success &&
                  std::abs(projected.points[0][0] + 8) < .001 &&
                  std::abs(projected.points[0][1] - 2) < .001 && projected.points[0][2] == 37.125,
              "XY projection confused navigation height, logical Y or visual depth");
        const auto path = xy.path({-8, -2, 37.125}, {8, 2, -19});
        check(path.status == NavStatus::Success && path.points.size() >= 2 &&
                  path.points.front()[1] < -1.9 && path.points.back()[1] > 1.9,
              "XY path did not use world XY endpoints");
        for (const auto& point : path.points)
            check(point[2] == 37.125, "XY path changed the starting visual depth");
        const auto moved = xy.move({-8, -2, 37.125}, {-7, 0, -19});
        check(moved.status == NavStatus::Success && moved.points[0][2] == 37.125 &&
                  std::abs(moved.points[0][0] + 7) < .001 && std::abs(moved.points[0][1]) < .001,
              "XY surface movement used destination Z or lost logical Y travel");
        for (const auto& point : mesh->triangles())
            check(point[2] == 0 && std::abs(point[1]) <= 10,
                  "XY overlay triangles leaked native vertical altitude");
        check(xy.project({0, 100, 7}).status == NavStatus::StartOutside &&
                  xy.project({0, 0, std::numeric_limits<double>::infinity()}).status ==
                      NavStatus::Invalid,
              "XY projection bypassed world coordinate validation");
        const auto free = xy.avoid_velocity({-3, 0, 7}, {2, 0, 0}, {}, 2, .4f, 2, {});
        const std::array<Query::Neighbour, 1> neighbours{{{{-1, 0, -19}, {-2, 0, 0}}}};
        const auto avoiding = xy.avoid_velocity({-3, 0, 7}, {2, 0, 0}, {}, 2, .4f, 2, neighbours);
        check(free.status == NavStatus::Success && avoiding.status == NavStatus::Success &&
                  free.points[0][2] == 0 && avoiding.points[0][2] == 0 &&
                  std::hypot(avoiding.points[0][0], avoiding.points[0][1]) <= 2.00001 &&
                  (avoiding.points[0][0] < free.points[0][0] - .05 ||
                   std::abs(avoiding.points[0][1]) > .05),
              "XY avoidance ignored logical neighbours at different visual depths");
        const auto vertical = xy.avoid_velocity({0, -3, 7}, {0, 2, 0}, {}, 2, .4f, 2, {});
        check(vertical.status == NavStatus::Success && vertical.points[0][1] > 1 &&
                  vertical.points[0][2] == 0,
              "XY avoidance discarded world Y velocity");
        rejects([&] { Mesh invalid(tile, static_cast<MeshSpace>(99)); });
        auto changed = tile;
        float height;
        std::memcpy(&height, changed.data() + sizeof(dtMeshHeader) + sizeof(float), sizeof height);
        height += .01f;
        std::memcpy(changed.data() + sizeof(dtMeshHeader) + sizeof(float), &height, sizeof height);
        rejects([&] { Mesh invalid(changed, MeshSpace::PlanarXY); });
    }
    const auto raised_obstacle = build_tile(geometry(scene(true)), {});
    rejects([&] { Mesh invalid(raised_obstacle, MeshSpace::PlanarXY); });
    std::cout << "Explicit XY native query/move/avoidance/depth/flat-admission PASS\n";
}
int main() {
    try {
        static_mesh_geometry_tests();
        planar_space_tests();
        planar_region_tests();
        for (const auto scale : {LocalScale{-20, 1, 20}, LocalScale{20, 0, 20}}) {
            auto signed_floor = scene(false);
            signed_floor["entities"][0]["world_affine"] = affine_transform({{}, {}, scale}).m;
            Query navigation(std::make_shared<Mesh>(build_tile(geometry(signed_floor), {})));
            check(navigation.path({-8, .1, 0}, {8, .1, 0}).status == NavStatus::Success,
                  "Mirrored/collapsed-height navigation plane lost winding");
        }
        auto collapsed = scene(false);
        collapsed["entities"][0]["world_affine"] = affine_transform({{}, {}, {0, 0, 0}}).m;
        rejects([&] { geometry(collapsed); });
        auto doc = scene();
        auto g = geometry(doc);
        auto mesh_sources = doc;
        for (auto& row : mesh_sources["entities"]) {
            auto& c = row["components"];
            c["forge.mesh_renderer"] = {{"mesh", engine_primitive(c["forge.primitive"]["kind"]).id},
                                        {"enabled", true}};
            c.erase("forge.primitive");
        }
        const auto converted = geometry(mesh_sources);
        check(converted.vertices == g.vertices && converted.indices == g.indices &&
                  converted.digest == g.digest,
              "Built-in MeshRenderer navigation differs from legacy primitive geometry");
        auto unsupported_mesh = mesh_sources;
        unsupported_mesh["entities"][0]["components"]["forge.mesh_renderer"]["mesh"] =
            AssetId::generate();
        rejects([&] { geometry(unsupported_mesh); });
        auto tile = build_tile(g, {});
        auto mesh = std::make_shared<Mesh>(tile);
        Query query(mesh);
        auto path = query.path({-8, .1, 0}, {8, .1, 0});
        check(path.status == NavStatus::Success && path.points.size() > 2, "Obstacle route failed");
        double distance = 0;
        for (std::size_t i = 1; i < path.points.size(); ++i) {
            auto a = path.points[i - 1], b = path.points[i];
            distance += std::hypot(b[0] - a[0], b[2] - a[2]);
        }
        check(distance > 16.1, "Route crosses obstacle");
        check(query.project({-8, .1, 0}).status == NavStatus::Success, "Projection failed");
        check(query.path({-100, 0, 0}, {8, 0, 0}).status == NavStatus::StartOutside,
              "Outside start not diagnosed");
        check(query.path({-8, 0, 0}, {100, 0, 0}).status == NavStatus::EndOutside,
              "Outside destination not diagnosed");
        for (int i = 0; i < 100; ++i)
            check(query.path({-8, .1, 0}, {8, .1, 0}).points == path.points,
                  "Query scratch leaked");
        for (std::size_t n = 0; n < tile.size(); ++n)
            rejects([&] { validate_tile(std::span(tile).first(n)); });
        auto bad = tile;
        bad.push_back(std::byte{});
        rejects([&] { validate_tile(bad); });
        for (auto field : {&dtMeshHeader::polyCount, &dtMeshHeader::vertCount,
                           &dtMeshHeader::maxLinkCount, &dtMeshHeader::detailMeshCount,
                           &dtMeshHeader::detailVertCount, &dtMeshHeader::detailTriCount,
                           &dtMeshHeader::bvNodeCount, &dtMeshHeader::offMeshConCount}) {
            bad = tile;
            dtMeshHeader h;
            std::memcpy(&h, bad.data(), sizeof h);
            h.*field = 0x7fffffff;
            std::memcpy(bad.data(), &h, sizeof h);
            rejects([&] { validate_tile(bad); });
        }
        auto meta = Json{{"asset_id", AssetId::generate()},
                         {"source_scene", g.scene},
                         {"sources", g.sources},
                         {"geometry_sha256", g.digest},
                         {"settings", NavigationSettings{}}};
        auto file = envelope(meta, tile);
        check(admit(file).mesh->triangles().size() > 0, "Envelope load failed");
        // Change only the declared JSON range; leave the binary tile and its digest intact.
        const auto metadata_size = file.size() - 20 - tile.size();
        for (const auto& suffix : {std::string(1, '\0'), std::string("\0hidden", 7),
                                   std::string(1, '\x1a'), std::string(" {}")}) {
            auto incomplete = file;
            const auto bytes = std::as_bytes(std::span(suffix));
            incomplete.insert(incomplete.begin() + 20 + metadata_size, bytes.begin(), bytes.end());
            const auto count = metadata_size + suffix.size();
            for (unsigned i = 0; i < 4; ++i)
                incomplete[12 + i] = std::byte((count >> (8 * i)) & 255);
            rejects([&] { admit(incomplete); });
        }
        auto escaped = meta;
        escaped["note"] = std::string("valid\0text", 10);
        check(admit(envelope(escaped, tile)).metadata.at("note") == escaped.at("note"),
              "Escaped JSON NUL or binary tile bytes were rejected");
        for (auto n : {0u, 8u, 12u, 16u, 19u, unsigned(file.size() - 1)})
            rejects([&] { admit(std::span(file).first(n)); });
        bad = file;
        bad.back() ^= std::byte{1};
        rejects([&] { admit(bad); });
        rejects([&] { build_tile(g, {.4f, 2, .4f, 45, .001f, .1f}); });
        auto empty = doc;
        empty["entities"] = Json::array();
        rejects([&] { geometry(empty); });
        auto floor = geometry(scene(false));
        Query straight(std::make_shared<Mesh>(build_tile(floor, {})));
        check(straight.path({-8, .1, 0}, {8, .1, 0}).points.size() == 2,
              "Floor route not straight");

        auto separated = scene(false);
        auto first = separated["entities"][0];
        first["world_affine"][3] = -25;
        separated["entities"][0] = first;
        first["id"] = EntityId::generate();
        first["world_affine"][3] = 25;
        separated["entities"].push_back(first);
        Query islands(std::make_shared<Mesh>(build_tile(geometry(separated), {})));
        check(islands.path({-25, .1, 0}, {25, .1, 0}).status == NavStatus::Partial,
              "Disconnected region not diagnosed");
        auto ramp = scene(false);
        LocalTransform rt{{}, rotation_from_euler({0, 0, 15}), {20, 1, 20}};
        ramp["entities"][0]["world_affine"] = affine_transform(rt).m;
        Query incline(std::make_shared<Mesh>(build_tile(geometry(ramp), {})));
        auto uphill = incline.path({-7, -1.87, 0}, {7, 1.87, 0});
        check(uphill.status == NavStatus::Success &&
                  uphill.points.back()[1] - uphill.points.front()[1] > 3,
              "Ramp elevation lost");
        auto huge = scene(false);
        huge["entities"][0]["world_affine"][0] = 1000;
        rejects([&] { build_tile(geometry(huge), {}); });
        huge["entities"][0]["world_affine"][3] = 5000;
        rejects([&] { geometry(huge); });
        auto labyrinth = scene(false);
        labyrinth["entities"][0]["world_affine"][0] = 90;
        for (int i = 0; i < 40; ++i) {
            auto wall = doc["entities"][1];
            wall["id"] = EntityId::generate();
            wall["world_affine"] =
                affine_transform({{double(i) * 1.8 - 35, 2, i % 2 ? 3. : -3.}, {}, {.5f, 4, 14}}).m;
            labyrinth["entities"].push_back(wall);
        }
        Query maze(std::make_shared<Mesh>(build_tile(geometry(labyrinth), {})));
        check(maze.path({-42, .1, 0}, {42, .1, 0}).status == NavStatus::Limit,
              "Long route did not report bounded limit");
        dtMeshHeader header;
        std::memcpy(&header, tile.data(), sizeof header);
        const std::size_t polyAt = sizeof(header) + header.vertCount * 3 * sizeof(float);
        bad = tile;
        dtPoly poly;
        std::memcpy(&poly, bad.data() + polyAt, sizeof poly);
        poly.verts[0] = 65535;
        std::memcpy(bad.data() + polyAt, &poly, sizeof poly);
        rejects([&] { validate_tile(bad); });
        const std::size_t triAt = polyAt + header.polyCount * sizeof(dtPoly) +
                                  header.maxLinkCount * sizeof(dtLink) +
                                  header.detailMeshCount * sizeof(dtPolyDetail) +
                                  header.detailVertCount * 3 * sizeof(float);
        bad = tile;
        for (int i = 0; i < header.detailTriCount; ++i)
            bad[triAt + i * 4 + 3] = std::byte{};
        rejects([&] { validate_tile(bad); });
        auto ancestry = scene(false);
        auto mover = ancestry["entities"][0];
        mover["id"] = EntityId::generate();
        mover["components"].erase("forge.navigation_surface");
        mover["components"]["forge.physics_body"] = {{"motion", 2u}};
        ancestry["entities"][0]["parent"] = mover["id"];
        ancestry["entities"].push_back(mover);
        rejects([&] { geometry(ancestry); });
        ancestry["entities"][0]["spatial"] = {{"mode", "world"}};
        check(!geometry(ancestry).vertices.empty(), "World-bound static source rejected");
        ancestry["entities"][0]["spatial"] = {
            {"mode", "explicit"},
            {"target", {{"scene", ancestry["asset_id"]}, {"entity", mover["id"]}}}};
        rejects([&] { geometry(ancestry); });
        ancestry["entities"][1]["components"].erase("forge.physics_body");
        ancestry["entities"][1]["components"]["forge.navigation_agent"] = {{"enabled", true}};
        rejects([&] { geometry(ancestry); });
        // Two independent query contexts share one immutable asset without shared scratch.
        Query second(mesh);
        for (int i = 0; i < 20; ++i) {
            check(second.path({8, .1, 0}, {-8, .1, 0}).status == NavStatus::Success,
                  "Second query failed");
            check(query.path({-8, .1, 0}, {8, .1, 0}).points == path.points, "Shared mesh mutated");
        }
        std::cout << "Navigation admission/build/query passed: " << tile.size() << " bytes, "
                  << path.points.size() << " waypoints, " << distance << " metres\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
