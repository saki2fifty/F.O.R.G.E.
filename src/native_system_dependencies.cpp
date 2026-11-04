#include "native_system_dependencies.hpp"
#include "native_system_access.hpp"
#include <map>
#include <set>
#include <stdexcept>
#include <vector>
namespace forge::detail {
Json native_system_dependencies(flecs::world& world, std::span<const ecs_entity_t> roots) {
    if (roots.size() > native_system_plan_capacity)
        throw std::runtime_error("Native dependency root capacity exceeded");
    struct Node {
        ecs_entity_t id;
        std::string name;
        bool root, phase_tag, truncated = false;
    };
    std::vector<Node> nodes;
    nodes.reserve(native_dependency_node_capacity);
    std::map<ecs_entity_t, std::size_t> indices;
    const auto add = [&](ecs_entity_t id, bool root) {
        if (const auto found = indices.find(id); found != indices.end()) {
            nodes[found->second].root |= root;
            return;
        }
        if (!id || !ecs_is_alive(world, id))
            throw std::runtime_error("Native dependency target is unavailable");
        const auto index = nodes.size();
        nodes.push_back(
            {id, native_system_display_name(world, id), root, ecs_has_id(world, id, EcsPhase)});
        indices.emplace(id, index);
    };
    for (auto id : roots)
        add(id, true);
    Json links = Json::array();
    // Native lookup may include inherited results. Follow the API's returned
    // targets, not a reconstructed IsA union or independently sorted schedule.
    for (std::size_t cursor = 0; cursor < nodes.size(); ++cursor) {
        const auto source = nodes[cursor].id;
        for (unsigned slot = 0; slot <= native_dependency_target_capacity; ++slot) {
            const auto target = ecs_get_target(world, source, EcsDependsOn, int(slot));
            if (!target)
                break;
            if (slot == native_dependency_target_capacity ||
                links.size() == native_dependency_link_capacity ||
                (nodes.size() == native_dependency_node_capacity && !indices.contains(target))) {
                nodes[cursor].truncated = true;
                break;
            }
            add(target, false);
            links.push_back(
                {{"source", source},
                 {"target", target},
                 {"slot", slot},
                 {"pair_owned", ecs_owns_id(world, source, ecs_pair(EcsDependsOn, target))}});
        }
    }
    Json copied = Json::array();
    bool truncated = false;
    for (const auto& node : nodes) {
        truncated |= node.truncated;
        copied.push_back({{"id", node.id},
                          {"name", node.name},
                          {"root", node.root},
                          {"phase_tag", node.phase_tag},
                          {"truncated", node.truncated}});
    }
    return {{"lookup", "native_depends_on_targets"},
            {"node_capacity", native_dependency_node_capacity},
            {"link_capacity", native_dependency_link_capacity},
            {"target_capacity", native_dependency_target_capacity},
            {"truncated", truncated},
            {"nodes", std::move(copied)},
            {"links", std::move(links)}};
}
void validate_native_system_dependencies(std::span<const ecs_entity_t> root_ids,
                                         const Json& snapshot) {
    const auto& nodes = snapshot.at("nodes");
    const auto& links = snapshot.at("links");
    if (root_ids.size() > native_system_plan_capacity || !nodes.is_array() ||
        nodes.size() > native_dependency_node_capacity || !links.is_array() ||
        links.size() > native_dependency_link_capacity ||
        snapshot.at("lookup") != "native_depends_on_targets" ||
        !snapshot.at("node_capacity").is_number_unsigned() ||
        snapshot.at("node_capacity") != native_dependency_node_capacity ||
        !snapshot.at("link_capacity").is_number_unsigned() ||
        snapshot.at("link_capacity") != native_dependency_link_capacity ||
        !snapshot.at("target_capacity").is_number_unsigned() ||
        snapshot.at("target_capacity") != native_dependency_target_capacity ||
        !snapshot.at("truncated").is_boolean())
        throw std::runtime_error("Invalid native dependency snapshot bounds");
    std::set<ecs_entity_t> roots;
    for (const auto id : root_ids) {
        if (!id)
            throw std::runtime_error("Invalid native dependency root");
        roots.insert(id);
    }
    std::map<ecs_entity_t, unsigned> next;
    std::set<ecs_entity_t> observed_roots;
    bool truncated = false;
    for (const auto& node : nodes) {
        if (!node.at("id").is_number_unsigned() || node.at("id") == 0 ||
            !node.at("name").is_string() ||
            node.at("name").get_ref<const std::string&>().size() > 480 ||
            !node.at("root").is_boolean() || !node.at("phase_tag").is_boolean() ||
            !node.at("truncated").is_boolean())
            throw std::runtime_error("Invalid native dependency node");
        const auto id = node.at("id").get<ecs_entity_t>();
        if (!next.emplace(id, 0).second || node.at("root") != roots.contains(id))
            throw std::runtime_error("Duplicate or incorrectly scoped native dependency node");
        if (node.at("root").get<bool>())
            observed_roots.insert(id);
        truncated |= node.at("truncated").get<bool>();
    }
    if (roots != observed_roots || snapshot.at("truncated") != truncated)
        throw std::runtime_error("Incomplete native dependency roots or truncation metadata");
    std::map<ecs_entity_t, std::vector<ecs_entity_t>> outgoing;
    for (const auto& link : links) {
        if (!link.at("source").is_number_unsigned() || !link.at("target").is_number_unsigned() ||
            !link.at("slot").is_number_unsigned() ||
            link.at("slot") >= native_dependency_target_capacity ||
            !link.at("pair_owned").is_boolean())
            throw std::runtime_error("Invalid native dependency link");
        const auto source = next.find(link.at("source").get<ecs_entity_t>());
        if (source == next.end() || !next.contains(link.at("target").get<ecs_entity_t>()) ||
            link.at("slot") != source->second++)
            throw std::runtime_error("Native dependency link lost its node or lookup slot");
        outgoing[source->first].push_back(link.at("target").get<ecs_entity_t>());
    }
    // Scope/shape validation only; this does not infer execution order or DAG validity.
    std::set<ecs_entity_t> reachable = roots;
    std::vector<ecs_entity_t> pending(roots.begin(), roots.end());
    for (std::size_t cursor = 0; cursor < pending.size(); ++cursor)
        if (const auto found = outgoing.find(pending[cursor]); found != outgoing.end())
            for (const auto target : found->second)
                if (reachable.insert(target).second)
                    pending.push_back(target);
    if (reachable.size() != nodes.size())
        throw std::runtime_error("Native dependency node is outside the captured roots");
}
} // namespace forge::detail
