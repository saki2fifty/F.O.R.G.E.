#pragma once
#include <cmath>
#include <forge/navigation_components.hpp>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace forge::navigation_detail {
inline constexpr double link_collision_min_length = .001;
inline constexpr double link_collision_max_length = 100;
inline constexpr unsigned link_collision_cast_limit = 64;

inline void validate_link_collision(const NavigationLinkCollision& value) {
    for (const auto length : {value.radius, value.straight_height, value.clearance})
        if (!std::isfinite(length) || length < link_collision_min_length ||
            length > link_collision_max_length)
            throw std::invalid_argument(
                "Navigation link collision lengths must be within 0.001-100 metres");
    if (value.clearance > 1)
        throw std::invalid_argument("Navigation link collision clearance must not exceed 1 metre");
}

inline nlohmann::json link_collision_value(const NavigationLinkCollision& value) {
    validate_link_collision(value);
    return {{"enabled", value.enabled},
            {"radius", value.radius},
            {"straight_height", value.straight_height},
            {"clearance", value.clearance},
            {"mask", value.mask},
            {"include_sensors", value.include_sensors}};
}

inline NavigationLinkCollision link_collision_from_value(const nlohmann::json& source) {
    NavigationLinkCollision result;
    result.enabled = source.at("enabled").get<bool>();
    result.radius = source.at("radius").get<float>();
    result.straight_height = source.at("straight_height").get<float>();
    result.clearance = source.at("clearance").get<float>();
    result.include_sensors = source.at("include_sensors").get<bool>();
    const auto& mask = source.at("mask");
    std::uint64_t bits;
    if (mask.is_number_unsigned())
        bits = mask.get<std::uint64_t>();
    else if (mask.is_number_integer()) {
        const auto signed_bits = mask.get<std::int64_t>();
        if (signed_bits < 0)
            throw std::invalid_argument(
                "Navigation link collision mask must be an unsigned 32-bit integer");
        bits = static_cast<std::uint64_t>(signed_bits);
    } else
        throw std::invalid_argument(
            "Navigation link collision mask must be an unsigned 32-bit integer");
    if (bits > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("Navigation link collision mask exceeds 32 bits");
    result.mask = static_cast<std::uint32_t>(bits);
    validate_link_collision(result);
    return result;
}
} // namespace forge::navigation_detail
