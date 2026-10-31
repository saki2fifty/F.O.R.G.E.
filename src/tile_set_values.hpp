#pragma once
#include "sprite_region.hpp"
#include <array>
#include <cmath>
#include <forge/asset_ref.hpp>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace forge {
inline constexpr std::size_t tile_set_image_limit = 16, tile_set_definition_limit = 4096;
inline constexpr std::size_t tile_collision_vertex_limit = 8;
inline constexpr std::size_t tile_animation_frame_limit = 64;
struct TileSetImage {
    std::uint64_t id = 0;
    std::string name;
    AssetRef<MaterialAsset> material;
    SpriteGridLayout grid;
};
enum class TileCollisionKind { FullCell, None, Convex, Polygon };
struct TileCollisionProfile {
    TileCollisionKind kind = TileCollisionKind::FullCell;
    // Unit cell in local XY, counterclockwise; physical scale is admitted later.
    std::vector<std::array<double, 2>> points;
    bool operator==(const TileCollisionProfile&) const = default;
};
struct TileAnimationFrame {
    std::uint32_t frame = 0;
    double seconds = .1;
    bool operator==(const TileAnimationFrame&) const = default;
};
struct TileAnimation {
    bool enabled = false;
    std::vector<TileAnimationFrame> frames;
    double duration = 0; // Derived once from admitted frames, never source authority.
    bool operator==(const TileAnimation&) const = default;
};
struct TileSetDefinition {
    std::uint64_t id = 0, image = 0;
    std::string name;
    std::uint32_t frame = 0;
    TileCollisionProfile collision{};
    std::shared_ptr<const TileAnimation> animation;
};
struct TileSetRows {
    std::vector<TileSetImage> images;
    std::vector<TileSetDefinition> tiles;
};
namespace tile_detail {
// Source contours require +1. Signed physical scale may reverse them to -1;
// zero rejects repeated/collinear/concave/self-intersecting order in either case.
template <class Scalar> int tile_collision_winding(std::span<const std::array<Scalar, 2>> points) {
    if (points.size() < 3 || points.size() > tile_collision_vertex_limit)
        return 0;
    int winding = 0;
    for (std::size_t edge = 0; edge < points.size(); ++edge) {
        const auto next = (edge + 1) % points.size();
        const auto& a = points[edge];
        const auto& b = points[next];
        for (std::size_t i = 0; i < points.size(); ++i)
            if (i != edge && i != next) {
                const auto& p = points[i];
                const double cross = (double(b[0]) - a[0]) * (double(p[1]) - a[1]) -
                                     (double(b[1]) - a[1]) * (double(p[0]) - a[0]);
                if (!std::isfinite(cross) || cross == 0)
                    return 0;
                const auto side = cross > 0 ? 1 : -1;
                if (winding && winding != side)
                    return 0;
                winding = side;
            }
    }
    return winding;
}
// Shared value admission. Source owns unknown fields/allocation history;
// the cooked decoder separately requires exactly its runtime fields.
TileSetRows decode_tile_set_rows(const nlohmann::json& images, const nlohmann::json& tiles,
                                 std::uint32_t profile_version = 1);
nlohmann::json tile_collision_json(const TileCollisionProfile&);
nlohmann::json tile_animation_json(const TileAnimation&);
nlohmann::json tile_definition_json(const TileSetDefinition&);
} // namespace tile_detail
} // namespace forge
