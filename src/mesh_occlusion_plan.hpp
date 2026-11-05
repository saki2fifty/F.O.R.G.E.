#pragma once
#include <algorithm>
#include <bit>
#include <cstdint>
#include <optional>

namespace forge {
// Private image accounting for the MeshSceneRenderer occlusion facet. The
// hierarchy starts at conservative 2x2 depth maxima, with power-of-two padding;
// missing source/padded pixels must represent far depth, never an occluder.
struct MeshOcclusionImagePlan {
    unsigned width{}, height{}, pyramid_width{}, pyramid_height{}, levels{};
    std::uint64_t depth_bytes{}, pyramid_bytes{};
    bool operator==(const MeshOcclusionImagePlan&) const = default;
};
inline constexpr std::uint64_t mesh_occlusion_pixel_limit = 8388608;
inline constexpr std::uint64_t mesh_occlusion_image_budget = 64ull * 1024 * 1024;
// Unsupported/empty/oversized views decline the optional optimization. The
// caller must retain ordinary visibility/submission; this is not a render error.
inline std::optional<MeshOcclusionImagePlan>
mesh_occlusion_image_plan(unsigned width, unsigned height, unsigned device_dimension) noexcept {
    if (!width || !height || width > 16384 || height > 16384 || width > device_dimension ||
        height > device_dimension || std::uint64_t(width) * height > mesh_occlusion_pixel_limit)
        return std::nullopt;
    MeshOcclusionImagePlan result;
    result.width = width;
    result.height = height;
    result.pyramid_width = std::bit_ceil((width + 1) / 2);
    result.pyramid_height = std::bit_ceil((height + 1) / 2);
    result.depth_bytes = std::uint64_t(width) * height * 4;
    unsigned w = result.pyramid_width, h = result.pyramid_height;
    do {
        result.pyramid_bytes += std::uint64_t(w) * h * 4;
        ++result.levels;
        if (w == 1 && h == 1)
            break;
        w = std::max(w / 2, 1u);
        h = std::max(h / 2, 1u);
    } while (true);
    if (result.depth_bytes + result.pyramid_bytes > mesh_occlusion_image_budget)
        return std::nullopt;
    return result;
}
} // namespace forge
