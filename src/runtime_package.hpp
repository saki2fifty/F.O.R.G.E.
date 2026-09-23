#pragma once
#include <forge/assets.hpp>
#include <forge/project_lease.hpp>
#include <forge/ui_assets.hpp>
#include <functional>
#include <stop_token>
namespace forge {
// Content packaging only, not a standalone executable exporter. Logical AssetIds
// and the existing selected-artifact loaders are retained without source codecs.
struct RuntimePackageTarget {
    std::string platform, backend;
};
struct RuntimePackageLimits {
    std::uint64_t bytes = 2ull * 1024 * 1024 * 1024;
    std::size_t assets = 16384, files = 32768;
};
using RuntimeUiInspector = std::function<UiAssetSnapshot(AssetId, std::stop_token)>;
// Authoring metadata preparation, separate from package publication. New native
// UI discoveries receive persisted UUIDv4s here; repeated exports never remint IDs.
void prepare_runtime_content_catalog(const ProjectLease&, std::span<const AssetId>,
                                     const RuntimeUiInspector&, const nlohmann::json& schema = {},
                                     std::stop_token = {});
nlohmann::json package_runtime_content(const std::filesystem::path& project,
                                       const std::filesystem::path& destination,
                                       std::span<const AssetId> roots, const RuntimePackageTarget&,
                                       RuntimePackageLimits = {}, std::stop_token = {},
                                       const nlohmann::json& reference_schema = {},
                                       const RuntimeUiInspector& ui_inspector = {});
// Read-only compact manifest projection; AssetCatalog remains authoritative.
nlohmann::json runtime_asset_inventory(const AssetCatalog&);
// Validates the complete manifest, closure, hashes, target, and cooked formats.
// Returns the ordinary AssetCatalog used by the existing runtime loaders.
AssetCatalog open_runtime_content(const std::filesystem::path& package, const RuntimePackageTarget&,
                                  RuntimePackageLimits = {}, std::stop_token = {});
} // namespace forge
