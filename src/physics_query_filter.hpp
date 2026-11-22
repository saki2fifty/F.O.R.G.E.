#pragma once
#include <algorithm>
#include <cmath>
#include <forge/physics_service.hpp>
#include <stdexcept>

namespace forge::physics_detail {
struct PhysicsQueryLimits {
    double position, minimum_radius, maximum_radius, minimum_box_size, maximum_box_size;
};
inline constexpr PhysicsQueryLimits physics_query_limits_3d{1e9, .001, 10000, .001, 10000};
// Preserve the native float thresholds used by existing XY requests.
inline constexpr PhysicsQueryLimits physics_query_limits_2d{1e6, .005f, 500, .01f, 1000};
inline void validate_query_slice(const PhysicsQuerySlice& slice) {
    if (!std::isfinite(slice.from) || !std::isfinite(slice.to) || slice.from < 0 ||
        slice.from > slice.to || slice.to > 1 || !slice.query_budget ||
        slice.query_budget > physics_motion_query_limit)
        throw std::runtime_error("Physics motion query needs an ordered unit interval and budget");
}
inline void validate_query_filter(const PhysicsQueryFilter& filter) {
    if (filter.exclusions.size() > physics_query_exclusion_limit)
        throw std::runtime_error("Physics query exclusion count exceeds the supported limit");
    for (const auto& ref : filter.exclusions)
        if (!ref.scene || !ref.entity)
            throw std::runtime_error(
                "Physics query exclusion needs a complete canonical reference");
}
inline bool query_excludes(const PhysicsQueryFilter& filter, const EntityRef& ref) noexcept {
    return std::find(filter.exclusions.begin(), filter.exclusions.end(), ref) !=
           filter.exclusions.end();
}
} // namespace forge::physics_detail
