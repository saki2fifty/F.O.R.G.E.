#pragma once
#include "../render_values.hpp"
#include "../sprite_region.hpp"
#include <forge/render_components.hpp>
#include <functional>
#include <nlohmann/json.hpp>
#include <string_view>

namespace forge::ui {
// Detached field context. Frame is deliberately separate: Timeline owns pending
// keys, and live animation may update it freely.
struct SpriteFrameContext {
    std::string_view owner;
    AssetRef<MaterialAsset> material;
    SpriteGridLayout grid;
    std::optional<SpritePixelRegion> crop;
    std::vector<SpriteFrameRegion> frame_regions;
    bool texture_frames = false;
};
inline SpriteFrameContext sprite_frame_context(std::string_view owner,
                                               const SpriteRenderer& sprite) {
    return {owner,
            sprite.material,
            {sprite.columns, sprite.rows, sprite.margin_x, sprite.margin_y, sprite.spacing_x,
             sprite.spacing_y},
            sprite.region_enabled
                ? std::optional<SpritePixelRegion>{{sprite.region_x, sprite.region_y,
                                                    sprite.region_width, sprite.region_height}}
                : std::nullopt,
            sprite.frame_regions,
            sprite.texture_frames};
}
inline SpriteFrameContext sprite_frame_context(std::string_view owner,
                                               const nlohmann::json& sprite) {
    const auto integer = [&](const char* field) {
        const auto& value = sprite.at(field);
        if (!value.is_number_integer() || value.get<std::uint64_t>() > UINT32_MAX)
            throw std::invalid_argument("Sprite grid values must be unsigned whole numbers");
        return value.get<std::uint32_t>();
    };
    return {
        owner,
        sprite.at("material").is_null() ? AssetRef<MaterialAsset>{}
                                        : sprite.at("material").get<AssetRef<MaterialAsset>>(),
        {integer("columns"), integer("rows"), integer("margin_x"), integer("margin_y"),
         integer("spacing_x"), integer("spacing_y")},
        sprite.value("region_enabled", false)
            ? std::optional<SpritePixelRegion>{{integer("region_x"), integer("region_y"),
                                                integer("region_width"), integer("region_height")}}
            : std::nullopt,
        sprite.contains("frame_regions")
            ? forge::detail::sprite_frame_regions_value(sprite.at("frame_regions"))
            : std::vector<SpriteFrameRegion>{},
        sprite.value("texture_frames", false)};
}
using SpriteFrameField = std::function<bool(const SpriteFrameContext&, std::uint32_t&)>;
} // namespace forge::ui
