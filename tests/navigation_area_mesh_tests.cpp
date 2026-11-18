#include "navigation_area_value.hpp"
#include "navigation_asset.hpp"
#include "navigation_geometry.hpp"
#include <DetourNavMesh.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
using namespace forge;
using namespace forge::navigation_detail;
using Json = nlohmann::json;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F call) {
    try {
        call();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid area mesh was admitted");
}
Json source(NavigationPlane plane) {
    Json doc = {{"asset_id", AssetId::generate()}, {"entities", Json::array()}};
    for (auto size : {Double3{18, 1, 12}, Double3{3, 1, 6}}) {
        const auto planar = plane == NavigationPlane::PlanarXY;
        Json components = planar ? Json{{"forge.navigation_region_2d",
                                         {{"enabled", true},
                                          {"obstacle", false},
                                          {"width", size[0]},
                                          {"height", size[2]}}}}
                                 : Json{{"forge.navigation_surface", {{"enabled", true}}},
                                        {"forge.primitive", {{"kind", 3}}}};
        LocalTransform transform;
        if (!planar)
            transform.scale = {float(size[0]), 1, float(size[2])};
        doc["entities"].push_back({{"id", EntityId::generate()},
                                   {"spatial_resolved", true},
                                   {"world_affine", affine_transform(transform).m},
                                   {"components", std::move(components)}});
    }
    return doc;
}
void exercise(NavigationPlane plane, bool dynamic) {
    NavigationSettings settings;
    settings.plane = plane;
    settings.dynamic_obstacles = dynamic;
    auto doc = source(plane);
    const auto ordinary = geometry(capture(doc, settings));
    const auto legacy = build_tile(ordinary, settings);
    doc["entities"][1]["components"]["forge.navigation_area"] = {{"area", 0}};
    const auto zero = geometry(capture(doc, settings));
    check(zero.digest == ordinary.digest && build_tile(zero, settings) == legacy &&
              !zero.classified && zero.areas.empty(),
          "Explicit area zero changed legacy geometry or native bytes");
    doc["entities"][1]["components"]["forge.navigation_area"]["area"] = 2;
    const auto captured = capture(doc, settings);
    const auto g = geometry(captured);
    check(g.classified && g.areas.size() == g.indices.size() / 3 && g.digest != zero.digest,
          "Classification did not reach triangle data and source staleness");
    const auto tile = build_tile(g, settings);
    Json meta = {{"version", 9},
                 {"geometry_version", plane == NavigationPlane::PlanarXY ? 3 : 2},
                 {"asset_id", AssetId::generate()},
                 {"source_scene", g.scene},
                 {"sources", g.sources},
                 {"geometry_sha256", g.digest},
                 {"settings", settings},
                 {"mesh_inputs", Json::array()},
                 {"area_sources", area_sources(captured)}};
    const auto bytes = envelope(meta, tile);
    const auto loaded = admit(bytes);
    check(loaded.metadata.at("version").get<unsigned>() == 9 &&
              loaded.metadata.at("area_sources") == area_sources(captured),
          "Classified envelope lost immutable provenance");
    Query query(loaded.mesh), independent(loaded.mesh);
    const auto planar = plane == NavigationPlane::PlanarXY;
    const auto position = [&](double x, double horizontal) {
        return planar ? Double3{x, horizontal, 7} : Double3{x, 0, horizontal};
    };
    const auto start = position(-7, 0), end = position(7, 0), center = position(0, 0);
    NavigationAreaFilter only_default, only_two, none, expensive;
    only_default.mask = 1;
    only_two.mask = 4;
    none.mask = 0;
    expensive.costs[2] = 20;
    check(query.project(center, only_two).status == NavStatus::Success &&
              query.project(center, only_default).status == NavStatus::StartOutside,
          "The default raster erased the overlapping classified surface");
    check(query.project(center, none).status == NavStatus::StartOutside &&
              query.path(start, end, only_two).status == NavStatus::StartOutside,
          "Projection/path did not honor the caller's allowed-area mask");
    const auto direct = query.path(start, end);
    const auto detour = query.path(start, end, expensive);
    const auto excluded = query.path(start, end, only_default);
    const auto around = [&](const NavResult& result) {
        return result.status == NavStatus::Success &&
               std::ranges::any_of(result.points,
                                   [&](auto p) { return std::abs(p[planar ? 1 : 2]) >= 2.8; });
    };
    check(direct.status == NavStatus::Success && direct.points.size() == 2 && around(detour) &&
              around(excluded),
          "Costs and exclusion did not select a real alternative route");
    check(query.path(start, end).points == direct.points &&
              independent.path(start, end).points == direct.points,
          "A filtered query contaminated another request or mesh consumer");
    check(query.move(center, center, only_default).status == NavStatus::StartOutside,
          "Constrained movement ignored the applicable policy");
    if (planar)
        for (const auto& point : detour.points)
            check(point[2] == 7, "Classified XY routing lost visual depth");
    if (dynamic) {
        auto layers = decode_layers(tile);
        check(layers.classified && encode_layers(layers) == tile,
              "Classified raw layers did not round trip canonically");
        auto downgraded = layers;
        downgraded.classified = false;
        rejects([&] { encode_layers(downgraded); });
        auto bad = layers;
        auto& cells = bad.layers.front().cells;
        constexpr auto grid = layer_width * layer_width;
        auto occupied = std::find_if(cells.begin() + grid, cells.begin() + grid * 2,
                                     [](auto area) { return area != 0; });
        check(occupied != cells.begin() + grid * 2, "Fixture has no occupied area cell");
        *occupied = 17;
        rejects([&] { encode_layers(bad); });
        const CylinderObstacle obstacle{EntityId::generate(), {0, -1, 4}, .75f, 4};
        check(query.obstacles(std::span(&obstacle, 1)) && query.tile_rebuilds() != 0 &&
                  query.project(center, only_two).status == NavStatus::Success &&
                  query.project(center, only_default).status == NavStatus::StartOutside,
              "Dynamic carving discarded polygon classification");
        check(query.obstacles({}) && query.path(start, end).points == direct.points,
              "Obstacle removal did not restore the classified route");
        auto old_profile = meta;
        old_profile["version"] = 4;
        old_profile.erase("area_sources");
        rejects([&] { envelope(old_profile, tile); });
        rejects([&] { Mesh(tile, plane, true); });
    } else {
        rejects([&] { validate_tile(tile, plane); });
        auto damaged = tile;
        dtMeshHeader header;
        std::memcpy(&header, damaged.data(), sizeof(header));
        const auto align4 = [](std::size_t size) { return (size + 3) & ~std::size_t(3); };
        const auto offset = align4(sizeof(header)) + align4(sizeof(float) * header.vertCount * 3);
        dtPoly polygon;
        std::memcpy(&polygon, damaged.data() + offset, sizeof(polygon));
        polygon.flags = 0xffff;
        std::memcpy(damaged.data() + offset, &polygon, sizeof(polygon));
        rejects([&] { validate_tile(damaged, plane, {}, true); });
        auto old_profile = meta;
        old_profile["version"] = planar ? 3 : 2;
        old_profile.erase("area_sources");
        rejects([&] { envelope(old_profile, tile); });
    }
    for (const auto& invalid : {Json(-1), Json(0), Json(16), Json(2.5), Json(true)}) {
        auto damaged = meta;
        damaged["area_sources"][0]["area"] = invalid;
        rejects([&] { envelope(damaged, tile); });
    }
    auto repeated = meta;
    repeated["area_sources"].push_back(repeated["area_sources"][0]);
    rejects([&] { envelope(repeated, tile); });
    repeated = meta;
    repeated["area_sources"][0]["entity"] = EntityId::generate();
    rejects([&] { envelope(repeated, tile); });
    repeated = meta;
    repeated["area_sources"] = Json::array();
    rejects([&] { envelope(repeated, tile); });
    // All sixteen flags, including the high bit, are usable independently.
    doc["entities"][1]["components"]["forge.navigation_area"]["area"] = 15;
    const auto high = geometry(capture(doc, settings));
    Query high_query(std::make_shared<Mesh>(build_tile(high, settings), plane, dynamic,
                                            std::span<const NavigationLinkData>{}, true));
    NavigationAreaFilter high_only;
    high_only.mask = 1u << 15;
    check(high_query.project(center, high_only).status == NavStatus::Success &&
              high_query.project(center, only_two).status == NavStatus::StartOutside,
          "The highest navigation class was truncated or merged with another class");
    std::cout << (planar ? "XY" : "3D") << (dynamic ? " tiled" : " static")
              << " classification/costs/exclusion/isolation/admission passed\n";
}
} // namespace
int main() {
    try {
        for (auto plane : {NavigationPlane::Spatial3D, NavigationPlane::PlanarXY})
            for (bool dynamic : {false, true})
                exercise(plane, dynamic);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
