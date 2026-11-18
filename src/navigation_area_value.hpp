#pragma once
#include <cmath>
#include <forge/navigation_components.hpp>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace forge::navigation_detail {
inline std::uint32_t area_integer(const nlohmann::json& value, std::uint32_t maximum,
                                  const char* label) {
    const auto error = [&] {
        return std::runtime_error(std::string(label) + " must be an integer from 0 to " +
                                  std::to_string(maximum));
    };
    if (!value.is_number_integer())
        throw error();
    if (!value.is_number_unsigned() && value.get<std::int64_t>() < 0)
        throw error();
    const auto number = value.get<std::uint64_t>();
    if (number > maximum)
        throw error();
    return std::uint32_t(number);
}
inline void validate_navigation_area(const NavigationArea& value) {
    if (value.area >= navigation_area_limit)
        throw std::runtime_error("Navigation area must be from 0 to 15");
}
inline bool valid_area_cost(double value) {
    return std::isfinite(value) && value >= 1 && value <= navigation_area_max_cost;
}
inline void validate_navigation_area_filter(const NavigationAreaFilter& value) {
    if (value.mask > UINT16_MAX)
        throw std::runtime_error("Allowed navigation areas require a 16-bit mask");
    for (const auto cost : value.costs)
        if (!valid_area_cost(cost))
            throw std::runtime_error("Navigation area costs must be finite from 1 to 1000000");
}
inline nlohmann::json area_value(const NavigationArea& value) { return {{"area", value.area}}; }
inline NavigationArea area_from_value(const nlohmann::json& value) {
    if (!value.is_object())
        throw std::runtime_error("Navigation area must be an object");
    return {area_integer(value.value("area", nlohmann::json(0u)),
                         std::uint32_t(navigation_area_limit - 1), "Navigation area")};
}
inline nlohmann::json area_filter_value(const NavigationAreaFilter& value) {
    return {{"mask", value.mask}, {"costs", value.costs}};
}
inline NavigationAreaFilter area_filter_from_value(const nlohmann::json& value) {
    if (!value.is_object())
        throw std::runtime_error("Navigation area filter must be an object");
    NavigationAreaFilter result;
    result.mask = area_integer(value.value("mask", nlohmann::json(result.mask)), UINT16_MAX,
                               "Allowed navigation area mask");
    if (const auto costs = value.find("costs"); costs != value.end()) {
        if (!costs->is_array() || costs->size() != navigation_area_limit)
            throw std::runtime_error("Navigation area filter requires exactly 16 costs");
        for (std::size_t i = 0; i < navigation_area_limit; ++i) {
            const auto& item = costs->at(i);
            if (!item.is_number() || !valid_area_cost(item.get<double>()))
                throw std::runtime_error("Navigation area costs must be finite from 1 to 1000000");
            result.costs[i] = item.get<float>();
        }
    }
    validate_navigation_area_filter(result);
    return result;
}
} // namespace forge::navigation_detail
