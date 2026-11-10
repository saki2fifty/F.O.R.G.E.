#pragma once
#include <forge/project_lease.hpp>
#include <forge/ui_assets.hpp>
namespace forge {
// Choice filtering is cheap; the selected reference is re-admitted by UiResources.
bool ui_image_source_candidate(const AssetRecord&);
nlohmann::json ui_image_reference(const AssetCatalog&, const std::filesystem::path& document,
                                  AssetId);
nlohmann::json ui_stylesheet_reference(const AssetCatalog&, const std::filesystem::path& document,
                                       AssetId);
// Font-face sources are project relative; recorded family names are not authored aliases.
nlohmann::json ui_font_reference(const AssetCatalog&, AssetId, unsigned face_index);
// Explicit authoring operations. Never called by the presenter/runtime itself.
AssetCatalog prepare_ui_asset_catalog(AssetCatalog, const std::filesystem::path&,
                                      const UiAssetSnapshot&);
AssetCatalog refresh_ui_asset_catalog(const ProjectLease&, const UiAssetSnapshot&);
AssetRecord register_ui_source(const ProjectLease&, const std::filesystem::path&);
} // namespace forge
