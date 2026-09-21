#pragma once
#include "asset_file_transaction.hpp"
#include <forge/assets.hpp>
#include <functional>
namespace forge {
enum class AssetFileAction { Move, Duplicate, Delete };
struct AssetFileRequest {
    AssetFileAction action;
    AssetId asset;
    std::filesystem::path destination;
};
struct AssetFilePlan {
    AssetFileRequest request;
    AssetId result;
    std::vector<AssetId> affected, catalog_dependents;
    std::map<AssetId, AssetId> duplicated;
    std::vector<AssetFileChange> changes;
    std::string importer;
    std::vector<std::string> warnings;
};
// A format adapter may adjust only understood source-owned identity/locators.
// It returns an immutable candidate; no filesystem/catalog/scene mutation.
using AssetSourceRewrite =
    std::function<std::string(const AssetRecord&, std::string_view, const std::filesystem::path&,
                              const std::map<AssetId, AssetId>&)>;
// Read-only worker operation, with exact source/sidecar/catalog revision capture.
AssetFilePlan prepare_asset_file_operation(const std::filesystem::path& project,
                                           AssetFileRequest request,
                                           const AssetSourceRewrite& rewrite = {},
                                           std::stop_token stop = {});
// Supports source-owned Scene/Prefab/Material/Shader identities. Other imported
// raw sources are copied by their caller only when their format contract permits.
std::string rewrite_authored_asset(const AssetRecord&, std::string_view,
                                   const std::filesystem::path&, const std::map<AssetId, AssetId>&);
AssetSourceRewrite project_asset_file_rewriter(const std::filesystem::path& project);
} // namespace forge
