#pragma once
#include "fbx_profile.hpp"
#include "model_importer.hpp"
namespace forge::asset_detail {
std::string fbx_recipe_revision();
std::shared_ptr<const AssetImporter> fbx_importer(std::filesystem::path worker,
                                                  std::filesystem::path converter = {});
std::vector<ArtifactFile> execute_fbx_recipe(ImportProcessRequest request,
                                             std::stop_token stop = {});
// Raw-source copy inspection uses the same supervised capture as model import.
// External selections admit dependencies only beneath the selected file's folder.
std::vector<AssetSourceDependency> fbx_source_dependencies(const std::filesystem::path& worker,
                                                           const std::filesystem::path& source,
                                                           std::stop_token stop = {});
} // namespace forge::asset_detail
