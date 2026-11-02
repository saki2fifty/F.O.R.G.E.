#pragma once
#include <forge/navigation_behavior.hpp>
namespace forge::navigation_behavior_detail {
bool has_tree_progress(const NavigationBehaviorCursor&);
void validate_tree_cursor(const NavigationBehaviorGraph&, const NavigationBehaviorCursor&);
} // namespace forge::navigation_behavior_detail
