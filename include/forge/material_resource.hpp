#pragma once
#include <filesystem>
#include <forge/material_asset.hpp>
#include <forge/resource.hpp>
namespace forge {
struct MaterialResourceData {
    MaterialData values;
    MaterialTextureBindings textures;
    std::size_t resident_bytes() const;
};
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
