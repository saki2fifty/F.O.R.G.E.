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
    const NavigationBehaviorState& state(std::uint32_t) const;
    std::size_t resident_bytes() const;
};
struct NavigationBehaviorSource {
    nlohmann::json document;
    static NavigationBehaviorSource create(AssetId);
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
