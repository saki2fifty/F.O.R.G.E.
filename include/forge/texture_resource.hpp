#pragma once
#include <filesystem>
#include <forge/resource.hpp>
#include <forge/texture_asset.hpp>
namespace forge {
template <> struct ResourceTraits<TextureAsset> {
    using Data = TextureData;
};
ResourcePool<TextureAsset>::Loader texture_resource_loader(std::filesystem::path path,
                                                           std::string expected_file_digest,
                                                           TextureLimits limits = {});
} // namespace forge
