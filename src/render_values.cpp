#include "render_values.hpp"
#include <forge/render_view.hpp>
namespace forge::detail {
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
