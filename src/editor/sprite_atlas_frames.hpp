#pragma once
#include "../bounded_json.hpp"
#include "../render_values.hpp"
#include "../sprite_region.hpp"
#include <forge/identity.hpp>

namespace forge::ui {
// Detached clipboard admission only. The caller's existing property transaction
// owns validation, prefab intent, Undo and persistence.
inline nlohmann::json sprite_atlas_frame_values(std::string_view text, AssetId texture,
                                                unsigned width, unsigned height) {
    const auto value = asset_detail::parse_bounded_json(std::as_bytes(std::span(text)),
                                                        sprite_frame_value_byte_limit, 20000, 16);
    if (!value.is_object() || value.size() != 6 ||
        value.at("format") != "forge.sprite-atlas-layout" ||
        (value.at("version") != 1 && value.at("version") != 2) || !texture ||
        value.at("texture") != texture.str() || value.at("width") != width ||
        value.at("height") != height)
        throw std::invalid_argument(
            "Assign a Sprite material using the copied atlas Texture first");
    const auto regions = detail::sprite_frame_regions_value(value.at("frames"));
    if (regions.empty())
        throw std::invalid_argument("Atlas needs at least one frame rectangle");
    (void)sprite_frame_region(width, height, {}, 0, {}, regions);
    return {{"frame_regions", value.at("frames")},
            {"frame", 0},
            {"region_enabled", false},
            {"texture_frames", false}};
}
} // namespace forge::ui
