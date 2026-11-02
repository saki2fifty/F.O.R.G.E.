#include "navigation_behavior_tree.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>

namespace forge {
namespace {
using Kind = NavigationBehaviorTreeKind;
using Status = NavigationBehaviorTreeStatus;
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
bool composite(Kind kind) {
    return kind == Kind::Sequence || kind == Kind::ReactiveSequence || kind == Kind::Selector;
}
bool action(Kind kind) {
    return kind == Kind::Wait || kind == Kind::MoveToPoint || kind == Kind::FollowTarget;
}
std::size_t slot(const NavigationBehaviorGraph& graph, std::uint32_t id) {
    const auto& node = graph.node(id);
    return std::size_t(&node - graph.nodes.data());
}
NavigationBehaviorTreeSelection evaluate(const NavigationBehaviorGraph& graph,
                                         const NavigationBehaviorCursor& cursor,
                                         const NavigationBehaviorTreeInputs& input, double dt) {
    validate_navigation_behavior_cursor(graph, cursor);
    require(graph.profile == NavigationBehaviorProfile::Tree,
            "Tree evaluation requires a behavior tree");
    require(!input.target_distance ||
                (std::isfinite(*input.target_distance) && *input.target_distance >= 0),
            "Behavior tree target distance is invalid");
    auto previous = cursor;
    if (previous.tree_status != Status::Running) {
        previous.state = 0;
        previous.elapsed = 0;
        previous.tree_active = 0;
        previous.tree_children.fill(0);
    }
    NavigationBehaviorTreeSelection result;
    result.cursor.transitions = cursor.transitions;
    unsigned visits = 0;
    const std::function<Status(std::uint32_t, unsigned)> visit = [&](std::uint32_t id,
                                                                     unsigned depth) -> Status {
        require(++visits <= 32 && depth <= 8, "Behavior tree work or depth limit exceeded");
        const auto index = slot(graph, id);
        const auto& node = graph.nodes[index];
        const auto bit = std::uint32_t(1) << index;
        if (composite(node.kind)) {
            require(!node.children.empty() && node.children.size() <= 8,
                    "Behavior tree composite children are invalid");
            const auto first = node.kind == Kind::Sequence && (previous.tree_active & bit)
                                   ? previous.tree_children[index]
                                   : 0;
            require(std::size_t(first) < node.children.size(),
                    "Behavior tree continuation child is invalid");
            for (std::size_t i = first; i < node.children.size(); ++i) {
                const auto status = visit(node.children[i], depth + 1);
                if (status == Status::Running) {
                    result.cursor.tree_children[index] = std::uint8_t(i);
                    result.cursor.tree_active |= bit;
                    return status;
                }
                if ((node.kind != Kind::Selector && status == Status::Failure) ||
                    (node.kind == Kind::Selector && status == Status::Success))
                    return status;
            }
            return node.kind == Kind::Selector ? Status::Failure : Status::Success;
        }
        if (node.kind == Kind::TargetMissing)
            return !input.target_distance ? Status::Success : Status::Failure;
        if (node.kind == Kind::TargetNear || node.kind == Kind::TargetFar) {
            require(std::isfinite(node.value) && node.value >= 0 && node.value <= 8192,
                    "Behavior tree condition distance is invalid");
            const bool matches =
                input.target_distance &&
                (node.kind == Kind::TargetNear ? *input.target_distance <= node.value
                                               : *input.target_distance >= node.value);
            return matches ? Status::Success : Status::Failure;
        }
        require(action(node.kind), "Behavior tree action is invalid");
        if (node.kind == Kind::FollowTarget && !input.target_distance)
            return Status::Failure;
        auto elapsed = previous.state == id ? previous.elapsed : 0;
        if (dt > 0 && input.sampled_leaf == id) {
            if (input.action_failed)
                return Status::Failure;
            if (node.kind != Kind::Wait && input.arrived)
                return Status::Success;
            elapsed = std::min(3600., elapsed + dt);
        }
        if (node.kind == Kind::Wait) {
            require(std::isfinite(node.value) && node.value >= 0 && node.value <= 3600,
                    "Behavior tree wait duration is invalid");
            if (elapsed >= node.value)
                return Status::Success;
        }
        result.leaf = id;
        result.cursor.state = id;
        result.cursor.elapsed = elapsed;
        result.cursor.tree_active |= bit;
        return Status::Running;
    };
    result.cursor.tree_status = visit(graph.entry, 1);
    if (dt > 0) {
        require(result.cursor.transitions != std::numeric_limits<std::uint64_t>::max(),
                "Behavior tree step counter exhausted");
        ++result.cursor.transitions;
    }
    validate_navigation_behavior_cursor(graph, result.cursor);
    return result;
}
} // namespace
namespace navigation_behavior_detail {
bool has_tree_progress(const NavigationBehaviorCursor& cursor) {
    return cursor.tree_status != Status::NotStarted || cursor.tree_active ||
           std::ranges::any_of(cursor.tree_children, [](auto value) { return value != 0; });
}
void validate_tree_cursor(const NavigationBehaviorGraph& graph,
                          const NavigationBehaviorCursor& cursor) {
    require(graph.profile == NavigationBehaviorProfile::Tree && !graph.nodes.empty() &&
                graph.nodes.size() <= 32 && graph.entry,
            "Behavior tree graph is empty or invalid");
    require(unsigned(cursor.tree_status) <= unsigned(Status::Failure) && !cursor.last_transition,
            "Behavior tree status or transition is invalid");
    if (cursor.tree_status == Status::NotStarted) {
        require(!cursor.state && !cursor.transitions && cursor.elapsed == 0 &&
                    !has_tree_progress(cursor),
                "Uninitialized behavior tree contains progress");
        return;
    }
    if (cursor.tree_status != Status::Running) {
        require(
            !cursor.state && !cursor.tree_active && cursor.elapsed == 0 &&
                std::ranges::all_of(cursor.tree_children, [](auto value) { return value == 0; }),
            "Completed behavior tree retains active progress");
        return;
    }
    std::uint32_t path = 0, id = graph.entry;
    for (unsigned depth = 1;; ++depth) {
        require(depth <= 8, "Behavior tree cursor exceeds depth eight");
        const auto index = slot(graph, id);
        const auto& node = graph.nodes[index];
        const auto bit = std::uint32_t(1) << index;
        require(!(path & bit), "Behavior tree cursor contains a cycle");
        path |= bit;
        if (!composite(node.kind)) {
            require(action(node.kind) && cursor.state == id && !cursor.tree_children[index],
                    "Behavior tree cursor does not select its running action");
            break;
        }
        const auto child = cursor.tree_children[index];
        require(child < node.children.size(), "Behavior tree cursor child is missing");
        id = node.children[child];
    }
    require(path == cursor.tree_active, "Behavior tree cursor active path is inconsistent");
    for (std::size_t i = 0; i < cursor.tree_children.size(); ++i)
        require((path & (std::uint32_t(1) << i)) || !cursor.tree_children[i],
                "Inactive behavior tree branch retains continuation");
}
} // namespace navigation_behavior_detail
const char* navigation_behavior_tree_status_name(NavigationBehaviorTreeStatus status) {
    switch (status) {
    case Status::NotStarted:
        return "Not started";
    case Status::Running:
        return "Running";
    case Status::Success:
        return "Success";
    case Status::Failure:
        return "Failure";
    }
    throw std::runtime_error("Invalid behavior tree status");
}
NavigationBehaviorTreeSelection
select_navigation_behavior_tree(const NavigationBehaviorGraph& graph,
                                const NavigationBehaviorCursor& cursor,
                                const NavigationBehaviorTreeInputs& input) {
    return evaluate(graph, cursor, input, 0);
}
NavigationBehaviorTreeSelection
advance_navigation_behavior_tree(const NavigationBehaviorGraph& graph,
                                 const NavigationBehaviorTreeSelection& selected,
                                 const NavigationBehaviorTreeInputs& input, double dt) {
    require(std::isfinite(dt) && dt > 0 && dt <= 1,
            "Behavior tree requires positive fixed delta <= 1");
    require(selected.cursor.tree_status != Status::NotStarted &&
                selected.leaf == selected.cursor.state && input.sampled_leaf == selected.leaf,
            "Behavior tree observation does not match the selected action");
    return evaluate(graph, selected.cursor, input, dt);
}
} // namespace forge
