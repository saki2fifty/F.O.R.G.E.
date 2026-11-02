#pragma once
#include <array>
#include <cstdint>
#include <forge/navigation_behavior_components.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace forge {
inline constexpr std::size_t navigation_behavior_byte_limit = 64 * 1024;
inline constexpr std::size_t navigation_behavior_cooked_byte_limit =
    navigation_behavior_byte_limit + 128;
inline constexpr const char* navigation_behavior_layout_format = "forge.navigation-behavior-layout";
enum class NavigationBehaviorAction { Wait, MoveToPoint, FollowTarget };
enum class NavigationBehaviorCondition { Elapsed, Arrived, TargetNear, TargetFar, TargetMissing };
enum class NavigationBehaviorProfile { StateGraph, Tree };
enum class NavigationBehaviorTreeKind {
    Sequence,
    ReactiveSequence,
    Selector,
    Wait,
    MoveToPoint,
    FollowTarget,
    TargetNear,
    TargetFar,
    TargetMissing
};
const char* navigation_behavior_tree_kind_name(NavigationBehaviorTreeKind);
const char* navigation_behavior_tree_status_name(NavigationBehaviorTreeStatus);
struct NavigationBehaviorTreeNode {
    std::uint32_t id = 0;
    std::string name;
    NavigationBehaviorTreeKind kind = NavigationBehaviorTreeKind::Wait;
    double value = 0;
    std::array<double, 3> point{};
    std::vector<std::uint32_t> children;
};
const char* navigation_behavior_action_name(NavigationBehaviorAction);
const char* navigation_behavior_condition_name(NavigationBehaviorCondition);
struct NavigationBehaviorTransition {
    std::uint32_t id = 0, destination = 0;
    NavigationBehaviorCondition condition = NavigationBehaviorCondition::Elapsed;
    double value = 0;
};
struct NavigationBehaviorState {
    std::uint32_t id = 0;
    std::string name;
    NavigationBehaviorAction action = NavigationBehaviorAction::Wait;
    std::array<double, 3> point{};
    std::vector<NavigationBehaviorTransition> transitions;
};
struct NavigationBehaviorGraph {
    AssetId asset;
    std::uint32_t entry = 0;
    std::vector<NavigationBehaviorState> states;
    std::string cooked_digest{}; // Verified immutable bytes; supplied by resource admission.
    NavigationBehaviorProfile profile = NavigationBehaviorProfile::StateGraph;
    std::vector<NavigationBehaviorTreeNode> nodes{};
    const NavigationBehaviorState& state(std::uint32_t) const;
    const NavigationBehaviorTreeNode& node(std::uint32_t) const;
    std::size_t resident_bytes() const;
};
struct NavigationBehaviorSource {
    nlohmann::json document;
    static NavigationBehaviorSource create(AssetId);
    static NavigationBehaviorSource create_tree(AssetId);
    static NavigationBehaviorSource parse(std::span<const std::byte>);
    static NavigationBehaviorSource parse_cooked(std::span<const std::byte>);
    AssetId asset() const;
    void validate() const;
    NavigationBehaviorGraph compiled() const;
    std::vector<std::byte> cook() const;
};
struct NavigationBehaviorInputs {
    std::optional<double> target_distance;
    bool arrived = false; // Caller verifies this state's actual destination.
};
struct NavigationBehaviorTreeInputs {
    std::optional<double> target_distance;
    std::uint32_t sampled_leaf = 0;
    bool arrived = false, action_failed = false;
};
struct NavigationBehaviorTreeSelection {
    NavigationBehaviorCursor cursor;
    std::uint32_t leaf = 0;
};
// Selection stages intent/continuation without advancing elapsed time or step count.
NavigationBehaviorTreeSelection
select_navigation_behavior_tree(const NavigationBehaviorGraph&, const NavigationBehaviorCursor&,
                                const NavigationBehaviorTreeInputs&);
// Consume only sampled_leaf's observation/time; failure preserves the input cursor.
NavigationBehaviorTreeSelection
advance_navigation_behavior_tree(const NavigationBehaviorGraph&,
                                 const NavigationBehaviorTreeSelection&,
                                 const NavigationBehaviorTreeInputs&, double fixed_dt);
void validate_navigation_behavior_cursor(const NavigationBehaviorGraph&,
                                         const NavigationBehaviorCursor&);
void to_json(nlohmann::json&, const NavigationBehaviorCursor&);
void from_json(const nlohmann::json&, NavigationBehaviorCursor&);
// First matching outgoing edge wins, at most once per positive fixed tick.
// Failure leaves the cursor unchanged. Returned action belongs to the graph.
const NavigationBehaviorState& advance_navigation_behavior(const NavigationBehaviorGraph&,
                                                           NavigationBehaviorCursor&,
                                                           const NavigationBehaviorInputs&,
                                                           double fixed_dt);
} // namespace forge
