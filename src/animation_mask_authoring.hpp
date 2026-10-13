#pragma once
#include "asset_import_service.hpp"
#include "asset_source_document.hpp"
#include <forge/animation_mask.hpp>
namespace forge {
std::shared_ptr<const AssetImporterRegistry> animation_mask_import_registry();
void prepare_animation_mask_publication(AssetPublicationCandidate&, const AssetImportPlan&);
struct AnimationMaskDocumentTraits {
    using Source = AnimationMaskSource;
    static constexpr auto byte_limit = animation_mask_byte_limit;
    static constexpr auto asset_type = AnimationMaskAsset::type;
    static constexpr auto suffix = ".mask.json";
    static Source create(AssetId id) { return Source::create(id); }
    static void validate_edit(const Source&, const Source&) {}
    static void restore_identity_metadata(Source& next, const Source&) { next.validate(); }
};
using AnimationMaskDocument = AssetSourceDocument<AnimationMaskDocumentTraits>;
} // namespace forge
