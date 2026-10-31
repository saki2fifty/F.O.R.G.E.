#pragma once
#include "tile_gallery_scene_fixture.hpp"

namespace forge::test_concave_tile_scene {
inline constexpr const char* source = "Scenes/120-concave-tile-collision.scene.json";
using test_tile_asset_io::check;
using test_tile_gallery_scene::Prepared;
inline Prepared prepare(const std::filesystem::path& gallery, const std::filesystem::path& scratch,
                        const std::filesystem::path& worker) {
    return test_tile_gallery_scene::prepare(gallery, scratch, worker, source);
}
} // namespace forge::test_concave_tile_scene
