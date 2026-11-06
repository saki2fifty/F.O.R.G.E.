#pragma once
#include "../animation_graph_layout.hpp"
#include "../state_graph_layout.hpp"
#include <forge/animation_graph.hpp>
#include <map>

namespace forge::ui {
// Transient numeric handles adapt the shared canvas. Only source UUIDs are saved.
struct AnimationGraphCanvasModel {
    using Json = nlohmann::json;
    Json projected;
    std::vector<AssetId> identities;
    std::map<AssetId, std::uint32_t> handles;
    explicit AnimationGraphCanvasModel(const Json& source) {
        projected = {{"states", Json::array()}, {"entry", 0}};
        for (const auto& node : source.at("nodes")) {
            const auto id = node.at("id").get<AssetId>();
            identities.push_back(id);
            handles.emplace(id, std::uint32_t(identities.size()));
        }
        for (std::size_t i = 0; i < identities.size(); ++i) {
            const auto& node = source.at("nodes").at(i);
            const auto handle = std::uint32_t(i + 1);
            Json row{{"id", handle}, {"name", node.at("name")}, {"kind", node.at("kind")}};
            if (node.at("kind") != "clip")
                for (const auto* port : {"first", "second"})
                    row[port] =
                        node.at(port).is_null() ? 0u : handles.at(node.at(port).get<AssetId>());
            projected["states"].push_back(std::move(row));
            asset_detail::set_state_graph_position(
                projected, handle,
                asset_detail::animation_graph_position(source, identities[i], i));
        }
        if (!source.at("output").is_null())
            projected["entry"] = handles.at(source.at("output").get<AssetId>());
    }
    AssetId identity(std::uint32_t handle) const {
        return handle ? identities.at(handle - 1) : AssetId{};
    }
    std::uint32_t handle(AssetId id) const {
        const auto at = handles.find(id);
        return at == handles.end() ? 0 : at->second;
    }
    void apply(Json& source, const Json& edited) const {
        // Revision checks belong to SourceDocument. Also reject a different
        // identity projection before interpreting any transient handles.
        if (source.at("nodes").size() != identities.size() ||
            edited.at("states").size() != identities.size())
            throw std::runtime_error("Animation Graph canvas projection changed");
        for (std::size_t i = 0; i < identities.size(); ++i) {
            if (source.at("nodes").at(i).at("id").get<AssetId>() != identities[i])
                throw std::runtime_error("Animation Graph canvas identity changed");
            const auto h = std::uint32_t(i + 1);
            const auto before = asset_detail::state_graph_position(projected, h, i);
            const auto after = asset_detail::state_graph_position(edited, h, i);
            if (before != after)
                asset_detail::set_animation_graph_position(source, identities[i], after);
            const auto& row = projected.at("states").at(i);
            if (row.at("kind") != "clip")
                for (const auto* port : {"first", "second"})
                    if (row.at(port) != edited.at("states").at(i).at(port)) {
                        const auto value = edited.at("states").at(i).at(port).get<std::uint32_t>();
                        source["nodes"][i][port] = value ? Json(identity(value)) : Json(nullptr);
                    }
        }
    }
};
} // namespace forge::ui
