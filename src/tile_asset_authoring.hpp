#pragma once
#include "asset_import_service.hpp"
#include "tile_asset_types.hpp"

namespace forge {
std::shared_ptr<const AssetImporterRegistry>
tile_asset_import_registry(std::filesystem::path worker);
void prepare_tile_set_publication(AssetPublicationCandidate&, const AssetImportPlan&);
void prepare_tile_map_publication(AssetPublicationCandidate&, const AssetImportPlan&);
} // namespace forge
