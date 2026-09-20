#pragma once
#include "asset_import_service.hpp"
namespace forge {
std::shared_ptr<const AssetImporterRegistry>
asset_import_registry(const std::filesystem::path& worker);
void prepare_asset_publication(AssetPublicationCandidate& candidate, const AssetImportPlan& plan,
                               const AssetCatalog& previous_catalog,
                               std::span<const SubassetIdentityDecision> decisions = {});
} // namespace forge
