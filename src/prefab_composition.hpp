#pragma once
#include "relationship_graph.hpp"
#include <algorithm>
#include <forge/asset_ref.hpp>
#include <forge/prefab.hpp>
#include <functional>
#include <optional>
#include <set>

namespace forge::detail {
inline void enable_scene_prefab_composition(Json& scene) {
    if (scene.at("version").get<unsigned>() >= 6)
        return;
    for (const auto& row : scene.at("entities"))
        if (row.contains("prefab_nested") ||
            (row.contains("prefab_instance") && row.at("prefab_instance").contains("nested")))
            throw std::runtime_error(
                "Older scene contains opaque composition fields; preserve or rename them "
                "explicitly before enabling scene6");
    scene["version"] = 6;
}
inline std::set<std::string> inactive_prefab_composition_entities(const Json& scene) {
    std::set<std::string> inactive;
    if (scene.at("version").get<unsigned>() < 6)
        return inactive;
    std::map<std::string, std::vector<std::string>> children;
    for (const auto& row : scene.at("entities")) {
        const auto id = row.at("id").get<std::string>();
        if (row.contains("parent"))
            children[row.at("parent").get<std::string>()].push_back(id);
        if (row.contains("prefab_nested") && row.value("missing_member", false))
            inactive.insert(id);
    }
    std::vector<std::string> pending(inactive.begin(), inactive.end());
    for (std::size_t i = 0; i < pending.size(); ++i)
        if (const auto at = children.find(pending[i]); at != children.end())
            for (const auto& child : at->second)
                if (inactive.insert(child).second)
                    pending.push_back(child);
    return inactive;
}
inline std::optional<AssetId> nested_prefab_asset(const Json& member, unsigned version) {
    if (version < 7 || !member.contains("prefab_instance") ||
        member.at("prefab_instance").is_null())
        return {};
    const auto& link = member.at("prefab_instance");
    if (!link.is_object())
        throw std::runtime_error("Nested prefab requires an asset object or null");
    return link.at("asset").get<AssetId>();
}
template <class Lookup>
std::size_t expanded_prefab_member_count(AssetId asset, const Lookup& lookup) {
    std::map<AssetId, std::size_t> counts;
    std::set<AssetId> visiting;
    std::function<std::size_t(AssetId)> count = [&](AssetId current) {
        if (const auto at = counts.find(current); at != counts.end())
            return at->second;
        if (!visiting.insert(current).second || visiting.size() > 17)
            throw std::runtime_error("Invalid nested prefab composition graph");
        const Json* definition = lookup(current);
        if (!definition)
            throw std::runtime_error("Missing nested prefab definition: " + current.str());
        const auto& members = definition->at("members");
        std::size_t rows = members.size();
        if (rows > 4096)
            throw std::runtime_error("Prefab composition exceeds4096 expanded members");
        for (const auto& member : members)
            if (const auto child = nested_prefab_asset(member, definition->at("version"))) {
                const auto size = count(*child);
                if (size > 4096 - rows)
                    throw std::runtime_error("Prefab composition exceeds4096 expanded members");
                rows += size;
            }
        visiting.erase(current);
        counts.emplace(current, rows);
        return rows;
    };
    return count(asset);
}
template <class Lookup>
void validate_prefab_composition_structure(AssetId asset, const Lookup& lookup) {
    // Scratch graph handles have no identity or persistence meaning. Reuse the
    // native relationship admission for the complete structural expansion.
    RelationshipGraph graph;
    std::set<AssetId> visiting;
    std::size_t serial = 0;
    std::function<void(AssetId, const std::string&)> expand =
        [&](AssetId current, const std::string& external_parent) {
            if (!visiting.insert(current).second || visiting.size() > 17)
                throw std::runtime_error("Invalid nested prefab structural expansion");
            const Json* definition = lookup(current);
            if (!definition || PrefabDocument::is_variant(*definition))
                throw std::runtime_error("Resolve prefab sources before structural expansion");
            std::map<std::string, std::string> handles;
            for (const auto& row : definition->at("members")) {
                if (serial >= 4096)
                    throw std::runtime_error("Prefab composition exceeds4096 expanded members");
                const auto handle = std::to_string(serial++);
                handles.emplace(row.at("id").get<std::string>(), handle);
                graph.try_emplace(handle);
            }
            for (const auto& row : definition->at("members")) {
                const auto handle = handles.at(row.at("id").get<std::string>());
                if (row.contains("parent"))
                    graph.at(handle).push_back({handles.at(row.at("parent").get<std::string>())});
                else if (!external_parent.empty())
                    graph.at(handle).push_back({external_parent});
                if (const auto child = nested_prefab_asset(row, definition->at("version")))
                    expand(*child, handle);
            }
            visiting.erase(current);
        };
    expand(asset, "");
    validate_relationship_graph(graph, "Nested prefab composition", 1);
}
// Detached dependency admission only. Raw source ownership includes retained
// declarations; dormant structure must not hide a cycle or missing dependency.
inline void validate_prefab_composition_graph(const PrefabSources& sources) {
    std::set<AssetId> visiting;
    std::map<AssetId, unsigned> depths;
    std::function<unsigned(AssetId)> visit = [&](AssetId asset) {
        if (const auto at = depths.find(asset); at != depths.end())
            return at->second;
        if (!visiting.insert(asset).second)
            throw std::runtime_error("Cyclic prefab base/nesting dependencies");
        if (visiting.size() > 17)
            throw std::runtime_error("Prefab composition exceeds16 dependency links");
        const auto at = sources.find(asset);
        if (at == sources.end())
            throw std::runtime_error("Missing prefab composition dependency: " + asset.str());
        const PrefabDocument document(at->second);
        if (document.asset() != asset)
            throw std::runtime_error("Prefab source AssetId mismatch");
        unsigned depth = 0;
        const auto edge = [&](AssetId child) { depth = std::max(depth, visit(child) + 1); };
        for (const auto base : document.base_assets())
            edge(base);
        for (const auto* rows : PrefabDocument::authored_member_arrays(document.source))
            for (const auto& member : document.source.at(rows))
                if (const auto child = nested_prefab_asset(member, document.source.at("version")))
                    edge(*child);
        if (depth > 16)
            throw std::runtime_error("Prefab composition exceeds16 dependency links");
        visiting.erase(asset);
        depths.emplace(asset, depth);
        return depth;
    };
    for (const auto& [asset, document] : sources) {
        (void)document;
        visit(asset);
    }
}
} // namespace forge::detail
