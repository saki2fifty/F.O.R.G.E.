#pragma once
#include <filesystem>
#include <forge/material_asset.hpp>
#include <forge/resource.hpp>
#include <forge/shader_asset.hpp>
namespace forge {
// Exact cooked dependency embedded in a Material publication. It survives an
// incompatible later Shader publication, cache pruning and cooked packaging.
// Authored Material sources keep only the logical Shader reference and values.
struct MaterialShaderSnapshot {
    AssetRef<ShaderAsset> shader;
    std::string revision;
    ShaderData program;
};
struct MaterialResourceData {
    MaterialData values;
    MaterialTextureBindings textures;
    std::optional<MaterialShaderSnapshot> surface;
    std::size_t resident_bytes() const;
};
void validate_render_material(const MaterialResourceData&);
template <> struct ResourceTraits<MaterialAsset> {
    using Data = MaterialResourceData;
};
// Bindings and layout are copied from one immutable publication selection. Pass
// its generation to ResourcePool::request even if the cooked digest is unchanged.
ResourcePool<MaterialAsset>::Loader material_resource_loader(std::filesystem::path path,
                                                             std::string expected_file_digest,
                                                             MaterialTextureBindings bindings,
                                                             MaterialLayout layout);
} // namespace forge
