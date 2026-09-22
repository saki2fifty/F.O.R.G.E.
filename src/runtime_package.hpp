#pragma once
#include <forge/assets.hpp>
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
nlohmann::json package_runtime_content(const std::filesystem::path& project,
                                       const std::filesystem::path& destination,
                                       std::span<const AssetId> roots, const RuntimePackageTarget&,
                                       RuntimePackageLimits = {}, std::stop_token = {});
// Validates the complete manifest, closure, hashes, target, and cooked formats.
// Returns the ordinary AssetCatalog used by the existing runtime loaders.
AssetCatalog open_runtime_content(const std::filesystem::path& package, const RuntimePackageTarget&,
                                  RuntimePackageLimits = {}, std::stop_token = {});
} // namespace forge
