#pragma once
#include "runtime_package.hpp"
namespace forge {
// Physical distribution metadata, not another logical asset graph. Content keeps
// its existing AssetCatalog and immutable artifact/identity contracts.
struct StandaloneDistribution {
    std::filesystem::path root, content;
    nlohmann::json manifest, settings;
};
// Full read-only admission before loading gameplay code or opening a window.
StandaloneDistribution open_standalone_distribution(const std::filesystem::path&,
                                                    const RuntimePackageTarget&,
                                                    std::string_view linkage_profile,
                                                    std::string_view sdk_fingerprint,
                                                    std::stop_token = {});
// Reusable bounded physical-file verification for runtime kits and module kits.
// Manifest and directories must be link-free. Every ordinary file is inventoried.
nlohmann::json verify_distribution_files(const std::filesystem::path&,
                                         const std::filesystem::path& manifest,
                                         std::string_view format, std::stop_token = {});
} // namespace forge
