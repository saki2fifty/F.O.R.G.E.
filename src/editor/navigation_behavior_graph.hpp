#pragma once
#include "../navigation_behavior_authoring.hpp"
#include "state_graph_canvas.hpp"
namespace forge {
class NavigationBehaviorGraphView {
  public:
    using Edit = StateGraphCanvas::Edit;
    bool draft_dirty() const { return canvas_.draft_dirty(); }
    void cancel() { canvas_.cancel(); }
    void reset() { canvas_.reset(); }
    bool flush(const Edit& edit) { return canvas_.flush(edit); }
    void draw(const Json& source, std::uint64_t revision, std::uint32_t state, std::uint32_t edge,
              bool locked, const Edit& edit, const std::function<void(std::uint32_t)>& select,
              const std::function<void(std::uint32_t)>& select_edge) {
        std::vector<StateGraphConnection> edges;
        const bool tree = source.at("version") == 2;
        if (tree) {
            for (const auto& node : source.at("nodes"))
                if (node.contains("children")) {
                    unsigned order = 0;
                    for (const auto& child : node.at("children"))
                        edges.push_back(
                            {child, node.at("id"), child, "Order " + std::to_string(++order)});
                }
        } else
            for (const auto& s : source.at("states"))
                for (const auto& e : s.at("transitions")) {
                    const auto& condition = e.at("condition");
                    auto label = condition.at("kind").get<std::string>();
                    if (label == "elapsed")
                        label += " " + std::to_string(condition.at("seconds").get<double>()) +
                                 " seconds";
                    else if (label == "target_near" || label == "target_far")
                        label += " " + std::to_string(condition.at("distance").get<double>());
                    edges.push_back({e.at("id"), s.at("id"), e.at("to"), std::move(label)});
                }
        canvas_.draw(
            source, revision, state, locked, edit, select, edges,
            [tree](Json& draft, std::uint32_t from, std::uint32_t to) {
                if (tree)
                    connect_navigation_behavior_nodes(draft, from, to);
                else
                    (void)connect_navigation_behavior_states(draft, from, to);
            },
            select_edge, edge, {}, 0, tree ? "nodes" : "states", tree ? "nodes" : "states",
            tree ? "Root" : "Entry", tree ? 480 : 250);
    }

  private:
    StateGraphCanvas canvas_{{"Navigation behavior canvas", "behavior_graph",
                              navigation_behavior_layout_format, "Move behavior state",
                              "Arrange behavior states", "Connect behavior states", true}};
};
} // namespace forge
