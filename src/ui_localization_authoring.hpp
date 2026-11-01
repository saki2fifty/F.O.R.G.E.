#pragma once
#include "asset_import_service.hpp"
#include "asset_source_document.hpp"
#include <forge/ui_localization.hpp>

namespace forge {
std::shared_ptr<const AssetImporterRegistry> ui_localization_import_registry();
void prepare_ui_localization_publication(AssetPublicationCandidate&, const AssetImportPlan&);
struct UiLocalizationDocumentTraits {
    using Source = UiLocalizationSource;
    static constexpr auto byte_limit = ui_localization_byte_limit;
    static constexpr auto asset_type = UiLocalizationAsset::type;
    static constexpr auto suffix = ".localization.json";
    static Source create(AssetId id) { return Source::create(id); }
};
using UiLocalizationDocument = AssetSourceDocument<UiLocalizationDocumentTraits>;
} // namespace forge
