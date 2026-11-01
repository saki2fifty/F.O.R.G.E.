#include "sprite_atlas_settings.hpp"
#include "view_image_limits.h"
#include <stdexcept>
namespace forge::asset_detail {
nlohmann::json sprite_atlas_settings(const SpriteAtlasPacking& value) {
    value.validate();
    if (value.max_size > FORGE_VIEW_IMAGE_MAX_EXTENT ||
        value.byte_limit > FORGE_VIEW_IMAGE_PIXEL_LIMIT)
        throw std::invalid_argument(
            "Atlas PNG source supports at most 4096 pixels per side and 64 MiB of pixels");
    return {{"max_size", value.max_size},
            {"padding", value.padding},
            {"byte_limit", value.byte_limit},
            {"extrude", value.extrude},
            {"trim_transparent", value.trim_transparent},
            {"allow_rotation", value.allow_rotation}};
}
SpriteAtlasPacking sprite_atlas_settings(const nlohmann::json& value) {
    if (!value.is_object() || value.size() < 4 || value.size() > 6 || !value.contains("max_size") ||
        !value.contains("padding") || !value.contains("byte_limit") || !value.contains("extrude") ||
        !value.at("extrude").is_boolean())
        throw std::invalid_argument("Invalid atlas settings record");
    for (const auto& [key, field] : value.items()) {
        if (key == "max_size" || key == "padding" || key == "byte_limit" || key == "extrude")
            continue;
        if ((key != "trim_transparent" && key != "allow_rotation") || !field.is_boolean())
            throw std::invalid_argument("Unknown or invalid atlas transform setting");
    }
    const auto integer = [&](const char* name, std::uint64_t maximum) {
        const auto& field = value.at(name);
        if (!field.is_number_integer() || field.get<std::uint64_t>() > maximum)
            throw std::invalid_argument("Invalid bounded whole-number atlas setting");
        return field.get<std::uint64_t>();
    };
    SpriteAtlasPacking result{unsigned(integer("max_size", 16384)),
                              unsigned(integer("padding", 64)),
                              std::size_t(integer("byte_limit", 128ull * 1024 * 1024)),
                              value.at("extrude").get<bool>(),
                              value.value("trim_transparent", false),
                              value.value("allow_rotation", false)};
    (void)sprite_atlas_settings(result);
    return result;
}
} // namespace forge::asset_detail
