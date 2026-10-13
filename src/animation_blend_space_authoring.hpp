#pragma once
#include "asset_import_service.hpp"
#include "asset_source_document.hpp"
#include <forge/animation_blend_space.hpp>
namespace forge {
std::shared_ptr<const AssetImporterRegistry> animation_blend_space_import_registry();
void prepare_animation_blend_space_publication(AssetPublicationCandidate&, const AssetImportPlan&);
struct AnimationBlendSpaceDocumentTraits {
    using Source = AnimationBlendSpaceSource;
    static constexpr auto byte_limit = animation_blend_space_byte_limit;
    static constexpr auto asset_type = AnimationBlendSpaceAsset::type;
    static constexpr auto suffix = ".blendspace.json";
    static Source create(AssetId id) { return Source::create(id); }
    static void validate_edit(const Source&, const Source&) {}
    static void restore_identity_metadata(Source& next, const Source&) { next.validate(); }
};
using AnimationBlendSpaceDocument = AssetSourceDocument<AnimationBlendSpaceDocumentTraits>;
} // namespace forge
