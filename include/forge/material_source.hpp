#pragma once
#include <forge/material_asset.hpp>
#include <optional>
namespace forge {
inline constexpr std::size_t material_source_byte_limit = 1024 * 1024;
// Authored JSON is retained intact, including unrecognized extension fields.
// Missing overrides inherit. Explicit null resets a parameter to model default
// or clears a texture. Equal-value overrides remain authored intent.
struct MaterialSource {
    nlohmann::json document;
    AssetId asset() const;
    std::optional<AssetRef<MaterialAsset>> base() const;
    static MaterialSource parse(std::span<const std::byte> bytes);
    static MaterialSource create(AssetId asset);
    void validate() const;
};
struct ResolvedMaterialSource {
    MaterialData values;
    MaterialTextureBindings textures;
};
// The caller supplies an already selected, validated immutable base revision.
// No source reads, recursive import, publication, scene edits or ECS inheritance.
// Catalog dependency admission owns cycles and revision invalidation.
ResolvedMaterialSource resolve_material_source(const MaterialSource& source,
                                               const ResolvedMaterialSource* base = nullptr);
} // namespace forge
