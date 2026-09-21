#pragma once
#include "asset_import_service.hpp"
namespace forge {
std::shared_ptr<const AssetImporterRegistry> material_import_registry();
void prepare_material_publication(AssetPublicationCandidate&, const AssetImportPlan&);
} // namespace forge
