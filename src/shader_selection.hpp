#pragma once
#include <forge/shader_resource.hpp>
namespace forge::asset_detail {
// One immutable catalog selection, shared by resource-pool and editor admission.
// Preparation reads cooked bytes only and applies the same provenance checks.
struct ShaderSelection {
    std::string revision;
    std::uint64_t generation;
    ResourcePool<ShaderAsset>::Loader prepare;
};
ShaderSelection select_shader(std::filesystem::path, const AssetCatalog&, AssetRef<ShaderAsset>);
} // namespace forge::asset_detail
