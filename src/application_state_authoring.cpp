#include "application_state_authoring.hpp"
#include "asset_single_source_importer.hpp"

namespace forge {
namespace {
const SingleSourceImportProfile<ApplicationStatesSource> profile{
    .importer = "forge.application.states",
    .revision = "forge.application.states.v1",
    .label = "Application States",
    .description = "Publish logical game states, hierarchy and domain conditions.",
    .suffix = ".appstates.json",
    .format = "forge.application-states",
    .asset_type = ApplicationStatesAsset::type,
    .file = "application-states.json",
    .evidence_schema = "forge.application-states.single.v1",
    .metadata_key = "forge.application_states",
    .source_limit = application_states_byte_limit,
    .cooked_limit = application_states_byte_limit,
    .parse_cooked = ApplicationStatesSource::parse};
} // namespace
std::shared_ptr<const AssetImporterRegistry> application_states_import_registry() {
    return single_source_import_registry(profile);
}
void prepare_application_states_publication(AssetPublicationCandidate& candidate,
                                            const AssetImportPlan& plan) {
    prepare_single_source_publication(candidate, plan, profile);
}
} // namespace forge
