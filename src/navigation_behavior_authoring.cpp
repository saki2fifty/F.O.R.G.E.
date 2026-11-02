#include "navigation_behavior_authoring.hpp"
#include "asset_single_source_importer.hpp"
#include "state_graph_layout.hpp"
#include <algorithm>
#include <set>

namespace forge {
namespace {
Json& tree_rows(Json& source) {
    if (!source.at("version").is_number_integer() || source.at("version") != 2)
        throw std::runtime_error("This operation requires a behavior tree source");
    return source.at("nodes");
}
Json& tree_row(Json& source, std::uint32_t id) {
    auto& rows = tree_rows(source);
    const auto found = std::find_if(rows.begin(), rows.end(),
                                    [id](const auto& row) { return row.at("id") == id; });
    if (found == rows.end())
        throw std::runtime_error("Behavior tree node is missing");
    return *found;
}
void configure_tree_node(Json& node, NavigationBehaviorTreeKind kind) {
    node["kind"] = navigation_behavior_tree_kind_name(kind);
    if (kind == NavigationBehaviorTreeKind::Sequence ||
        kind == NavigationBehaviorTreeKind::ReactiveSequence ||
        kind == NavigationBehaviorTreeKind::Selector) {
        if (!node.contains("children"))
            node["children"] = Json::array();
    } else if (node.contains("children"))
        node["children"] = Json::array();
    if (kind == NavigationBehaviorTreeKind::Wait && !node.contains("seconds"))
        node["seconds"] = 1;
    if (kind == NavigationBehaviorTreeKind::MoveToPoint && !node.contains("point"))
        node["point"] = {0, 0, 0};
    if ((kind == NavigationBehaviorTreeKind::TargetNear ||
         kind == NavigationBehaviorTreeKind::TargetFar) &&
        !node.contains("distance"))
        node["distance"] = 3;
}
} // namespace
void set_navigation_behavior_node_kind(Json& source, std::uint32_t id,
                                       NavigationBehaviorTreeKind kind) {
    configure_tree_node(tree_row(source, id), kind);
}
std::uint32_t add_navigation_behavior_node(Json& source, NavigationBehaviorTreeKind kind) {
    auto& rows = tree_rows(source);
    const auto id = source.at("next_id").get<std::uint64_t>();
    if (id > UINT32_MAX || rows.size() >= 32)
        throw std::runtime_error("Behavior tree node or identity capacity reached");
    Json node{{"id", id}, {"name", "Node " + std::to_string(id)}};
    configure_tree_node(node, kind);
    rows.push_back(std::move(node));
    source["next_id"] = id + 1;
    if (source.at("entry") == 0)
        source["entry"] = id;
    return std::uint32_t(id);
}
void connect_navigation_behavior_nodes(Json& source, std::uint32_t parent, std::uint32_t child) {
    auto& from = tree_row(source, parent);
    (void)tree_row(source, child);
    if (parent == child || source.at("entry") == child)
        throw std::runtime_error("Behavior tree root cannot have a parent");
    if (!from.contains("children") || from.at("children").size() >= 8)
        throw std::runtime_error("Choose a composite with fewer than eight children");
    for (const auto& node : source.at("nodes"))
        if (node.contains("children"))
            for (const auto& existing : node.at("children"))
                if (existing == child)
                    throw std::runtime_error("Disconnect the node's existing parent first");
    from["children"].push_back(child);
}
void remove_navigation_behavior_node(Json& source, std::uint32_t id) {
    auto& rows = tree_rows(source);
    (void)tree_row(source, id);
    std::erase_if(rows.get_ref<Json::array_t&>(),
                  [id](const auto& row) { return row.at("id") == id; });
    std::set<std::uint32_t> children;
    for (auto& row : rows)
        if (row.contains("children")) {
            auto& edges = row["children"].get_ref<Json::array_t&>();
            std::erase_if(edges, [id](const auto& child) { return child == id; });
            for (const auto& child : edges)
                children.insert(child.get<std::uint32_t>());
        }
    asset_detail::remove_state_graph_position(source, id, navigation_behavior_layout_format);
    if (source.at("entry") == id) {
        source["entry"] = 0;
        for (const auto& row : rows)
            if (!children.contains(row.at("id").get<std::uint32_t>())) {
                source["entry"] = row.at("id");
                break;
            }
    }
}
std::uint32_t add_navigation_behavior_state(nlohmann::json& source) {
    const auto id = source.at("next_id").get<std::uint64_t>();
    if (id > UINT32_MAX || source.at("states").size() >= 32)
        throw std::runtime_error("Behavior state or identity capacity reached");
    source["states"].push_back({{"id", id},
                                {"name", "State " + std::to_string(id)},
                                {"action", {{"kind", "wait"}}},
                                {"transitions", nlohmann::json::array()}});
    source["next_id"] = id + 1;
    if (source.at("entry") == 0)
        source["entry"] = id;
    return std::uint32_t(id);
}
std::uint32_t connect_navigation_behavior_states(nlohmann::json& source, std::uint32_t from,
                                                 std::uint32_t to) {
    auto& states = source.at("states");
    auto at = std::find_if(states.begin(), states.end(),
                           [&](const auto& s) { return s.at("id") == from; });
    const auto target =
        std::find_if(states.begin(), states.end(), [&](const auto& s) { return s.at("id") == to; });
    std::size_t total = 0;
    for (const auto& s : states)
        total += s.at("transitions").size();
    const auto id = source.at("next_id").get<std::uint64_t>();
    if (at == states.end() || target == states.end())
        throw std::runtime_error("Behavior transition state is missing");
    if (id > UINT32_MAX || at->at("transitions").size() >= 8 || total >= 128)
        throw std::runtime_error("Behavior transition or identity capacity reached");
    (*at)["transitions"].push_back(
        {{"id", id}, {"to", to}, {"condition", {{"kind", "elapsed"}, {"seconds", 1}}}});
    source["next_id"] = id + 1;
    return std::uint32_t(id);
}
void remove_navigation_behavior_state(nlohmann::json& source, std::uint32_t id) {
    auto& states = source.at("states");
    const auto at =
        std::find_if(states.begin(), states.end(), [&](const auto& s) { return s.at("id") == id; });
    if (at == states.end())
        throw std::runtime_error("Behavior state is missing");
    states.erase(at);
    for (auto& s : states) {
        auto& edges = s.at("transitions");
        for (auto e = edges.begin(); e != edges.end();)
            e = e->at("to") == id ? edges.erase(e) : std::next(e);
    }
    asset_detail::remove_state_graph_position(source, id, navigation_behavior_layout_format);
    if (source.at("entry") == id)
        source["entry"] = states.empty() ? nlohmann::json(0) : states.front().at("id");
}
namespace {
const SingleSourceImportProfile<NavigationBehaviorSource> profile{
    .importer = "forge.navigation.behavior",
    .revision = "forge.navigation.behavior.v1",
    .label = "Navigation Behavior",
    .description = "Cook bounded patrol, wait and target-chase state graphs.",
    .suffix = ".behavior.json",
    .format = "forge.navigation-behavior",
    .asset_type = NavigationBehaviorAsset::type,
    .file = "behavior.json",
    .evidence_schema = "forge.navigation-behavior.single.v1",
    .metadata_key = "forge.navigation_behavior",
    .source_limit = navigation_behavior_byte_limit,
    .cooked_limit = navigation_behavior_cooked_byte_limit,
    .parse_cooked = NavigationBehaviorSource::parse_cooked};
} // namespace
std::shared_ptr<const AssetImporterRegistry> navigation_behavior_import_registry() {
    return single_source_import_registry(profile);
}
void prepare_navigation_behavior_publication(AssetPublicationCandidate& candidate,
                                             const AssetImportPlan& plan) {
    prepare_single_source_publication(candidate, plan, profile);
}
} // namespace forge
