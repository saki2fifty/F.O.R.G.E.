#pragma once
#include <forge/ui_values.hpp>
#include <nlohmann/json.hpp>

namespace forge::detail {
inline nlohmann::json ui_values_json(const UiValues& source) {
    auto values = nlohmann::json::array();
    for (const auto& item : source.values)
        values.push_back({{"name", item.name}, {"value", item.value}});
    return {{"values", std::move(values)}};
}
inline UiValues ui_values_from_json(const nlohmann::json& source) {
    const auto& values = source.at("values");
    if (!values.is_array() || values.size() > UiValues::max_entries)
        throw std::runtime_error("UI Values requires an array of at most 16 named numbers");
    UiValues result;
    result.values.reserve(values.size());
    for (const auto& item : values) {
        if (!item.at("value").is_number())
            throw std::runtime_error("UI Values requires numeric values");
        result.values.push_back(
            {item.at("name").get<std::string>(), item.at("value").get<double>()});
    }
    result.validate();
    return result;
}
} // namespace forge::detail
