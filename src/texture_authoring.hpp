#pragma once
#include "asset_import_service.hpp"
namespace forge {
std::shared_ptr<const AssetImporterRegistry>
texture_import_registry(const std::filesystem::path& worker);
ImportTarget desktop_texture_target();
void prepare_texture_publication(AssetPublicationCandidate& candidate, const AssetImportPlan& plan);
} // namespace forge
