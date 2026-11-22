#pragma once
#include <algorithm>
#include <cmath>
#include <forge/physics_service.hpp>
#include <stdexcept>

namespace forge::physics_detail {
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
