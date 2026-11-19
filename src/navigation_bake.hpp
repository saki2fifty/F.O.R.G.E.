#pragma once
#include "navigation_asset.hpp"
#include "navigation_geometry.hpp"
#include "worker_staging.hpp"

namespace forge::navigation_detail {
// Pure provenance assembly shared by producers of the existing worker request.
nlohmann::json navigation_bake_metadata(AssetId, const Sources&, const Geometry&,
                                        const NavigationSettings&, const nlohmann::json& meshes,
                                        const nlohmann::json& tiles,
                                        const nlohmann::json& collisions);
// One private staged artifact; no catalog writer or publication authority.
struct NavigationBake {
    asset_detail::WorkerStaging staging;
    Admitted data;
    std::string artifact_digest;
    explicit NavigationBake(const std::filesystem::path& root) : staging(root, "navigation") {}
};
std::unique_ptr<NavigationBake> prepare_navigation_bake(const Geometry&, nlohmann::json metadata,
                                                        const std::filesystem::path& worker,
                                                        const std::filesystem::path& staging_root,
                                                        std::stop_token cancel = {});
} // namespace forge::navigation_detail
