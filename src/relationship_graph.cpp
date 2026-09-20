#include "relationship_graph.hpp"
#include <algorithm>
#include <flecs.h>
#include <stdexcept>
namespace forge::detail {
void validate_relationship_graph(const RelationshipGraph& graph, const char* relationship,
                                 unsigned reserved_depth) {
    if (reserved_depth >= FLECS_DAG_DEPTH_MAX)
        throw std::runtime_error("Invalid internal relationship depth reservation");
    const auto limit = FLECS_DAG_DEPTH_MAX - reserved_depth;
    struct Node {
        std::size_t incoming = 0, depth = 1;
    };
    std::map<std::string, Node> state;
    for (const auto& [id, edges] : graph) {
        (void)edges;
        state.emplace(id, Node{});
    }
    for (const auto& [id, edges] : graph) {
        (void)id;
        for (const auto& edge : edges) {
            const auto found = state.find(edge.target);
            if (found == state.end())
                throw std::runtime_error(std::string("Missing ") + relationship +
                                         " target: " + edge.target);
            ++found->second.incoming;
        }
    }
    std::vector<std::string> ready;
    ready.reserve(graph.size());
    for (const auto& [id, node] : state)
        if (!node.incoming)
            ready.push_back(id);
    for (std::size_t i = 0; i < ready.size(); ++i) {
        const auto& id = ready[i];
        const auto depth = state.at(id).depth;
        if (depth > limit)
            throw std::runtime_error(std::string(relationship) +
                                     " hierarchy exceeds the pinned Flecs limit of " +
                                     std::to_string(limit) + " authored levels at " + id);
        for (const auto& edge : graph.at(id)) {
            auto& child = state.at(edge.target);
            child.depth = std::max(child.depth, depth + (edge.advances_depth ? 1 : 0));
            if (!--child.incoming)
                ready.push_back(edge.target);
        }
    }
    if (ready.size() != graph.size())
        throw std::runtime_error(std::string("Cyclic ") + relationship + " hierarchy");
}
} // namespace forge::detail
