#pragma once
#include "asset_import_service.hpp"
#include "asset_source_document.hpp"
#include <forge/animation_graph.hpp>

namespace forge {
std::shared_ptr<const AssetImporterRegistry> animation_graph_import_registry();
void prepare_animation_graph_publication(AssetPublicationCandidate&, const AssetImportPlan&);
struct AnimationGraphDocumentTraits {
    using Source = AnimationGraphSource;
    static constexpr auto byte_limit = animation_graph_byte_limit;
    static constexpr auto asset_type = AnimationGraphAsset::type;
    static constexpr auto suffix = ".animgraph.json";
    static Source create(AssetId id) { return Source::create(id); }
    static void validate_edit(const Source&, const Source& candidate) {
        const auto bytes = candidate.document.dump(2) + '\n';
        (void)Source::parse(std::as_bytes(std::span(bytes)));
    }
};
using AnimationGraphDocument = AssetSourceDocument<AnimationGraphDocumentTraits>;
} // namespace forge
