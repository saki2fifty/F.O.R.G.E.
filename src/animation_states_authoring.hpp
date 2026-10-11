#pragma once
#include "asset_import_service.hpp"
#include "asset_source_document.hpp"
#include <algorithm>
#include <forge/animation_states.hpp>
#include <set>
namespace forge {
std::shared_ptr<const AssetImporterRegistry> animation_states_import_registry();
void prepare_animation_states_publication(AssetPublicationCandidate&, const AssetImportPlan&);
struct AnimationStatesDocumentTraits {
    using Source = AnimationStatesSource;
    static constexpr auto byte_limit = animation_states_byte_limit;
    static constexpr auto asset_type = AnimationStatesAsset::type;
    static constexpr auto suffix = ".states.json";
    static Source create(AssetId id) { return Source::create(id); }
    static void validate_edit(const Source& current, const Source& candidate) {
        const auto next = current.document.at("next_id").get<std::uint64_t>();
        if (candidate.document.at("next_id").get<std::uint64_t>() < next)
            throw std::runtime_error("Animation state ID sequence cannot move backwards");
        std::set<std::uint32_t> existing;
        for (const auto& state : current.document.at("states"))
            existing.insert(state.at("id").get<std::uint32_t>());
        for (const auto& state : candidate.document.at("states")) {
            const auto id = state.at("id").get<std::uint32_t>();
            if (!existing.contains(id) && id < next)
                throw std::runtime_error("Animation state ID was already allocated");
        }
    }
    static void restore_identity_metadata(Source& next, const Source& current) {
        // Undo restores authored rows, never the allocator's published high-water mark.
        next.document["next_id"] = std::max(next.document.at("next_id").get<std::uint64_t>(),
                                            current.document.at("next_id").get<std::uint64_t>());
        next.validate();
    }
};
using AnimationStatesDocument = AssetSourceDocument<AnimationStatesDocumentTraits>;
} // namespace forge
