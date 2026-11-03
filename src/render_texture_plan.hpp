#pragma once
#include <forge/render_scene.hpp>
#include <forge/render_texture.hpp>
#include <map>
#include <set>

namespace forge {
inline constexpr std::size_t render_texture_output_limit = 4;
inline constexpr std::size_t render_texture_camera_limit = 16;
inline constexpr std::uint64_t render_texture_pixel_limit = 4 * 1024 * 1024;
struct RenderTexturePass {
    RenderTextureData definition;
    CameraComposition composition;
    std::set<AssetId> reads;
};
struct RenderTexturePlan {
    std::map<AssetId, RenderTexturePass> passes;
    std::vector<AssetId> order;
};
// Derived frame routing only. Catalog dependencies still own saved references;
// selected draw materials supply reads through their existing resource owner.
RenderTexturePlan
prepare_render_texture_plan(const RenderScene&,
                            const std::map<AssetId, RenderTextureData>& definitions,
                            const std::map<EntityId, std::set<AssetId>>& camera_reads,
                            const std::set<AssetId>& main_reads = {});
} // namespace forge
