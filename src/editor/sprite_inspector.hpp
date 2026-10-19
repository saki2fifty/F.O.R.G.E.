#pragma once
#include "editor_state.hpp"
#include "sprite_frame_context.hpp"
#include <algorithm>
#include <array>
#include <nlohmann/json.hpp>
#include <string_view>

namespace forge {
inline bool sprite_frame_field(const ui::SpriteFrameField& chooser, std::string_view owner,
                               const nlohmann::json& renderer, nlohmann::json& value) {
    if (!chooser)
        return false;
    try {
        if (!value.is_number_integer() || value.get<std::uint64_t>() > UINT32_MAX)
            throw std::invalid_argument("Sprite frame must be an unsigned whole number");
        auto frame = value.get<std::uint32_t>();
        if (chooser(ui::sprite_frame_context(owner, renderer), frame)) {
            value = frame;
            return true;
        }
    } catch (const std::exception& error) {
        ui::field_error(error.what());
    }
    return false;
}
// A view of reflected fields. Schema/value admission and property history stay
// with the existing component owner; this does not retain component values.
inline nlohmann::json sprite_inspector_fields(const nlohmann::json& fields,
                                              const nlohmann::json& values) {
    static constexpr std::array<std::string_view, 22> order{
        "material",  "columns", "rows",    "margin_x", "margin_y", "spacing_x",
        "spacing_y", "frame",   "width",   "height",   "pivot_x",  "pivot_y",
        "flip_x",    "flip_y",  "tint_r",  "tint_g",   "tint_b",   "tint_a",
        "order",     "enabled", "visible", "layers"};
    auto result = fields;
    std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        const auto rank = [](const auto& field) {
            return std::find(order.begin(), order.end(),
                             field.at("id").template get<std::string>());
        };
        return rank(a) < rank(b);
    });
    for (auto& field : result)
        if (field.at("id") == "frame")
            field["maximum"] =
                values.at("columns").get<std::uint64_t>() * values.at("rows").get<std::uint64_t>() -
                1;
    return result;
}
inline const char* sprite_inspector_group(std::string_view field) {
    if (field == "material")
        return "Image material";
    if (field == "columns" || field == "rows" || field == "frame" || field.starts_with("margin_") ||
        field.starts_with("spacing_"))
        return "Sheet frames";
    if (field == "width" || field == "height" || field.starts_with("pivot_") ||
        field.starts_with("flip_"))
        return "Size and pivot";
    if (field.starts_with("tint_") || field == "order")
        return "Appearance";
    return "Visibility";
}
} // namespace forge
