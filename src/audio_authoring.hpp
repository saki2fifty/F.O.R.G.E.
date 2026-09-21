#pragma once
#include "asset_import_service.hpp"
namespace forge {
std::shared_ptr<const AssetImporterRegistry>
audio_import_registry(const std::filesystem::path& worker);
void prepare_audio_publication(AssetPublicationCandidate&, const AssetImportPlan&);
} // namespace forge
