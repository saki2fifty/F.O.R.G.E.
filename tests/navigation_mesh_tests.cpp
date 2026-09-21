#include "navigation_asset.hpp"
#include "navigation_geometry.hpp"
#include <DetourNavMesh.h>
#include <cmath>
#include <cstring>
#include <forge/engine_assets.hpp>
#include <iostream>
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
int main() {
    try {
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
