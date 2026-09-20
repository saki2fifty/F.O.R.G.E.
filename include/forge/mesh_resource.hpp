#pragma once
#include <filesystem>
#include <forge/mesh_asset.hpp>
#include <forge/render_components.hpp>
#include <forge/resource.hpp>
namespace forge {
// Keys are stable within one logical mesh. Physical slot numbers belong only to
// this cooked revision and must never identify an authored override.
struct MeshMaterialBinding {
    std::uint32_t physical_slot = 0;
    std::string key;
    AssetRef<MaterialAsset> material;
    bool operator==(const MeshMaterialBinding&) const = default;
};

struct MeshMaterialSelection {
    std::vector<MeshMaterialBinding> bindings;
    // Retain authored entries when reimport removes a slot. Never guess a target.
    std::vector<std::string> unresolved;
};
struct MeshResourceData {
    MeshData mesh;
    std::vector<MeshMaterialBinding> materials;
    std::size_t resident_bytes() const;
};
void validate_mesh_material_bindings(const MeshResourceData& mesh);
MeshMaterialSelection select_mesh_materials(const MeshResourceData& mesh,
                                            std::span<const MaterialSlotOverride> overrides);
template <> struct ResourceTraits<MeshAsset> {
    using Data = MeshResourceData;
};
// Loads only an already-cooked, bounded file. No source importer, network, catalog
// mutation or graphics device is available to this runtime provider.
ResourcePool<MeshAsset>::Loader
mesh_resource_loader(std::filesystem::path path, std::string expected_file_digest,
                     MeshLimits limits = {}, std::vector<MeshMaterialBinding> bindings = {});
} // namespace forge
