#pragma once
#include "import_process.hpp"
#include <forge/asset_importer.hpp>
namespace forge::asset_detail {
std::shared_ptr<const AssetImporter> audio_importer(std::filesystem::path worker);
ImportSettingsSchema audio_settings();
std::string audio_recipe_revision();
WorkerLimits audio_worker_limits();
std::vector<ArtifactFile> execute_audio_recipe(const ImportProcessRequest&, std::stop_token = {});
} // namespace forge::asset_detail
