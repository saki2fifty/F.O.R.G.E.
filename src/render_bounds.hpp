#pragma once
#include <forge/mesh_asset.hpp>
#include <forge/render_view.hpp>
#include <optional>
namespace forge {
struct RenderBounds {
    Double3 minimum{}, maximum{};
    bool operator==(const RenderBounds&) const = default;
};
// Conservative affine AABB; works with reflection, shear and zero scale.
RenderBounds transform_bounds(const MeshBounds&, const AffineTransform&);
// Double local bounds for integer grids and other non-mesh numeric consumers.
RenderBounds transform_render_bounds(const RenderBounds&, const AffineTransform&);
// Copied, transient skin data. Matrices are ordered by the prepared draw palette,
// already in world space (joint world * inverse bind); no inverse of the mesh
// node is needed or permitted. Bounds enclose every positive normalized blend.
struct SkinPose {
    MeshBounds source_bounds;
    std::vector<AffineTransform> palette;
    RenderBounds bounds;
    bool operator==(const SkinPose&) const = default;
};
SkinPose prepare_skin_pose(std::span<const AffineTransform> joint_world,
                           std::span<const AffineTransform> inverse_bind,
                           std::span<const std::uint32_t> draw_palette,
                           const MeshBounds& morphed_bounds);
// Enclose the camera-relative float palette/blend arithmetic used by MeshDraw.
// Recompute for each view origin, including shadow cameras; source bounds already
// include morph evaluation error. This is culling padding, not an authored edit.
RenderBounds skin_bounds_for_camera(const SkinPose&, Double3 origin);
// Projection is the actual admitted camera projection, not a second camera model.
bool bounds_visible(const RenderBounds&, const CameraView&);
double bounds_camera_depth(const RenderBounds&, const CameraView&);
// Conservative projected diameter relative to viewport height, clamped [0,1].
// A box crossing the eye plane selects full detail without division through zero.
float bounds_screen_coverage(const RenderBounds&, const CameraView&);
struct MeshOcclusionBounds {
    // Viewport-local conservative pixel edges, right/bottom exclusive.
    std::array<std::uint32_t, 4> rectangle{};
    float nearest_depth{};
};
// Static mesh positions only: enclose the actual binary32 world, camera and
// projection arithmetic, including instanced world rows. No skin/morph/wind.
// Eye/clip crossings, overflow and uncertain input decline to ordinary drawing.
std::optional<MeshOcclusionBounds> mesh_occlusion_bounds(const MeshBounds&, const AffineTransform&,
                                                         const CameraView&);
std::size_t select_mesh_lod(const MeshData&, float screen_coverage);
} // namespace forge
