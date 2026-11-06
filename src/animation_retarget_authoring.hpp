#pragma once
#include "animation_retarget.hpp"
#include "asset_import_service.hpp"
#include "asset_source_document.hpp"

namespace forge {
std::shared_ptr<const AssetImporterRegistry> animation_retarget_import_registry();
void prepare_animation_retarget_publication(AssetPublicationCandidate&, const AssetImportPlan&);
struct AnimationRetargetDocumentTraits {
    using Source = AnimationRetargetSource;
    static constexpr auto byte_limit = animation_retarget_byte_limit;
    static constexpr auto asset_type = AnimationClipAsset::type;
    static constexpr auto suffix = ".retarget.json";
    static Source create(AssetId id) { return Source::create(id); }
    static void validate_edit(const Source&, const Source& candidate) {
        const auto bytes = candidate.document.dump(2) + '\n';
        (void)Source::parse(std::as_bytes(std::span(bytes)));
    }
};
using AnimationRetargetDocument = AssetSourceDocument<AnimationRetargetDocumentTraits>;
} // namespace forge
