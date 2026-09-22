#include <forge/asset_build.hpp>
#include <forge/engine_assets.hpp>
namespace forge {
std::string engine_asset_revision(AssetId id) {
    const auto* asset = engine_asset(id);
    if (!asset)
        throw std::runtime_error("Unknown engine asset identity");
    const auto* recipe = asset->primitive           ? "forge-engine-primitive-v2"
                         : engine_texture_asset(id) ? "forge-engine-texture-v1"
                                                    : "forge-engine-material-v1";
    return asset_build_digest({{"recipe", recipe}, {"asset", id}});
}
} // namespace forge
