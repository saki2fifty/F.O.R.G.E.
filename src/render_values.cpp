#include "render_values.hpp"
#include <cmath>
#include <forge/render_view.hpp>
#include <stdexcept>
namespace forge::detail {
Json render_value(const TileMapRenderer& value) {
    for (const auto tint : {value.tint_r, value.tint_g, value.tint_b, value.tint_a})
        if (!std::isfinite(tint) || tint < 0 || tint > 1)
            throw std::invalid_argument("Tile Map tint must be finite linear RGBA in [0,1]");
    return {{"map", value.map.id ? Json(value.map.id) : Json()},
            {"enabled", value.enabled},
            {"visible", value.visible},
            {"layers", value.layers},
            {"order", value.order},
            {"tint_r", value.tint_r},
            {"tint_g", value.tint_g},
            {"tint_b", value.tint_b},
            {"tint_a", value.tint_a}};
}
TileMapRenderer tile_map_renderer_value(const Json& source) {
    TileMapRenderer value;
    if (!source.at("map").is_null())
        value.map.id = source.at("map").get<AssetId>();
    value.enabled = source.at("enabled");
    value.visible = source.at("visible");
    value.layers = source.at("layers").get<std::uint32_t>();
    value.order = source.at("order").get<std::int32_t>();
    value.tint_r = source.at("tint_r").get<float>();
    value.tint_g = source.at("tint_g").get<float>();
    value.tint_b = source.at("tint_b").get<float>();
    value.tint_a = source.at("tint_a").get<float>();
    (void)render_value(value);
    return value;
}
void validate_sprite_renderer(const SpriteRenderer& value) {
    for (const auto size : {value.width, value.height})
        if (!std::isfinite(size) || size < .0001 || size > 10000)
            throw std::invalid_argument(
                "Sprite width/height must be finite meters in [0.0001,10000]");
    for (const auto pivot : {value.pivot_x, value.pivot_y})
        if (!std::isfinite(pivot) || pivot < 0 || pivot > 1)
            throw std::invalid_argument("Sprite pivot must be finite normalized values in [0,1]");
    for (const auto tint : {value.tint_r, value.tint_g, value.tint_b, value.tint_a})
        if (!std::isfinite(tint) || tint < 0 || tint > 1)
            throw std::invalid_argument("Sprite tint must be finite linear RGBA in [0,1]");
    if (!value.columns || !value.rows || value.columns > 16384 || value.rows > 16384 ||
        value.margin_x > 16384 || value.margin_y > 16384 || value.spacing_x > 16384 ||
        value.spacing_y > 16384 || value.frame >= std::uint64_t(value.columns) * value.rows)
        throw std::invalid_argument("Sprite grid/frame is outside the supported integer range");
    if (value.image_mode > std::uint32_t(SpriteImageMode::Tiled))
        throw std::invalid_argument("Sprite image mode is unsupported");
    if (!std::isfinite(value.pixels_per_meter) || value.pixels_per_meter < .0001 ||
        value.pixels_per_meter > 1000000)
        throw std::invalid_argument("Sprite pixels per meter must be finite in [0.0001,1000000]");
    if (value.image_mode == std::uint32_t(SpriteImageMode::Tiled) &&
        (value.width * value.pixels_per_meter > 1048576 ||
         value.height * value.pixels_per_meter > 1048576))
        throw std::invalid_argument("Sprite repeated fill exceeds 1048576 target pixels");
    for (const auto border :
         {value.border_left, value.border_right, value.border_top, value.border_bottom})
        if (border > 16384)
            throw std::invalid_argument("Sprite borders must be whole pixels in [0,16384]");
}
Json render_value(const SpriteRenderer& p) {
    validate_sprite_renderer(p);
    return {{"material", p.material.id ? Json(p.material.id) : Json()},
            {"enabled", p.enabled},
            {"visible", p.visible},
            {"flip_x", p.flip_x},
            {"flip_y", p.flip_y},
            {"layers", p.layers},
            {"columns", p.columns},
            {"rows", p.rows},
            {"margin_x", p.margin_x},
            {"margin_y", p.margin_y},
            {"spacing_x", p.spacing_x},
            {"spacing_y", p.spacing_y},
            {"frame", p.frame},
            {"order", p.order},
            {"width", p.width},
            {"height", p.height},
            {"pivot_x", p.pivot_x},
            {"pivot_y", p.pivot_y},
            {"tint_r", p.tint_r},
            {"tint_g", p.tint_g},
            {"tint_b", p.tint_b},
            {"tint_a", p.tint_a},
            {"image_mode", p.image_mode},
            {"pixels_per_meter", p.pixels_per_meter},
            {"border_left", p.border_left},
            {"border_right", p.border_right},
            {"border_top", p.border_top},
            {"border_bottom", p.border_bottom}};
}
SpriteRenderer sprite_renderer_value(const Json& p) {
    SpriteRenderer value;
    if (!p.at("material").is_null())
        value.material.id = p.at("material").get<AssetId>();
    value.enabled = p.at("enabled");
    value.visible = p.at("visible");
    value.flip_x = p.at("flip_x");
    value.flip_y = p.at("flip_y");
    value.layers = p.at("layers").get<std::uint32_t>();
    value.columns = p.at("columns").get<std::uint32_t>();
    value.rows = p.at("rows").get<std::uint32_t>();
    value.margin_x = p.at("margin_x").get<std::uint32_t>();
    value.margin_y = p.at("margin_y").get<std::uint32_t>();
    value.spacing_x = p.at("spacing_x").get<std::uint32_t>();
    value.spacing_y = p.at("spacing_y").get<std::uint32_t>();
    value.frame = p.at("frame").get<std::uint32_t>();
    value.order = p.at("order").get<std::int32_t>();
    value.width = p.at("width").get<double>();
    value.height = p.at("height").get<double>();
    value.pivot_x = p.at("pivot_x").get<double>();
    value.pivot_y = p.at("pivot_y").get<double>();
    value.tint_r = p.at("tint_r").get<float>();
    value.tint_g = p.at("tint_g").get<float>();
    value.tint_b = p.at("tint_b").get<float>();
    value.tint_a = p.at("tint_a").get<float>();
    value.image_mode = p.value("image_mode", value.image_mode);
    value.pixels_per_meter = p.value("pixels_per_meter", value.pixels_per_meter);
    value.border_left = p.value("border_left", value.border_left);
    value.border_right = p.value("border_right", value.border_right);
    value.border_top = p.value("border_top", value.border_top);
    value.border_bottom = p.value("border_bottom", value.border_bottom);
    validate_sprite_renderer(value);
    return value;
}
Json render_value(const ParticleEmitter& p) {
    return {{"effect", p.effect.id ? Json(p.effect.id) : Json()},
            {"enabled", p.enabled},
            {"visible", p.visible},
            {"seed", p.seed},
            {"layers", p.layers},
            {"playback_speed", p.playback_speed},
            {"size_multiplier", p.size_multiplier},
            {"tint_r", p.tint_r},
            {"tint_g", p.tint_g},
            {"tint_b", p.tint_b}};
}
ParticleEmitter particle_emitter_value(const Json& p) {
    ParticleEmitter value;
    if (!p.at("effect").is_null())
        value.effect.id = p.at("effect").get<AssetId>();
    value.enabled = p.at("enabled");
    value.visible = p.at("visible");
    value.seed = p.at("seed").get<std::uint32_t>();
    value.layers = p.at("layers").get<std::uint32_t>();
    value.playback_speed = p.at("playback_speed").get<double>();
    value.size_multiplier = p.value("size_multiplier", 1.);
    value.tint_r = p.value("tint_r", 1.f);
    value.tint_g = p.value("tint_g", 1.f);
    value.tint_b = p.value("tint_b", 1.f);
    return value;
}
Json render_value(const Camera& p) {
    return {{"enabled", p.enabled},
            {"projection", p.projection},
            {"basis", p.basis},
            {"vertical_fov", p.vertical_fov},
            {"orthographic_height", p.orthographic_height},
            {"orthographic_width", p.orthographic_width},
            {"near_plane", p.near_plane},
            {"far_plane", p.far_plane},
            {"infinite_far", p.infinite_far},
            {"aspect", p.aspect},
            {"flip_x", p.flip_x},
            {"flip_y", p.flip_y},
            {"viewport_x", p.viewport_x},
            {"viewport_y", p.viewport_y},
            {"viewport_width", p.viewport_width},
            {"viewport_height", p.viewport_height},
            {"order", p.order},
            {"layers", p.layers},
            {"clear_color", p.clear_color},
            {"clear_depth", p.clear_depth},
            {"background_r", p.background_r},
            {"background_g", p.background_g},
            {"background_b", p.background_b},
            {"background_a", p.background_a}};
}
Json render_value(const Light& p) {
    return {{"enabled", p.enabled},
            {"kind", p.kind},
            {"basis", p.basis},
            {"color_r", p.color_r},
            {"color_g", p.color_g},
            {"color_b", p.color_b},
            {"intensity", p.intensity},
            {"range", p.range},
            {"inner_cone", p.inner_cone},
            {"outer_cone", p.outer_cone},
            {"cast_shadows", p.cast_shadows},
            {"shadow_bias", p.shadow_bias},
            {"shadow_normal_bias", p.shadow_normal_bias},
            {"layers", p.layers}};
}
Camera camera_value(const Json& p) {
    Camera value;
    value.enabled = p.at("enabled").get<bool>();
    value.projection = p.at("projection").get<std::uint32_t>();
    value.basis = p.at("basis").get<std::uint32_t>();
    value.vertical_fov = p.at("vertical_fov").get<double>();
    value.orthographic_height = p.at("orthographic_height").get<double>();
    value.orthographic_width = p.at("orthographic_width").get<double>();
    value.near_plane = p.at("near_plane").get<double>();
    value.far_plane = p.at("far_plane").get<double>();
    value.infinite_far = p.at("infinite_far").get<bool>();
    value.aspect = p.at("aspect").get<double>();
    value.flip_x = p.at("flip_x").get<bool>();
    value.flip_y = p.at("flip_y").get<bool>();
    value.viewport_x = p.at("viewport_x").get<double>();
    value.viewport_y = p.at("viewport_y").get<double>();
    value.viewport_width = p.at("viewport_width").get<double>();
    value.viewport_height = p.at("viewport_height").get<double>();
    value.order = p.at("order").get<std::int32_t>();
    value.layers = p.at("layers").get<std::uint32_t>();
    value.clear_color = p.at("clear_color").get<bool>();
    value.clear_depth = p.at("clear_depth").get<bool>();
    value.background_r = p.at("background_r").get<float>();
    value.background_g = p.at("background_g").get<float>();
    value.background_b = p.at("background_b").get<float>();
    value.background_a = p.at("background_a").get<float>();
    validate_camera(value);
    return value;
}
Light light_value(const Json& p) {
    Light value;
    value.enabled = p.at("enabled").get<bool>();
    value.kind = p.at("kind").get<std::uint32_t>();
    value.basis = p.at("basis").get<std::uint32_t>();
    value.color_r = p.at("color_r").get<float>();
    value.color_g = p.at("color_g").get<float>();
    value.color_b = p.at("color_b").get<float>();
    value.intensity = p.at("intensity").get<double>();
    value.range = p.at("range").get<double>();
    value.inner_cone = p.at("inner_cone").get<double>();
    value.outer_cone = p.at("outer_cone").get<double>();
    value.cast_shadows = p.at("cast_shadows").get<bool>();
    value.shadow_bias = p.at("shadow_bias").get<float>();
    value.shadow_normal_bias = p.at("shadow_normal_bias").get<float>();
    value.layers = p.at("layers").get<std::uint32_t>();
    validate_light(value);
    return value;
}
} // namespace forge::detail
