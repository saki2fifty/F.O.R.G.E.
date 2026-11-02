#pragma once
#include "asset_import_service.hpp"
#include "asset_source_document.hpp"
#include <forge/navigation_behavior.hpp>
#include <map>

namespace forge {
std::shared_ptr<const AssetImporterRegistry> navigation_behavior_import_registry();
void prepare_navigation_behavior_publication(AssetPublicationCandidate&, const AssetImportPlan&);
// Mutations are committed/validated atomically by NavigationBehaviorDocument.
std::uint32_t add_navigation_behavior_state(nlohmann::json&);
std::uint32_t connect_navigation_behavior_states(nlohmann::json&, std::uint32_t from,
                                                 std::uint32_t to);
void remove_navigation_behavior_state(nlohmann::json&, std::uint32_t);
std::uint32_t add_navigation_behavior_node(nlohmann::json&, NavigationBehaviorTreeKind);
void set_navigation_behavior_node_kind(nlohmann::json&, std::uint32_t, NavigationBehaviorTreeKind);
void connect_navigation_behavior_nodes(nlohmann::json&, std::uint32_t parent, std::uint32_t child);
void remove_navigation_behavior_node(nlohmann::json&, std::uint32_t);
struct NavigationBehaviorDocumentTraits {
    using Source = NavigationBehaviorSource;
    static constexpr auto byte_limit = navigation_behavior_byte_limit;
    static constexpr auto asset_type = NavigationBehaviorAsset::type;
    static constexpr auto suffix = ".behavior.json";
    static Source create(AssetId id) { return Source::create(id); }
    static std::map<std::uint32_t, bool> identities(const Source& source) {
        std::map<std::uint32_t, bool> result;
        const bool tree = source.document.at("version") == 2;
        for (const auto& state : source.document.at(tree ? "nodes" : "states")) {
            result.emplace(state.at("id").get<std::uint32_t>(), false);
            if (!tree)
                for (const auto& edge : state.at("transitions"))
                    result.emplace(edge.at("id").get<std::uint32_t>(), true);
        }
        return result;
    }
    static void validate_edit(const Source& current, const Source& candidate) {
        const auto next = current.document.at("next_id").get<std::uint64_t>();
        if (candidate.document.at("next_id").get<std::uint64_t>() < next)
            throw std::runtime_error("Behavior identity sequence cannot move backwards");
        const auto existing = identities(current);
        for (const auto& [id, edge] : identities(candidate)) {
            const auto prior = existing.find(id);
            if (prior != existing.end() ? prior->second != edge : id < next)
                throw std::runtime_error("Behavior identity was already allocated to another row");
        }
    }
    static void restore_identity_metadata(Source& candidate, const Source& current) {
        candidate.document["next_id"] =
            std::max(candidate.document.at("next_id").get<std::uint64_t>(),
                     current.document.at("next_id").get<std::uint64_t>());
        candidate.validate();
    }
};
using NavigationBehaviorDocument = AssetSourceDocument<NavigationBehaviorDocumentTraits>;
} // namespace forge
