#pragma once
#include "asset_import_service.hpp"
#include "asset_source_document.hpp"
#include <forge/transform_animation.hpp>
namespace forge {
std::shared_ptr<const AssetImporterRegistry> transform_animation_import_registry();
void prepare_transform_animation_publication(AssetPublicationCandidate&, const AssetImportPlan&);
struct TransformAnimationDocumentTraits {
    using Source = TransformAnimationSource;
    static constexpr auto byte_limit = transform_animation_byte_limit;
    static constexpr auto asset_type = TransformAnimationAsset::type;
    static constexpr auto suffix = ".animation.json";
    static Source create(AssetId id) { return Source::create(id); }
};
using TransformAnimationDocument = AssetSourceDocument<TransformAnimationDocumentTraits>;
} // namespace forge
