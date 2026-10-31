#pragma once
#include "runtime_tile_map.hpp"
#include "tile_animation.hpp"
#include "tile_gallery_scene_fixture.hpp"
#include <forge/physics.hpp>
#include <forge/runtime_world.hpp>
#include <thread>

namespace forge::test_animated_tile_scene {
inline constexpr const char* source = "Scenes/121-animated-tiles.scene.json";
using test_tile_asset_io::check;
inline Json playback(const std::filesystem::path& root, AssetId scene) {
    RuntimeWorld world({}, {}, std::nullopt, root, false);
    world.scene.restore_snapshot(load_game_scene(root, {scene}));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!world.prepare_scene_resources()) {
        check(std::chrono::steady_clock::now() < deadline, "Animated Gallery assets stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const std::map<unsigned, std::array<unsigned, 4>> expected{
        {0, {0, 3, 0, 1}},  {2, {0, 3, 1, 1}},  {4, {1, 2, 1, 1}}, {8, {2, 1, 3, 1}},
        {12, {3, 0, 3, 1}}, {18, {1, 2, 1, 1}}, {24, {3, 0, 3, 1}}};
    Json samples = Json::array();
    std::shared_ptr<const TileMapData> cells;
    std::shared_ptr<const TileSetData> definitions;
    for (unsigned tick = 0; tick <= 24; ++tick) {
        if (tick)
            world.simulation.tick(.0625f);
        if (!expected.contains(tick))
            continue;
        auto frame = extract_render_scene(world.simulation.presentation(1));
        detail::bind_runtime_tile_maps(world.engine.world(), frame);
        check(frame.diagnostics.empty() && frame.cameras.size() == 1 && frame.tile_maps.size() == 1,
              "Animated Gallery lost its Camera/Map composition");
        const auto& map = frame.tile_maps[0];
        check(map.cells && map.definitions && map.cells->occupied() == 24 &&
                  map.animation_seconds == tick * .0625,
              "Animated Gallery lost its cells or completed-tick sample");
        if (!tick) {
            cells = map.cells;
            definitions = map.definitions;
        }
        check(map.cells == cells && map.definitions == definitions,
              "Animated Gallery rebuilt immutable source data for a timing change");
        Json frames = Json::array();
        for (unsigned lane = 0; lane < 4; ++lane) {
            const auto selected =
                tile_detail::tile_frame_at(map.definitions->tile(lane + 2), map.animation_seconds);
            check(selected == expected.at(tick)[lane], "Animated Gallery lane sequence differs");
            frames.push_back(selected);
        }
        samples.push_back({{"tick", tick}, {"seconds", map.animation_seconds}, {"frames", frames}});
    }
    return samples;
}
} // namespace forge::test_animated_tile_scene
