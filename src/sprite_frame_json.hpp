#pragma once
#include "sprite_region.hpp"
#include <nlohmann/json.hpp>

namespace forge::detail {
// Shared value codec, with no file, asset, world or publication ownership.
inline nlohmann::json sprite_frame_document(const SpriteFrameRegion& frame) {
    (void)sprite_frame_canvas(frame);
    return {{"x", frame.x},
            {"y", frame.y},
            {"width", frame.width},
            {"height", frame.height},
            {"rotated_clockwise", frame.rotated_clockwise},
            {"source_width", frame.source_width},
            {"source_height", frame.source_height},
            {"trim_x", frame.trim_x},
            {"trim_y", frame.trim_y}};
}
inline SpriteFrameRegion sprite_frame_value(const nlohmann::json& row) {
    const auto pixel = [&](const char* field, std::uint32_t minimum) {
        const auto& value = row.at(field);
        if (!value.is_number_integer() || value.get<std::uint64_t>() < minimum ||
            value.get<std::uint64_t>() > 16384)
            throw std::invalid_argument("Sprite frame region pixels are outside their range");
        return value.get<std::uint32_t>();
    };
    const auto optional_pixel = [&](const char* field) {
        return row.contains(field) ? pixel(field, 0) : 0u;
    };
    SpriteFrameRegion frame{pixel("x", 0), pixel("y", 0), pixel("width", 1), pixel("height", 1)};
    if (row.contains("rotated_clockwise")) {
        if (!row.at("rotated_clockwise").is_boolean())
            throw std::invalid_argument("Sprite storage rotation must be a boolean");
        frame.rotated_clockwise = row.at("rotated_clockwise").get<bool>();
    }
    frame.source_width = optional_pixel("source_width");
    frame.source_height = optional_pixel("source_height");
    frame.trim_x = optional_pixel("trim_x");
    frame.trim_y = optional_pixel("trim_y");
    (void)sprite_frame_canvas(frame);
    return frame;
}
} // namespace forge::detail
