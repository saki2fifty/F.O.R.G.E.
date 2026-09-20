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
