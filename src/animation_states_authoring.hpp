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
        if (current.document.at("version") == 1 && candidate.document.at("version") == 2)
            for (const auto& state : current.document.at("states"))
                if (state.contains("exit") && !state.at("exit").is_null())
                    throw std::runtime_error(
                        "Legacy state contains opaque exit data; rename that extension before "
                        "enabling automatic transitions");
        const auto next = current.document.at("next_id").get<std::uint64_t>();
        if (candidate.document.at("next_id").get<std::uint64_t>() < next)
            throw std::runtime_error("Animation state ID sequence cannot move backwards");
        validate_markers(current, candidate);
        std::set<std::uint32_t> existing;
        for (const auto& state : current.document.at("states"))
            existing.insert(state.at("id").get<std::uint32_t>());
        for (const auto& state : candidate.document.at("states")) {
            const auto id = state.at("id").get<std::uint32_t>();
            if (!existing.contains(id) && id < next)
                throw std::runtime_error("Animation state ID was already allocated");
        }
    }
    static void validate_markers(const Source& current, const Source& candidate) {
        const auto next = current.document.value("next_marker_id", std::uint64_t(1));
        if (candidate.document.value("next_marker_id", std::uint64_t(1)) < next)
            throw std::runtime_error("Animation marker ID sequence cannot move backwards");
        std::set<std::uint32_t> existing;
        for (const auto& state : current.document.at("states"))
            if (state.contains("markers"))
                for (const auto& marker : state.at("markers"))
                    existing.insert(marker.at("id").get<std::uint32_t>());
        for (const auto& state : candidate.document.at("states"))
            if (state.contains("markers"))
                for (const auto& marker : state.at("markers")) {
                    const auto id = marker.at("id").get<std::uint32_t>();
                    if (!existing.contains(id) && id < next)
                        throw std::runtime_error("Animation marker ID was already allocated");
                }
    }
    static void restore_identity_metadata(Source& next, const Source& current) {
        // Undo restores authored rows, never the allocator's published high-water mark.
        next.document["next_id"] = std::max(next.document.at("next_id").get<std::uint64_t>(),
                                            current.document.at("next_id").get<std::uint64_t>());
        if (next.document.contains("next_marker_id") || current.document.contains("next_marker_id"))
            next.document["next_marker_id"] =
                std::max(next.document.value("next_marker_id", std::uint64_t(1)),
                         current.document.value("next_marker_id", std::uint64_t(1)));
        next.validate();
    }
};
using AnimationStatesDocument = AssetSourceDocument<AnimationStatesDocumentTraits>;
} // namespace forge
