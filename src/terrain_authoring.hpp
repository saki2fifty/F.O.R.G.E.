#pragma once
#include "asset_import_service.hpp"
#include "terrain_document.hpp"
namespace forge {
inline constexpr auto terrain_importer = "forge.terrain.heightfield";
inline constexpr auto terrain_recipe = "forge.terrain.heightfield.v1";
std::shared_ptr<const AssetImporterRegistry> terrain_import_registry();
void prepare_terrain_publication(AssetPublicationCandidate&, const AssetImportPlan&);
} // namespace forge
