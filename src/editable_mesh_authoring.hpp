#pragma once
#include "asset_import_service.hpp"
#include <forge/editable_mesh.hpp>
namespace forge {
std::shared_ptr<const AssetImporterRegistry> editable_mesh_import_registry();
void prepare_editable_mesh_publication(AssetPublicationCandidate&, const AssetImportPlan&);
} // namespace forge
