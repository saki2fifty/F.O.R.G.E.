#include "bounded_json.hpp"
#include "navigation_behavior_tree.hpp"
#include "state_graph_layout.hpp"
#include <algorithm>
#include <cmath>
#include <forge/navigation_behavior.hpp>
#include <functional>
#include <limits>
#include <set>

namespace forge {
namespace {
using Json = nlohmann::json;
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
std::uint64_t identifier(const Json& value, std::uint64_t maximum = UINT32_MAX) {
    require(value.is_number_unsigned() ||
                (value.is_number_integer() && value.get<std::int64_t>() >= 0),
            "Behavior identity must be a nonnegative integer");
    const auto id = value.get<std::uint64_t>();
    require(id <= maximum, "Behavior identity exceeds its supported range");
    return id;
}
bool version_one(const Json& value) { return value.is_number_integer() && value == 1; }
NavigationBehaviorAction action(const Json& value) {
    require(value.is_string(), "Behavior action kind must be a string");
    for (auto kind : {NavigationBehaviorAction::Wait, NavigationBehaviorAction::MoveToPoint,
                      NavigationBehaviorAction::FollowTarget})
        if (value == navigation_behavior_action_name(kind))
            return kind;
    throw std::runtime_error("Unsupported navigation behavior action");
}
NavigationBehaviorCondition condition(const Json& value) {
    require(value.is_string(), "Behavior condition kind must be a string");
    for (auto kind :
         {NavigationBehaviorCondition::Elapsed, NavigationBehaviorCondition::Arrived,
          NavigationBehaviorCondition::TargetNear, NavigationBehaviorCondition::TargetFar,
          NavigationBehaviorCondition::TargetMissing})
        if (value == navigation_behavior_condition_name(kind))
            return kind;
    throw std::runtime_error("Unsupported navigation behavior condition");
}
double bounded_number(const Json& value, double minimum, double maximum) {
    require(value.is_number(), "Behavior value must be numeric");
    const double number = value.get<double>();
    require(std::isfinite(number) && number >= minimum && number <= maximum,
            "Behavior value exceeds its supported range");
    return number;
}
double condition_value(const Json& source, NavigationBehaviorCondition kind) {
    if (kind == NavigationBehaviorCondition::Elapsed)
        return bounded_number(source.at("seconds"), 0, 3600);
    if (kind == NavigationBehaviorCondition::TargetNear ||
        kind == NavigationBehaviorCondition::TargetFar)
        return bounded_number(source.at("distance"), 0, 8192);
    return 0;
}
NavigationBehaviorTreeKind tree_kind(const Json& value) {
    require(value.is_string(), "Behavior tree kind must be a string");
    for (const auto kind :
         {NavigationBehaviorTreeKind::Sequence, NavigationBehaviorTreeKind::ReactiveSequence,
          NavigationBehaviorTreeKind::Selector, NavigationBehaviorTreeKind::Wait,
          NavigationBehaviorTreeKind::MoveToPoint, NavigationBehaviorTreeKind::FollowTarget,
          NavigationBehaviorTreeKind::TargetNear, NavigationBehaviorTreeKind::TargetFar,
          NavigationBehaviorTreeKind::TargetMissing})
        if (value == navigation_behavior_tree_kind_name(kind))
            return kind;
    throw std::runtime_error("Unsupported behavior tree node kind");
}
NavigationBehaviorGraph tree_graph(const Json& source, bool publish) {
    NavigationBehaviorGraph graph;
    graph.asset = source.at("asset_id").get<AssetId>();
    graph.entry = std::uint32_t(identifier(source.at("entry")));
    graph.profile = NavigationBehaviorProfile::Tree;
    const auto next = identifier(source.at("next_id"), std::uint64_t(UINT32_MAX) + 1);
    const auto& rows = source.at("nodes");
    require(rows.is_array() && rows.size() <= 32, "Behavior tree supports at most 32 nodes");
    std::set<std::uint32_t> ids, children;
    std::set<std::string> names;
    for (const auto& row : rows) {
        NavigationBehaviorTreeNode node;
        const auto id = identifier(row.at("id"));
        require(id && id < next && ids.insert(std::uint32_t(id)).second,
                "Behavior tree node identity is unallocated or duplicated");
        node.id = std::uint32_t(id);
        node.name = row.at("name").get<std::string>();
        require(!node.name.empty() && node.name.size() <= 64 && names.insert(node.name).second &&
                    std::none_of(node.name.begin(), node.name.end(),
                                 [](unsigned char c) { return c < 32 || c == 127; }),
                "Behavior tree names must be unique and contain 1-64 printable bytes");
        node.kind = tree_kind(row.at("kind"));
        const bool composite = node.kind == NavigationBehaviorTreeKind::Sequence ||
                               node.kind == NavigationBehaviorTreeKind::ReactiveSequence ||
                               node.kind == NavigationBehaviorTreeKind::Selector;
        if (composite) {
            const auto& edges = row.at("children");
            require(edges.is_array() && edges.size() <= 8 && (!publish || !edges.empty()),
                    "Behavior tree composites need 1-8 children to publish");
            for (const auto& child : edges) {
                const auto to = std::uint32_t(identifier(child));
                require(to && children.insert(to).second,
                        "Behavior tree nodes may have only one parent");
                node.children.push_back(to);
            }
        } else {
            require(!row.contains("children") ||
                        (row.at("children").is_array() && row.at("children").empty()),
                    "Behavior tree action/condition nodes cannot have children");
            if (node.kind == NavigationBehaviorTreeKind::Wait)
                node.value = bounded_number(row.at("seconds"), 0, 3600);
            if (node.kind == NavigationBehaviorTreeKind::TargetNear ||
                node.kind == NavigationBehaviorTreeKind::TargetFar)
                node.value = bounded_number(row.at("distance"), 0, 8192);
            if (node.kind == NavigationBehaviorTreeKind::MoveToPoint) {
                const auto& point = row.at("point");
                require(point.is_array() && point.size() == 3,
                        "Behavior tree point needs three coordinates");
                for (std::size_t i = 0; i < 3; ++i)
                    node.point[i] = bounded_number(point[i], -4096, 4096);
            }
        }
        graph.nodes.push_back(std::move(node));
    }
    require(rows.empty() ? graph.entry == 0 : ids.contains(graph.entry),
            "Behavior tree entry node is missing");
    require(!children.contains(graph.entry), "Behavior tree entry cannot have a parent");
    for (const auto id : children)
        require(ids.contains(id), "Behavior tree child is missing");
    std::set<std::uint32_t> active, visited;
    const std::function<void(std::uint32_t, unsigned)> walk = [&](std::uint32_t id,
                                                                  unsigned depth) {
        require(depth <= 8 && active.insert(id).second,
                "Behavior tree contains a cycle or exceeds depth eight");
        visited.insert(id);
        for (const auto child : graph.node(id).children)
            walk(child, depth + 1);
        active.erase(id);
    };
    if (graph.entry)
        walk(graph.entry, 1);
    if (publish)
        require(graph.entry && visited.size() == graph.nodes.size(),
                "Publish one connected behavior tree");
    else
        for (const auto& node : graph.nodes)
            if (!visited.contains(node.id))
                walk(node.id, 1);
    asset_detail::validate_state_graph_layout(source, navigation_behavior_layout_format, "nodes");
    return graph;
}
} // namespace
const char* navigation_behavior_tree_kind_name(NavigationBehaviorTreeKind kind) {
    switch (kind) {
    case NavigationBehaviorTreeKind::Sequence:
        return "sequence";
    case NavigationBehaviorTreeKind::ReactiveSequence:
        return "reactive_sequence";
    case NavigationBehaviorTreeKind::Selector:
        return "selector";
    case NavigationBehaviorTreeKind::Wait:
        return "wait";
    case NavigationBehaviorTreeKind::MoveToPoint:
        return "move_to_point";
    case NavigationBehaviorTreeKind::FollowTarget:
        return "follow_target";
    case NavigationBehaviorTreeKind::TargetNear:
        return "target_near";
    case NavigationBehaviorTreeKind::TargetFar:
        return "target_far";
    case NavigationBehaviorTreeKind::TargetMissing:
        return "target_missing";
    }
    throw std::runtime_error("Invalid behavior tree node kind");
}
const char* navigation_behavior_action_name(NavigationBehaviorAction kind) {
    switch (kind) {
    case NavigationBehaviorAction::Wait:
        return "wait";
    case NavigationBehaviorAction::MoveToPoint:
        return "move_to_point";
    case NavigationBehaviorAction::FollowTarget:
        return "follow_target";
    }
    throw std::runtime_error("Invalid navigation behavior action");
}
const char* navigation_behavior_condition_name(NavigationBehaviorCondition kind) {
    switch (kind) {
    case NavigationBehaviorCondition::Elapsed:
        return "elapsed";
    case NavigationBehaviorCondition::Arrived:
        return "arrived";
    case NavigationBehaviorCondition::TargetNear:
        return "target_near";
    case NavigationBehaviorCondition::TargetFar:
        return "target_far";
    case NavigationBehaviorCondition::TargetMissing:
        return "target_missing";
    }
    throw std::runtime_error("Invalid navigation behavior condition");
}
const NavigationBehaviorState& NavigationBehaviorGraph::state(std::uint32_t id) const {
    require(profile == NavigationBehaviorProfile::StateGraph,
            "Behavior tree nodes are not state-graph states");
    const auto found = std::find_if(states.begin(), states.end(),
                                    [id](const auto& value) { return value.id == id; });
    require(found != states.end(), "Behavior state is missing or deleted");
    return *found;
}
const NavigationBehaviorTreeNode& NavigationBehaviorGraph::node(std::uint32_t id) const {
    require(profile == NavigationBehaviorProfile::Tree, "Behavior graph is not a tree");
    const auto at =
        std::find_if(nodes.begin(), nodes.end(), [id](const auto& n) { return n.id == id; });
    require(at != nodes.end(), "Behavior tree node is missing or deleted");
    return *at;
}
std::size_t NavigationBehaviorGraph::resident_bytes() const {
    std::size_t bytes = sizeof(*this) + states.capacity() * sizeof(NavigationBehaviorState) +
                        cooked_digest.capacity();
    for (const auto& s : states)
        bytes +=
            s.name.capacity() + s.transitions.capacity() * sizeof(NavigationBehaviorTransition);
    bytes += nodes.capacity() * sizeof(NavigationBehaviorTreeNode);
    for (const auto& node : nodes)
        bytes += node.name.capacity() + node.children.capacity() * sizeof(std::uint32_t);
    return bytes;
}
NavigationBehaviorSource NavigationBehaviorSource::create(AssetId id) {
    require(bool(id), "Navigation behavior requires an asset identity");
    return {{{"format", "forge.navigation-behavior"},
             {"version", 1},
             {"asset_id", id},
             {"entry", 1},
             {"next_id", 2},
             {"states", Json::array({{{"id", 1},
                                      {"name", "Wait"},
                                      {"action", {{"kind", "wait"}}},
                                      {"transitions", Json::array()}}})}}};
}
NavigationBehaviorSource NavigationBehaviorSource::create_tree(AssetId id) {
    require(bool(id), "Behavior tree requires an asset identity");
    return {{{"format", "forge.navigation-behavior"},
             {"version", 2},
             {"asset_id", id},
             {"entry", 1},
             {"next_id", 2},
             {"nodes",
              Json::array({{{"id", 1}, {"name", "Wait"}, {"kind", "wait"}, {"seconds", 1}}})}}};
}
NavigationBehaviorSource NavigationBehaviorSource::parse(std::span<const std::byte> bytes) {
    NavigationBehaviorSource source{
        asset_detail::parse_bounded_json(bytes, navigation_behavior_byte_limit, 12000, 12)};
    source.validate();
    return source;
}
NavigationBehaviorSource NavigationBehaviorSource::parse_cooked(std::span<const std::byte> bytes) {
    const auto value =
        asset_detail::parse_bounded_json(bytes, navigation_behavior_cooked_byte_limit, 12008, 13);
    require(value.is_object() && value.at("format") == "forge.navigation-behavior" &&
                version_one(value.at("version")),
            "Unsupported cooked navigation behavior format/version");
    const auto text = value.at("document").dump();
    auto source = parse(std::as_bytes(std::span(text)));
    (void)source.compiled();
    return source;
}
AssetId NavigationBehaviorSource::asset() const { return document.at("asset_id").get<AssetId>(); }
void NavigationBehaviorSource::validate() const {
    const auto& d = document;
    require(d.is_object() && d.at("format") == "forge.navigation-behavior" &&
                d.at("version").is_number_integer() &&
                (d.at("version") == 1 || d.at("version") == 2),
            "Unsupported navigation behavior source format/version");
    const auto text = d.dump();
    const auto saved = asset_detail::parse_bounded_json(std::as_bytes(std::span(text)),
                                                        navigation_behavior_byte_limit, 12000, 12);
    require(saved == d, "Behavior contains values that cannot be saved as JSON");
    require(bool(asset()), "Navigation behavior asset identity is missing");
    const auto next = identifier(d.at("next_id"), std::uint64_t(UINT32_MAX) + 1);
    require(next != 0, "Behavior identity sequence cannot be zero");
    const auto entry = identifier(d.at("entry"));
    if (asset_detail::has_state_graph_layout(d, navigation_behavior_layout_format))
        require(version_one(d.at("editor_layout").at("version")),
                "Unsupported navigation behavior layout version");
    if (d.at("version") == 2) {
        (void)tree_graph(d, false);
        return;
    }
    const auto& rows = d.at("states");
    require(rows.is_array() && rows.size() <= 32, "Behavior supports at most 32 states");
    std::set<std::uint32_t> ids, states;
    std::set<std::string> names;
    const auto allocated = [&](const Json& value) {
        const auto id = identifier(value);
        require(id && id < next && ids.insert(std::uint32_t(id)).second,
                "Behavior state/transition identity is unallocated or duplicated");
        return std::uint32_t(id);
    };
    std::size_t transitions = 0;
    for (const auto& s : rows) {
        states.insert(allocated(s.at("id")));
        const auto name = s.at("name").get<std::string>();
        require(!name.empty() && name.size() <= 64 && names.insert(name).second &&
                    std::none_of(name.begin(), name.end(),
                                 [](unsigned char c) { return c < 32 || c == 127; }),
                "Behavior names must be unique and contain 1-64 printable bytes");
        const auto& a = s.at("action");
        if (action(a.at("kind")) == NavigationBehaviorAction::MoveToPoint) {
            const auto& point = a.at("point");
            require(point.is_array() && point.size() == 3,
                    "Behavior point requires three coordinates");
            for (const auto& coordinate : point)
                (void)bounded_number(coordinate, -4096, 4096);
        }
        const auto& edges = s.at("transitions");
        require(edges.is_array() && edges.size() <= 8 && (transitions += edges.size()) <= 128,
                "Behavior supports eight transitions per state and 128 total");
        for (const auto& edge : edges) {
            allocated(edge.at("id"));
            const auto& predicate = edge.at("condition");
            (void)condition_value(predicate, condition(predicate.at("kind")));
        }
    }
    require(rows.empty() ? entry == 0 : states.contains(std::uint32_t(entry)),
            "Behavior entry state is missing");
    for (const auto& s : rows)
        for (const auto& edge : s.at("transitions"))
            require(states.contains(std::uint32_t(identifier(edge.at("to")))),
                    "Behavior transition destination is missing");
    asset_detail::validate_state_graph_layout(d, navigation_behavior_layout_format);
}
NavigationBehaviorGraph NavigationBehaviorSource::compiled() const {
    validate();
    if (document.at("version") == 2)
        return tree_graph(document, true);
    require(!document.at("states").empty(), "Publish at least one behavior state");
    NavigationBehaviorGraph graph{asset(), std::uint32_t(identifier(document.at("entry"))), {}};
    for (const auto& row : document.at("states")) {
        NavigationBehaviorState s;
        s.id = std::uint32_t(identifier(row.at("id")));
        s.name = row.at("name");
        s.action = action(row.at("action").at("kind"));
        if (s.action == NavigationBehaviorAction::MoveToPoint)
            s.point = row.at("action").at("point").get<std::array<double, 3>>();
        for (const auto& edge : row.at("transitions")) {
            const auto& predicate = edge.at("condition");
            const auto kind = condition(predicate.at("kind"));
            s.transitions.push_back({std::uint32_t(identifier(edge.at("id"))),
                                     std::uint32_t(identifier(edge.at("to"))), kind,
                                     condition_value(predicate, kind)});
        }
        graph.states.push_back(std::move(s));
    }
    return graph;
}
std::vector<std::byte> NavigationBehaviorSource::cook() const {
    (void)compiled();
    auto runtime = document;
    if (asset_detail::has_state_graph_layout(runtime, navigation_behavior_layout_format))
        runtime.erase("editor_layout");
    const auto text = Json{{"format", "forge.navigation-behavior"},
                           {"version", 1},
                           {"document", std::move(runtime)}}
                          .dump();
    require(text.size() <= navigation_behavior_cooked_byte_limit,
            "Cooked navigation behavior exceeds its bound");
    const auto bytes = std::as_bytes(std::span(text));
    return {bytes.begin(), bytes.end()};
}
void to_json(nlohmann::json& value, const NavigationBehaviorCursor& cursor) {
    require(std::isfinite(cursor.elapsed) && cursor.elapsed >= 0 && cursor.elapsed <= 3600,
            "Behavior elapsed time is invalid");
    value = {{"state", cursor.state},
             {"last_transition", cursor.last_transition},
             {"elapsed", cursor.elapsed},
             {"transitions", cursor.transitions}};
    if (navigation_behavior_detail::has_tree_progress(cursor)) {
        require(unsigned(cursor.tree_status) <= unsigned(NavigationBehaviorTreeStatus::Failure) &&
                    std::ranges::all_of(cursor.tree_children, [](auto child) { return child < 8; }),
                "Behavior tree cursor fields are invalid");
        value["tree"] = {{"status", unsigned(cursor.tree_status)},
                         {"active", cursor.tree_active},
                         {"children", cursor.tree_children}};
    }
}
void from_json(const nlohmann::json& value, NavigationBehaviorCursor& cursor) {
    require(value.is_object() && value.size() == (value.contains("tree") ? 5u : 4u),
            "Invalid behavior cursor fields");
    NavigationBehaviorCursor next;
    next.state = std::uint32_t(identifier(value.at("state")));
    next.last_transition = std::uint32_t(identifier(value.at("last_transition")));
    next.elapsed = bounded_number(value.at("elapsed"), 0, 3600);
    next.transitions = identifier(value.at("transitions"), UINT64_MAX);
    if (value.contains("tree")) {
        const auto& tree = value.at("tree");
        require(tree.is_object() && tree.size() == 3 && tree.at("children").is_array() &&
                    tree.at("children").size() == next.tree_children.size(),
                "Invalid behavior tree cursor fields");
        next.tree_status = NavigationBehaviorTreeStatus(identifier(tree.at("status"), 3));
        next.tree_active = std::uint32_t(identifier(tree.at("active")));
        for (std::size_t i = 0; i < next.tree_children.size(); ++i)
            next.tree_children[i] = std::uint8_t(identifier(tree.at("children")[i], 7));
    }
    cursor = next;
}
void validate_navigation_behavior_cursor(const NavigationBehaviorGraph& graph,
                                         const NavigationBehaviorCursor& cursor) {
    require(std::isfinite(cursor.elapsed) && cursor.elapsed >= 0 && cursor.elapsed <= 3600,
            "Behavior elapsed time is invalid");
    if (graph.profile == NavigationBehaviorProfile::Tree) {
        navigation_behavior_detail::validate_tree_cursor(graph, cursor);
        return;
    }
    require(!navigation_behavior_detail::has_tree_progress(cursor),
            "State graph cursor contains behavior tree progress");
    if (!cursor.state) {
        require(!cursor.last_transition && !cursor.transitions && cursor.elapsed == 0,
                "Uninitialized behavior cursor contains progress");
        return;
    }
    (void)graph.state(cursor.state);
    if (!cursor.transitions) {
        require(!cursor.last_transition && cursor.state == graph.entry,
                "Initial behavior cursor does not select its entry");
        return;
    }
    bool found = false;
    for (const auto& state : graph.states)
        for (const auto& edge : state.transitions)
            found |= edge.id == cursor.last_transition && edge.destination == cursor.state;
    require(found, "Behavior cursor transition does not enter its current state");
}
const NavigationBehaviorState& advance_navigation_behavior(const NavigationBehaviorGraph& graph,
                                                           NavigationBehaviorCursor& cursor,
                                                           const NavigationBehaviorInputs& input,
                                                           double dt) {
    require(std::isfinite(dt) && dt > 0 && dt <= 1, "Behavior requires positive fixed delta <= 1");
    validate_navigation_behavior_cursor(graph, cursor);
    require(!input.target_distance ||
                (std::isfinite(*input.target_distance) && *input.target_distance >= 0),
            "Behavior target distance is invalid");
    auto next = cursor;
    if (!next.state) {
        next.state = graph.entry;
    }
    const auto& current = graph.state(next.state);
    next.elapsed = std::min(3600., next.elapsed + dt);
    for (const auto& edge : current.transitions) {
        bool matches = false;
        switch (edge.condition) {
        case NavigationBehaviorCondition::Elapsed:
            matches = next.elapsed >= edge.value;
            break;
        case NavigationBehaviorCondition::Arrived:
            matches = input.arrived;
            break;
        case NavigationBehaviorCondition::TargetNear:
            matches = input.target_distance && *input.target_distance <= edge.value;
            break;
        case NavigationBehaviorCondition::TargetFar:
            matches = input.target_distance && *input.target_distance >= edge.value;
            break;
        case NavigationBehaviorCondition::TargetMissing:
            matches = !input.target_distance;
            break;
        default:
            throw std::runtime_error("Invalid navigation behavior condition");
        }
        if (!matches)
            continue;
        (void)graph.state(edge.destination);
        require(next.transitions != std::numeric_limits<std::uint64_t>::max(),
                "Behavior transition counter exhausted");
        next.state = edge.destination;
        next.elapsed = 0;
        next.last_transition = edge.id;
        ++next.transitions;
        break;
    }
    const auto& result = graph.state(next.state);
    cursor = next;
    return result;
}
} // namespace forge
