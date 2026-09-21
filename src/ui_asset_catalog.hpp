#pragma once
#include <forge/project_lease.hpp>
#include <forge/ui_assets.hpp>
namespace forge {
// Explicit authoring operations. Never called by the presenter/runtime itself.
AssetCatalog refresh_ui_asset_catalog(const ProjectLease&, const UiAssetSnapshot&);
AssetRecord register_ui_source(const ProjectLease&, const std::filesystem::path&);
} // namespace forge
