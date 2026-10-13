#pragma once
#include <algorithm>
#include <cmath>
#include <forge/material_graph.hpp>
#include <limits>
namespace forge {
// Private authoring operations; the existing source document owns commit/history.
enum class GraphLayout { Start, Center, End, Distribute, Grid };
inline double material_graph_node_height(std::size_t inputs, std::size_t outputs) {
    return 42 + 24.0 * std::max(inputs, outputs);
}
inline std::array<double, 2> material_graph_node_size(const nlohmann::json& node) {
    std::size_t rows = 0;
    try {
        rows = std::max(material_graph_inputs(node).size(), material_graph_outputs(node).size());
    } catch (const std::exception&) {
        // The canvas preserves unavailable schemas and draws their header.
    }
    return {240, material_graph_node_height(rows, 0)};
}
inline bool arrange_material_graph(nlohmann::json& graph, const std::set<GraphNodeId>& ids,
                                   GraphLayout action, unsigned axis = 0, double spacing = 16) {
    if (ids.empty() || axis > 1 ||
        (action == GraphLayout::Grid && (!std::isfinite(spacing) || spacing < 1 || spacing > 1024)))
        throw std::runtime_error("Select graph nodes and a valid arrangement/grid.");
    struct Item {
        GraphNodeId id;
        std::size_t index;
        double position, size;
    };
    std::vector<Item> items;
    for (std::size_t i = 0; i < graph.at("nodes").size(); ++i) {
        const auto& node = graph.at("nodes").at(i);
        const auto id = node.at("id").get<GraphNodeId>();
        if (ids.contains(id))
            items.push_back({id, i, node.at("position").at(axis).get<double>(),
                             material_graph_node_size(node)[axis]});
    }
    if (items.size() != ids.size() || (action == GraphLayout::Distribute && items.size() < 3))
        throw std::runtime_error("Arrangement needs existing nodes; spacing needs at least three.");
    auto next = graph;
    double low = std::numeric_limits<double>::infinity(), high = -low;
    for (const auto& item : items) {
        low = std::min(low, item.position);
        high = std::max(high, item.position + item.size);
    }
    if (action == GraphLayout::Distribute) {
        std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
            const auto ac = a.position + a.size / 2, bc = b.position + b.size / 2;
            return ac == bc ? a.id < b.id : ac < bc;
        });
        const double first = items.front().position + items.front().size / 2,
                     last = items.back().position + items.back().size / 2;
        // Keep endpoints exactly, including their authored numeric representation.
        if (first != last)
            for (std::size_t i = 1; i + 1 < items.size(); ++i)
                next["nodes"][items[i].index]["position"][axis] =
                    first + (last - first) * double(i) / double(items.size() - 1) -
                    items[i].size / 2;
    } else
        for (const auto& item : items) {
            auto& position = next["nodes"][item.index]["position"];
            if (action == GraphLayout::Grid) {
                for (unsigned lane = 0; lane < 2; ++lane)
                    position[lane] = std::round(position[lane].get<double>() / spacing) * spacing;
            } else
                position[axis] = action == GraphLayout::Start    ? low
                                 : action == GraphLayout::Center ? (low + high - item.size) / 2
                                                                 : high - item.size;
        }
    validate_material_graph_document(next);
    if (next == graph)
        return false;
    graph = std::move(next);
    return true;
}
// Ignore only declared layout fields. Unknown data, identities/order, function
// definitions, binding labels and all program inputs remain in this comparison.
inline bool material_graph_layout_equivalent(const nlohmann::json& a, const nlohmann::json& b) {
    auto without_positions = [](nlohmann::json graph) {
        for (auto& node : graph.at("nodes"))
            node.erase("position");
        if (graph.contains("functions"))
            for (auto& function : graph["functions"])
                for (auto& node : function.at("graph").at("nodes"))
                    node.erase("position");
        return graph;
    };
    return without_positions(a) == without_positions(b);
}
} // namespace forge
