#pragma once
#include "../render_bounds.hpp"
#include "decal_guide.hpp"
#include "navigation_region_view.hpp"
#include "reflection_probe_view.hpp"
#include <algorithm>
#include <cmath>
#include <forge/scene.hpp>
#include <map>
#include <set>
#include <span>
namespace forge {
// Transient selection expansion: structural descendants, independent of spatial
// parenting. A model root therefore frames its complete placed subtree.
inline std::set<EntityId> framing_entities(const Json& source,
                                           std::span<const std::string> selected) {
    if (std::none_of(selected.begin(), selected.end(), [](const auto& id) { return !id.empty(); }))
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
    std::vector<EntityId> pending;
    pending.reserve(selected.size());
    for (const auto& id : selected)
        if (!id.empty())
            pending.push_back(EntityId::parse(id));
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
inline std::set<EntityId> framing_entities(const Json& source, const std::string& selected) {
    return framing_entities(source, std::span<const std::string>(&selected, 1));
}
// Retained render poses own art bounds. Selected navigation footprints also
// contribute their full XY extent; other non-mesh helpers contribute positions.
inline RenderBounds include_selected_helper_positions(const Json& source,
                                                      const std::set<EntityId>& selected,
                                                      const std::set<EntityId>& mesh_entities,
                                                      RenderBounds bounds) {
    for (const auto& row : source.at("entities")) {
        const auto id = row.at("id").get<EntityId>();
        if (!selected.contains(id) || row.value("prefab", false) ||
            !row.value("spatial_resolved", true) || !row.contains("world_affine"))
            continue;
        if (const auto region = navigation_region_view(row)) {
            for (const auto& point : region->outline)
                for (unsigned axis = 0; axis < 3; ++axis) {
                    bounds.minimum[axis] = std::min(bounds.minimum[axis], point[axis]);
                    bounds.maximum[axis] = std::max(bounds.maximum[axis], point[axis]);
                }
            continue;
        }
        if (const auto decal = ui::decal_guide(row))
            for (const auto& point : decal->outline)
                for (unsigned axis = 0; axis < 3; ++axis) {
                    bounds.minimum[axis] = std::min(bounds.minimum[axis], point[axis]);
                    bounds.maximum[axis] = std::max(bounds.maximum[axis], point[axis]);
                }
        if (const auto probe = ui::reflection_probe_view(row))
            for (const auto& point : probe->outline)
                for (unsigned axis = 0; axis < 3; ++axis) {
                    bounds.minimum[axis] = std::min(bounds.minimum[axis], point[axis]);
                    bounds.maximum[axis] = std::max(bounds.maximum[axis], point[axis]);
                }
        if (mesh_entities.contains(id))
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
