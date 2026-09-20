#pragma once
#include <forge/resource.hpp>
#include <forge/shader_asset.hpp>
namespace forge {
class AssetCatalog;
template <> struct ResourceTraits<ShaderAsset> {
    using Data = ShaderData;
};
// CPU admission only, independent of source/compiler/device/editor. The render
// owner realizes bytecode and pipelines before its own frame-boundary adoption.
// Copy one typed catalog selection before queueing; reads selected cooked cache
// files without consulting shader sources or loading the source compiler.
ResourceTicket request_shader(ResourcePool<ShaderAsset>&, std::filesystem::path project,
                              const AssetCatalog&, AssetRef<ShaderAsset>);
} // namespace forge
