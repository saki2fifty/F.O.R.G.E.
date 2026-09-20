#pragma once
#include "gltf_snapshot.hpp"
#include "import_process.hpp"
#include <forge/asset_importer.hpp>
namespace forge::asset_detail {
// Private model preparation profile. Renderer capability admission remains separate.
std::shared_ptr<const AssetImporter> model_importer(std::filesystem::path worker,
                                                    std::filesystem::path converter = {});
// Consumes the private native worker stage after that process has exited.
std::vector<ArtifactFile>
finish_model_recipe(std::vector<ArtifactFile> files, const std::filesystem::path& converter,
                    const std::filesystem::path& project, std::string_view source_digest,
                    std::string_view converter_digest, std::stop_token stop = {});
ImportSettingsSchema model_settings();
std::string model_recipe_revision();
WorkerLimits model_worker_limits();
const std::set<std::string>& model_cook_extensions();
std::vector<ArtifactFile> execute_model_recipe(ImportProcessRequest request,
                                               std::stop_token stop = {});
} // namespace forge::asset_detail
