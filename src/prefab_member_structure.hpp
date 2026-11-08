#pragma once
#include "relationship_graph.hpp"
#include "spatial_document.hpp"
#include <forge/asset_ref.hpp>
#include <forge/prefab.hpp>
#include <set>
#include <stdexcept>

namespace forge::detail {
inline std::set<std::string> inherited_prefab_member_ids(AssetId base,
                                                         const PrefabSources& sources) {
    std::set<std::string> ids;
    std::set<AssetId> visited;
    while (base) {
        if (!visited.insert(base).second || visited.size() > 16)
            throw std::runtime_error("Invalid prefab identity reservation base chain");
        const auto& source = sources.at(base);
        for (const auto* rows : PrefabDocument::authored_member_arrays(source))
            for (const auto& row : source.at(rows)) {
                ids.insert(row.at("id").get<PrefabMemberId>().str());
                if (row.contains("parent"))
                    ids.insert(row.at("parent").get<PrefabMemberId>().str());
                if (row.contains("spatial") && row.at("spatial").value("mode", "") == "explicit")
                    ids.insert(row.at("spatial").at("member").get<PrefabMemberId>().str());
            }
        if (PrefabDocument::is_variant(source) && source.at("version").get<unsigned>() >= 6)
            for (const auto& order : source.value("member_order", Json::array())) {
                ids.insert(order.at("parent").get<PrefabMemberId>().str());
                for (const auto& member : order.at("members"))
                    ids.insert(member.get<PrefabMemberId>().str());
            }
        if (!PrefabDocument::is_variant(source))
            break;
        base = source.at("base").get<AssetRef<PrefabAsset>>().id;
    }
    return ids;
}
// Detached composition only. The caller owns source admission, component intent
// and inherited identity reservations; the result feeds the existing compiler.
inline Json compose_prefab_member_structure(const Json& inherited, const Json& source,
                                            const std::set<std::string>& reserved) {
    auto members = inherited;
    const auto root = source.at("root").get<PrefabMemberId>().str();
    std::map<std::string, std::size_t> index;
    std::set<std::string> removed;
    for (std::size_t i = 0; i < members.size(); ++i)
        if (!index.emplace(members[i].at("id").get<PrefabMemberId>().str(), i).second)
            throw std::runtime_error("Duplicate inherited prefab member identity");
    if (!index.contains(root))
        throw std::runtime_error("Prefab structure requires its inherited root");
    const auto added = source.value("added_members", Json::array());
    if (!added.is_array() || added.size() > 4096 || members.size() > 4096)
        throw std::runtime_error("Prefab structure exceeds its member preparation budget");
    std::set<std::string> owned;
    for (auto row : added) {
        const auto id = row.at("id").get<PrefabMemberId>().str();
        if (reserved.contains(id) || index.contains(id))
            throw std::runtime_error("Local prefab member collides with an inherited identity");
        index.emplace(id, members.size());
        owned.insert(id);
        if (row.contains("removed")) {
            if (!row.at("removed").is_boolean() || !row.at("removed").get<bool>())
                throw std::runtime_error("Remove prefab removal intent to restore a member");
            removed.insert(id);
            row.erase("removed");
        }
        members.push_back(std::move(row));
    }
    for (const auto& row : source.at("overrides")) {
        const auto id = row.at("id").get<PrefabMemberId>().str();
        if (owned.contains(id))
            throw std::runtime_error("Local member values belong to their added definition");
        if (id == root &&
            (row.contains("parent") || row.contains("spatial") || row.contains("removed")))
            throw std::runtime_error("Prefab structure cannot replace or remove its root");
        if (row.contains("removed")) {
            if (!row.at("removed").is_boolean() || !row.at("removed").get<bool>())
                throw std::runtime_error("Remove prefab removal intent to restore a member");
            removed.insert(id);
        }
        if (const auto at = index.find(id); at != index.end())
            for (const auto* key : {"parent", "spatial"})
                if (row.contains(key))
                    members[at->second][key] = row.at(key);
    }

    RelationshipGraph hierarchy;
    std::map<std::string, std::vector<std::string>> dependents;
    std::map<std::string, std::uint64_t> handles;
    const auto handle = [&](const std::string& id) {
        return handles.try_emplace(id, handles.size() + 1).first->second;
    };
    std::map<std::uint64_t, TransformNode> spatial;
    for (const auto& row : members) {
        const auto id = row.at("id").get<PrefabMemberId>().str();
        hierarchy.try_emplace(id);
        std::string parent;
        if (row.contains("parent")) {
            parent = row.at("parent").get<PrefabMemberId>().str();
            hierarchy[id].push_back({parent});
            hierarchy.try_emplace(parent); // An absent target remains unavailable.
            dependents[parent].push_back(id);
        }
        if ((id == root) != parent.empty())
            throw std::runtime_error("Prefab structure must retain its one source root");
        auto binding = row.value("spatial", Json{{"mode", "follow_structure"}});
        const auto mode = binding.at("mode").get<std::string>();
        if (id == root && mode != "follow_structure")
            throw std::runtime_error("Prefab root attachment belongs to its Scene instance");
        TransformNode node{read_local(row.at("components"))};
        if (mode == "follow_structure")
            node.parent = parent.empty() ? 0 : handle(parent);
        else if (mode == "explicit") {
            const auto target = binding.at("member").get<PrefabMemberId>().str();
            node.parent = handle(target);
            dependents[target].push_back(id);
        } else if (mode != "world")
            throw std::runtime_error("Invalid prefab spatial mode");
        spatial.emplace(handle(id), node);
    }
    // An absent inherited row has no invented definition or default binding.
    // Its explicitly retained links still participate in known intent cycles.
    for (const auto& row : source.at("overrides")) {
        const auto id = row.at("id").get<PrefabMemberId>().str();
        if (index.contains(id))
            continue;
        hierarchy.try_emplace(id);
        if (row.contains("parent")) {
            const auto parent = row.at("parent").get<PrefabMemberId>().str();
            hierarchy[id].push_back({parent});
            hierarchy.try_emplace(parent);
        }
        if (row.contains("spatial")) {
            const auto& binding = row.at("spatial");
            const auto mode = binding.at("mode").get<std::string>();
            TransformNode node{};
            if (mode == "explicit")
                node.parent = handle(binding.at("member").get<PrefabMemberId>().str());
            else if (mode == "follow_structure" && row.contains("parent"))
                node.parent = handle(row.at("parent").get<PrefabMemberId>().str());
            spatial.emplace(handle(id), node);
        }
    }
    validate_relationship_graph(hierarchy, "Prefab structural intent", 1);
    // Spatial depth has its existing independent transform contract. Missing
    // targets remain unresolved; do not impose the structural Flecs depth here.
    (void)evaluate_transforms(spatial);
    std::vector<std::string> unavailable(removed.begin(), removed.end());
    std::set<std::string> hidden = removed;
    for (const auto& [target, rows] : dependents) {
        (void)rows;
        if (!index.contains(target) && hidden.insert(target).second)
            unavailable.push_back(target);
    }
    for (std::size_t i = 0; i < unavailable.size(); ++i)
        if (const auto at = dependents.find(unavailable[i]); at != dependents.end())
            for (const auto& child : at->second)
                if (hidden.insert(child).second)
                    unavailable.push_back(child);
    if (hidden.contains(root))
        throw std::runtime_error("Prefab structure cannot make its root unavailable");
    auto active = Json::array();
    for (const auto& row : members)
        if (!hidden.contains(row.at("id").get<std::string>()))
            active.push_back(row);

    std::set<std::string> ordered_parents;
    std::size_t order_budget = 0;
    for (const auto& order : source.value("member_order", Json::array())) {
        const auto parent = order.at("parent").get<PrefabMemberId>().str();
        const auto& prefix = order.at("members");
        if (!ordered_parents.insert(parent).second || !prefix.is_array() ||
            prefix.size() > 4096 - order_budget)
            throw std::runtime_error(
                "Prefab sibling order exceeds its unique-parent/4096-ID budget");
        order_budget += prefix.size();
        std::set<std::string> requested;
        std::vector<std::size_t> slots;
        std::map<std::string, std::size_t> siblings;
        for (std::size_t i = 0; i < active.size(); ++i)
            if (active[i].value("parent", "") == parent) {
                slots.push_back(i);
                siblings.emplace(active[i].at("id").get<std::string>(), i);
            }
        std::vector<Json> values;
        for (const auto& value : prefix) {
            const auto id = value.get<PrefabMemberId>().str();
            if (id == parent || !requested.insert(id).second)
                throw std::runtime_error("Prefab sibling order requires distinct child identities");
            if (const auto at = siblings.find(id); at != siblings.end())
                values.push_back(active[at->second]);
        }
        for (const auto i : slots)
            if (!requested.contains(active[i].at("id").get<std::string>()))
                values.push_back(active[i]);
        for (std::size_t i = 0; i < slots.size(); ++i)
            active[slots[i]] = std::move(values[i]);
    }
    return active;
}
} // namespace forge::detail
