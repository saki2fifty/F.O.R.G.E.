#pragma once
#include <cstdint>
#include <forge/asset_ref.hpp>

namespace forge {
struct NavigationBehaviorAsset {
    static constexpr const char* type = "navigation_behavior";
};
// Behavior owns destination intent while enabled; NavigationAgent owns movement.
struct NavigationBehavior {
    AssetRef<NavigationBehaviorAsset> graph;
    EntityRef target, anchor;
    bool enabled = true;
    bool operator==(const NavigationBehavior&) const = default;
};
// Runtime-only Flecs progress, not an authored Scene/Prefab component.
struct NavigationBehaviorCursor {
    std::uint32_t state = 0, last_transition = 0;
    double elapsed = 0;
    std::uint64_t transitions = 0;
    bool operator==(const NavigationBehaviorCursor&) const = default;
};
} // namespace forge
