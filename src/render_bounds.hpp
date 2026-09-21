#pragma once
#include <forge/mesh_asset.hpp>
#include <forge/render_view.hpp>
namespace forge {
struct RenderBounds {
    Double3 minimum{}, maximum{};
    bool operator==(const RenderBounds&) const = default;
};
// Conservative affine AABB; works with reflection, shear and zero scale.
RenderBounds transform_bounds(const MeshBounds&, const AffineTransform&);
// Copied, transient skin data. Matrices are ordered by the prepared draw palette,
// already in world space (joint world * inverse bind); no inverse of the mesh
// node is needed or permitted. Bounds enclose every positive normalized blend.
struct SkinPose {
    std::vector<AffineTransform> palette;
    RenderBounds bounds;
};
SkinPose prepare_skin_pose(std::span<const AffineTransform> joint_world,
                           std::span<const AffineTransform> inverse_bind,
                           std::span<const std::uint32_t> draw_palette,
                           const MeshBounds& morphed_bounds);
// Projection is the actual admitted camera projection, not a second camera model.
bool bounds_visible(const RenderBounds&, const CameraView&);
double bounds_camera_depth(const RenderBounds&, const CameraView&);
// Conservative projected diameter relative to viewport height, clamped [0,1].
// A box crossing the eye plane selects full detail without division through zero.
float bounds_screen_coverage(const RenderBounds&, const CameraView&);
std::size_t select_mesh_lod(const MeshData&, float screen_coverage);
} // namespace forge
