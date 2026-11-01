#include "ui_localization_authoring.hpp"
#include "asset_single_source_importer.hpp"

namespace forge {
namespace {
const SingleSourceImportProfile<UiLocalizationSource> profile{
    .importer = "forge.ui.localization",
    .revision = "forge.ui.localization.v1",
    .label = "UI Localization",
    .description = "Publish bounded literal UI messages and language fallbacks.",
    .suffix = ".localization.json",
    .format = "forge.ui-localization",
    .asset_type = UiLocalizationAsset::type,
    .file = "localization.json",
    .evidence_schema = "forge.ui-localization.single.v1",
    .metadata_key = "forge.ui_localization",
    .source_limit = ui_localization_byte_limit,
    .cooked_limit = ui_localization_byte_limit,
    .parse_cooked = UiLocalizationSource::parse};
} // namespace
std::shared_ptr<const AssetImporterRegistry> ui_localization_import_registry() {
    return single_source_import_registry(profile);
}
void prepare_ui_localization_publication(AssetPublicationCandidate& candidate,
                                         const AssetImportPlan& plan) {
    prepare_single_source_publication(candidate, plan, profile);
}
} // namespace forge
