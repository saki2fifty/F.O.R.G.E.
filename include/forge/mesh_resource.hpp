#pragma once
#include <filesystem>
#include <forge/mesh_asset.hpp>
#include <forge/resource.hpp>
namespace forge {
template <> struct ResourceTraits<MeshAsset> {
    using Data = MeshData;
};
// Loads only an already-cooked, bounded file. No source importer, network, catalog
// mutation or graphics device is available to this runtime provider.
ResourcePool<MeshAsset>::Loader mesh_resource_loader(std::filesystem::path path,
                                                     std::string expected_file_digest,
                                                     MeshLimits limits = {});
} // namespace forge
