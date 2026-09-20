#pragma once
#include "import_process.hpp"
#include <forge/asset_importer.hpp>
namespace forge::asset_detail {
std::shared_ptr<const AssetImporter> texture_importer(std::filesystem::path worker, bool container);
ImportSettingsSchema texture_settings(bool container);
std::string texture_recipe_revision();
WorkerLimits texture_worker_limits();
// Called only by the separate native worker; parent validates cooked artifacts.
std::vector<ArtifactFile> execute_texture_recipe(const ImportProcessRequest& request,
                                                 std::stop_token stop = {});
} // namespace forge::asset_detail
