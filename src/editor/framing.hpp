#pragma once
#include <forge/scene.hpp>
#include <map>
#include <set>
namespace forge {
// Transient selection expansion: structural descendants, independent of spatial
// parenting. A model root therefore frames its complete placed subtree.
inline std::set<EntityId> framing_entities(const Json& source, const std::string& selected) {
    if (selected.empty())
        return {};
    const auto& rows = source.at("entities");
    if (rows.size() > 10000)
        throw std::runtime_error("Scene framing exceeds entity profile");
    std::multimap<EntityId, EntityId> children;
    for (const auto& row : rows) {
        const auto parent = row.value("parent", std::string{});
        if (!parent.empty())
            children.emplace(EntityId::parse(parent), row.at("id").get<EntityId>());
    }
    std::set<EntityId> result;
    std::vector<EntityId> pending{EntityId::parse(selected)};
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        if (!result.insert(id).second)
            continue;
        const auto [first, last] = children.equal_range(id);
        for (auto child = first; child != last; ++child)
            pending.push_back(child->second);
    }
    return result;
}
} // namespace forge
