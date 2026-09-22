#pragma once
#include "asset_import_service.hpp"
#include <forge/material_resource.hpp>
#include <forge/material_source.hpp>
namespace forge {
struct EvaluatedMaterialSource {
    MaterialResourceData data;
    std::vector<AssetDependency> dependencies;
};
struct MaterialSourceContext {
    std::optional<ResolvedMaterialSource> base;
    std::optional<MaterialShaderSnapshot> surface;
    std::vector<AssetDependency> dependencies;
};
// Selecting the interface is separate from validating draft values: the editor
// must show required fields even while a new material is incomplete.
MaterialSourceContext prepare_material_source(const std::filesystem::path&, const AssetCatalog&,
                                              const MaterialSource&, std::stop_token = {});
// Detached cooked dependency selection for import and unsaved preview. Never
// compiles project shaders or publishes a source/catalog change.
EvaluatedMaterialSource evaluate_material_source(const std::filesystem::path&, const AssetCatalog&,
                                                 const MaterialSource&, std::stop_token = {});
std::shared_ptr<const AssetImporterRegistry> material_import_registry();
void prepare_material_publication(AssetPublicationCandidate&, const AssetImportPlan&);
} // namespace forge
