#pragma once
#include <forge/asset_ref.hpp>
#include <forge/model_asset.hpp>
#include <string>
#include <vector>
namespace forge {
// Source provenance, not a second model hierarchy. A null node marks an ordinary
// instance root; node association follows its structural membership under that root.
// Local scene TRS/edits remain authored values and are not reset by resource reimport.
struct ModelSource {
    AssetRef<ModelAsset> model;
    AssetRef<ModelNodeAsset> node;
    bool operator==(const ModelSource&) const = default;
};
// Authored local policy. Effective values also include structural ancestors,
// independently of spatial binding and of native IsA component inheritance.
struct NodeVisibility {
    bool visible = true;
    bool operator==(const NodeVisibility&) const = default;
};
struct NodeSelectability {
    bool selectable = true;
    bool operator==(const NodeSelectability&) const = default;
};
struct MaterialSlotOverride {
    std::string slot;
    // Explicit null chooses the built-in default. Absence follows the mesh.
    AssetRef<MaterialAsset> material;
    bool operator==(const MaterialSlotOverride&) const = default;
};
// Authored values only. GPU resources, loaded leases, bounds and resolved draw
// slots are derived state owned by presentation, never component fields.
struct MeshRenderer {
    AssetRef<MeshAsset> mesh;
    std::vector<MaterialSlotOverride> materials;
    bool enabled = true, visible = true, cast_shadows = true, receive_shadows = true;
    std::uint32_t layers = UINT32_MAX;
    bool operator==(const MeshRenderer&) const = default;
};
} // namespace forge

namespace forge {
// FORGE keeps +Z camera-forward. Imported glTF cameras/lights retain their
// authored node TRS and select the explicit -Z projection/direction adapter.
enum class ViewBasis : std::uint32_t { ForgePositiveZ, GltfNegativeZ };
enum class CameraProjection : std::uint32_t { Perspective, Orthographic };
struct Camera {
    bool enabled = true;
    std::uint32_t projection = std::uint32_t(CameraProjection::Perspective);
    std::uint32_t basis = std::uint32_t(ViewBasis::ForgePositiveZ);
    double vertical_fov = 1.0471975511965976;                // Radians, full vertical angle.
    double orthographic_height = 10, orthographic_width = 0; // Full size; width0 follows aspect.
    double near_plane = .05, far_plane = 1000;
    bool infinite_far = false; // Perspective only.
    double aspect = 0;         // Zero follows viewport; positive fits without stretching.
    bool flip_x = false, flip_y = false;
    double viewport_x = 0, viewport_y = 0, viewport_width = 1, viewport_height = 1;
    std::int32_t order = 0; // Ascending composition; EntityId breaks ties.
    std::uint32_t layers = UINT32_MAX;
    bool clear_color = true, clear_depth = true;
    float background_r = .02f, background_g = .025f, background_b = .03f, background_a = 1;
    bool operator==(const Camera&) const = default;
};
enum class LightKind : std::uint32_t { Directional, Point, Spot };
struct Light {
    bool enabled = true;
    std::uint32_t kind = std::uint32_t(LightKind::Directional);
    std::uint32_t basis = std::uint32_t(ViewBasis::ForgePositiveZ);
    float color_r = 1, color_g = 1, color_b = 1;
    double intensity = 1; // Lux for directional, candela for point/spot.
    double range = 0;     // Zero is unbounded; never multiplied by node scale.
    double inner_cone = 0, outer_cone = .7853981633974483; // Radians, half angles.
    bool cast_shadows = false;
    float shadow_bias = .001f, shadow_normal_bias = .02f;
    std::uint32_t layers = UINT32_MAX;
    bool operator==(const Light&) const = default;
};
} // namespace forge
