#pragma once
#include "asset_import_service.hpp"
#include "asset_source_document.hpp"
#include <forge/application_state_source.hpp>

namespace forge {
std::shared_ptr<const AssetImporterRegistry> application_states_import_registry();
void prepare_application_states_publication(AssetPublicationCandidate&, const AssetImportPlan&);
struct ApplicationStatesDocumentTraits {
    using Source = ApplicationStatesSource;
    static constexpr auto byte_limit = application_states_byte_limit;
    static constexpr auto asset_type = ApplicationStatesAsset::type;
    static constexpr auto suffix = ".appstates.json";
    static Source create(AssetId id) { return Source::create(id); }
};
using ApplicationStatesDocument = AssetSourceDocument<ApplicationStatesDocumentTraits>;
} // namespace forge
