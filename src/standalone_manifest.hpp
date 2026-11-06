#pragma once
#include "runtime_package.hpp"
namespace forge {
inline constexpr unsigned model_fbx_runtime_profile_version = 1;
// Physical distribution metadata, not another logical asset graph. Content keeps
// its existing AssetCatalog and immutable artifact/identity contracts.
struct StandaloneDistribution {
    std::filesystem::path root, content;
    nlohmann::json manifest, settings;
};
// Inspect the existing cooked document graph and feature requirements, including
// Prefab overrides. Default/off content retains earlier runtime-kit compatibility.
void validate_runtime_compatibility(const AssetCatalog&, const nlohmann::json& engine,
                                    const nlohmann::json* project = nullptr);
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
