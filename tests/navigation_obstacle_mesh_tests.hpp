#pragma once
#include "navigation_layers.hpp"

void tiled_surface_tests() {
    NavigationSettings settings;
    settings.dynamic_obstacles = true;
    auto slope = scene(false);
    slope["entities"][0]["world_affine"] =
        affine_transform({{}, rotation_from_euler({0, 0, 20}), {20, 1, 20}}).m;
    Query ramp(
        std::make_shared<Mesh>(build_tile(geometry(slope), settings), MeshSpace::Spatial3D, true));
    const auto rise = 8 * std::tan(20 * 3.141592653589793 / 180);
    const auto route = ramp.path({-8, -rise, 0}, {8, rise, 0});
    check(route.status == NavStatus::Success &&
              route.points.back()[1] - route.points.front()[1] > 5,
          "Tiled profile flattened or disconnected a sloped surface");
    auto stacked = scene(false);
    const auto floor = stacked["entities"][0];
    for (float y : {3.f, 6.f}) {
        auto extra = floor;
        extra["id"] = EntityId::generate();
        extra["world_affine"] = affine_transform({{0, y, 0}, {}, {20, 1, 20}}).m;
        stacked["entities"].push_back(std::move(extra));
    }
    const auto payload = build_tile(geometry(stacked), settings);
    check(decode_layers(payload).layers.size() == 48,
          "Stacked source did not retain three independent layers per tile");
    Query levels(std::make_shared<Mesh>(payload, MeshSpace::Spatial3D, true));
    std::array<CylinderObstacle, 1> shape{{{EntityId::generate(), {2.8f, -1, 2.8f}, 1.4f, 5.5f}}};
    levels.obstacles(shape);
    check(levels.tile_rebuilds() == 8 &&
              levels.path({-8, .1, 2.8}, {8, .1, 2.8}).points.size() > 2 &&
              levels.path({-8, 3.1, 2.8}, {8, 3.1, 2.8}).points.size() > 2 &&
              levels.path({-8, 6.1, 2.8}, {8, 6.1, 2.8}).points.size() == 2,
          "Eight-layer obstacle failed its exact bound or carved an unrelated floor");
    shape[0].height = 10;
    rejects([&] { levels.obstacles(shape); });
    check(levels.path({-8, 6.1, 2.8}, {8, 6.1, 2.8}).status == NavStatus::Unavailable,
          "An oversized multilayer snapshot left an old route exposed");
    auto tiny = scene(false);
    tiny["entities"][0]["world_affine"] = affine_transform({{}, {}, {2, 1, 2}}).m;
    Query sparse(
        std::make_shared<Mesh>(build_tile(geometry(tiny), settings), MeshSpace::Spatial3D, true));
    std::array<CylinderObstacle, 1> huge{{{EntityId::generate(), {0, -1, 0}, 100, 4}}};
    // Only one real layer overlaps. The native empty-coordinate scan must still
    // be bounded before registration, independently of the eight-layer limit.
    rejects([&] { sparse.obstacles(huge); });
    check(sparse.path({-.4, .1, 0}, {.4, .1, 0}).status == NavStatus::Unavailable,
          "Oversized sparse-world footprint left a derivative available");
    std::cout << "Tiled slopes/stacked floors/exact-eight-layer/sparse-scan bounds PASS\n";
}
void obstacle_mesh_tests() {
    tiled_surface_tests();
    for (auto plane : {NavigationPlane::Spatial3D, NavigationPlane::PlanarXY}) {
        NavigationSettings settings;
        settings.plane = plane;
        settings.dynamic_obstacles = true;
        auto g = geometry(scene(false));
        const auto payload = build_tile(g, settings);
        const auto layers = decode_layers(payload);
        check(layers.layers.size() > 8 && encode_layers(layers) == payload,
              "Tiled navigation did not round-trip its bounded raw cells");
        auto base = std::make_shared<Mesh>(payload, plane, true);
        Query query(base), independent(base);
        const Double3 start =
            plane == NavigationPlane::PlanarXY ? Double3{-8, 0, 7} : Double3{-8, .1, 0};
        const Double3 end =
            plane == NavigationPlane::PlanarXY ? Double3{8, 0, -19} : Double3{8, .1, 0};
        const auto original = query.path(start, end);
        check(original.status == NavStatus::Success && original.points.size() == 2,
              "Base route failed across tile seams");
        const auto original_triangles = base->triangles();
        std::array<CylinderObstacle, 1> shapes{{{EntityId::generate(), {0, -1, 0}, 2.4f, 4}}};
        check(query.obstacles(shapes) && query.generation() == 1 && query.tile_rebuilds() > 0,
              "Adding an obstacle did not rebuild its private touched tiles");
        const auto around = query.path(start, end);
        check(around.status == NavStatus::Success && around.points.size() > 2,
              "Obstacle did not reroute a path across tile seams");
        bool detoured = false;
        for (auto point : around.points) {
            detoured |= std::abs(point[plane == NavigationPlane::PlanarXY ? 1 : 2]) > 2.2;
            if (plane == NavigationPlane::PlanarXY)
                check(point[2] == 7, "Dynamic XY route changed visual depth");
        }
        check(detoured && independent.path(start, end).points == original.points &&
                  base->triangles() == original_triangles,
              "Obstacle carving mutated shared Mesh data or another Query");
        const auto count = query.tile_rebuilds(), generation = query.generation();
        check(!query.obstacles(shapes) && query.tile_rebuilds() == count &&
                  query.generation() == generation,
              "Unchanged obstacle snapshot performed redundant tile work");
        shapes[0].position[2] = 6;
        check(query.obstacles(shapes) && query.generation() == generation + 1 &&
                  query.path(start, end).points == original.points,
              "Moving an obstacle did not restore the old corridor");
        check(query.obstacles({}) && query.path(start, end).points == original.points,
              "Removing the obstacle did not restore the base route");
        // Native zero-touched requests never free their obstacle slots in this pin.
        // Off-mesh snapshots must not consume those slots or rebuild any tiles.
        const auto before_outside = query.tile_rebuilds();
        for (unsigned i = 0; i < 80; ++i) {
            shapes[0].position = {100.f + float(i), -1, 100};
            query.obstacles(shapes);
            query.obstacles({});
        }
        check(query.tile_rebuilds() == before_outside,
              "Out-of-mesh obstacles consumed native slots or performed tile work");
        shapes[0].position = {0, -1, 0};
        shapes[0].radius = 8;
        rejects([&] { query.obstacles(shapes); });
        check(query.path(start, end).status == NavStatus::Unavailable,
              "Rejected snapshot left a stale derivative available");
        check(query.obstacles({}) && query.path(start, end).points == original.points,
              "Valid snapshot could not reconstruct a retired derivative");
        std::array<CylinderObstacle, 2> duplicate{shapes[0], shapes[0]};
        duplicate[0].radius = duplicate[1].radius = 1;
        rejects([&] { query.obstacles(duplicate); });
        std::array<CylinderObstacle, 9> excessive;
        for (auto& shape : excessive)
            shape = {EntityId::generate(), {0, -1, 0}, 1, 4};
        rejects([&] { query.obstacles(excessive); });
        query.obstacles({});
        auto invalid = layers;
        invalid.layers[0].cells[layer_width * layer_width] = 7;
        rejects([&] { validate_layers(invalid); });
        invalid = layers;
        invalid.layers.push_back(invalid.layers[0]);
        rejects([&] { validate_layers(invalid); });
        auto truncated = payload;
        truncated.pop_back();
        rejects([&] { decode_layers(truncated); });
        auto meta = Json{{"asset_id", AssetId::generate()},
                         {"source_scene", g.scene},
                         {"sources", g.sources},
                         {"geometry_sha256", g.digest},
                         {"settings", settings},
                         {"version", 4},
                         {"geometry_version", plane == NavigationPlane::PlanarXY ? 3 : 2},
                         {"mesh_inputs", Json::array()}};
        const auto file = envelope(meta, payload);
        const auto admitted = admit(file);
        auto poisoned = payload;
        poisoned.back() = std::byte{255}; // Contradictory occupied/empty connection bits.
        auto forged_meta = admitted.metadata;
        forged_meta["tile_sha256"] = asset_detail::content_digest(poisoned);
        const auto text = forged_meta.dump();
        check(text.size() + payload.size() + 20 == file.size(), "Forged fixture length changed");
        auto forged = file;
        std::memcpy(forged.data() + 20, text.data(), text.size());
        std::copy(poisoned.begin(), poisoned.end(), forged.end() - poisoned.size());
        rejects([&] { admit(forged); });
        // This matches every outer digest/length/bounds field. Cell structure
        // must still be rejected before constructing native tile data.
        check(admitted.mesh->dynamic_obstacles() && admitted.mesh->space() == plane,
              "Version4 admission lost its explicit profile/basis");
        meta["version"] = plane == NavigationPlane::PlanarXY ? 3 : 2;
        rejects([&] { envelope(meta, payload); });
        settings.dynamic_obstacles = false;
        rejects([&] { build_layers(g, settings); });
    }
    std::cout << "Dynamic tiled admission/carving/seams/isolation/restore/budgets PASS\n";
}
