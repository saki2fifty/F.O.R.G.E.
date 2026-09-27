#pragma once
#include "../render_bounds.hpp"
#include <cmath>
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
// Mesh bounds come from retained render poses. Camera/light helpers have no mesh:
// include their derived world positions only when framing a selected mesh alongside them.
inline RenderBounds include_selected_helper_positions(const Json& source,
                                                      const std::set<EntityId>& selected,
                                                      const std::set<EntityId>& mesh_entities,
                                                      RenderBounds bounds) {
    for (const auto& row : source.at("entities")) {
        const auto id = row.at("id").get<EntityId>();
        if (!selected.contains(id) || mesh_entities.contains(id) || row.value("prefab", false) ||
            !row.value("spatial_resolved", true) || !row.contains("world_affine"))
            continue;
        const auto world = row.at("world_affine").get<std::array<double, 12>>();
        for (unsigned axis = 0; axis < 3; ++axis) {
            const double point = world[axis * 4 + 3];
            if (!std::isfinite(point))
                throw std::runtime_error("Selected helper position is nonfinite");
            bounds.minimum[axis] = std::min(bounds.minimum[axis], point);
            bounds.maximum[axis] = std::max(bounds.maximum[axis], point);
        }
    }
    return bounds;
}
} // namespace forge
