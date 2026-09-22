#pragma once
#include <forge/asset_ref.hpp>
namespace forge {
struct ModelAsset {
    static constexpr const char* type = "model";
};
// Durable provenance of a node inside an imported Model. A placed scene entity
// has its own EntityId; this reference never identifies a runtime instance.
struct ModelNodeAsset {
    static constexpr const char* type = "model_node";
};
// One imported material set. Mappings belong to its immutable Model revision;
// source array positions never identify an authored selection.
struct MaterialVariantAsset {
    static constexpr const char* type = "material_variant";
};
} // namespace forge
