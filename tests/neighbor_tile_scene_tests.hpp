#pragma once
#include "runtime_tile_map.hpp"
#include "tile_animation.hpp"
#include "tile_gallery_scene_fixture.hpp"
#include <forge/runtime_world.hpp>
#include <thread>

namespace forge::test_neighbor_tile_scene {
inline constexpr const char* source = "Scenes/122-neighbor-tiles.scene.json";
using test_tile_asset_io::check;
inline Json playback(const std::filesystem::path& root, AssetId scene) {
    RuntimeWorld world({}, {}, std::nullopt, root, false);
    world.scene.restore_snapshot(load_game_scene(root, {scene}));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!world.prepare_scene_resources()) {
        check(std::chrono::steady_clock::now() < deadline, "Neighbor Gallery assets stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Json samples = Json::array();
    std::shared_ptr<const TileMapData> cells;
    std::shared_ptr<const TileSetData> definitions;
    std::shared_ptr<const TileMapVariants> variants;
    for (unsigned tick = 0; tick <= 16; ++tick) {
        if (tick)
            world.simulation.tick(.0625f);
        if (tick % 4)
            continue;
        auto frame = extract_render_scene(world.simulation.presentation(1));
        detail::bind_runtime_tile_maps(world.engine.world(), frame);
        check(frame.diagnostics.empty() && frame.cameras.size() == 1 && frame.tile_maps.size() == 1,
              "Neighbor Gallery lost its Camera/Map composition");
        const auto& map = frame.tile_maps[0];
        check(map.cells && map.definitions && map.variants && map.cells->occupied() == 32 &&
                  map.animation_seconds == tick / 16.,
              "Neighbor Gallery lost logical cells/selection/time");
        if (!tick) {
            cells = map.cells;
            definitions = map.definitions;
            variants = map.variants;
        }
        check(map.cells == cells && map.definitions == definitions && map.variants == variants,
              "Neighbor Gallery rebuilt immutable selection on a time-only update");
        Json selected = Json::array();
        std::size_t interior = 0;
        variants->for_each_chunk([&](auto key, auto rows, auto) {
            for (unsigned slot = 0; slot < tile_chunk_cells; ++slot) {
                if (!rows[slot].tile)
                    continue;
                const auto at = tile_cell_address({key.address, slot});
                const auto logical = cells->cell(key.layer, at);
                check(logical.tile == 2 && logical.flags <= 1,
                      "Gallery source cells lost their one logical paint key");
                // Independent cardinal oracle for the authored identity/X-flip cases.
                const int right = logical.flags ? -1 : 1;
                const std::array<std::array<int, 3>, 4> edges{
                    {{0, 1, 3}, {right, 0, 4}, {0, -1, 5}, {-right, 0, 6}}};
                std::uint64_t expected = 2;
                for (const auto& edge : edges)
                    if (cells->cell(key.layer, {at.x + edge[0], at.y + edge[1]}).tile != 2) {
                        expected = edge[2];
                        break;
                    }
                check(rows[slot].tile == expected && rows[slot].flags == logical.flags,
                      "Gallery selected edges/hole/flipped L disagree with independent cardinal "
                      "oracle");
                const auto& definition = definitions->tile(expected);
                const auto selected_frame =
                    tile_detail::tile_frame_at(definition, map.animation_seconds);
                const auto expected_frame = expected == 2 ? (tick / 4) % 4 : expected - 3;
                check(selected_frame == expected_frame,
                      "Neighbor Gallery selected animation/frame differs");
                interior += expected == 2;
                selected.push_back(
                    {key.layer, at.x, at.y, expected, logical.flags, selected_frame});
            }
        });
        check(interior == 4, "Gallery lost its four animated connected interior cells");
        samples.push_back({{"tick", tick}, {"cells", selected}});
    }
    return samples;
}
} // namespace forge::test_neighbor_tile_scene
