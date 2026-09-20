#include "model_view_components.hpp"
#include "model_scene_values.hpp"
#include <cmath>
#include <forge/render_view.hpp>
#include <stdexcept>
namespace forge::asset_detail {
Camera model_camera_component(const nlohmann::json& source) {
    const auto value = model_camera_value(source, true);
    Camera result;
    result.basis = std::uint32_t(ViewBasis::GltfNegativeZ);
    if (value.at("type") == "perspective") {
        const auto& p = value.at("perspective");
        result.near_plane = p.at("znear");
        result.vertical_fov = p.at("yfov");
        result.infinite_far = p.at("zfar").is_null();
        if (!result.infinite_far)
            result.far_plane = p.at("zfar");
        if (!p.at("aspectRatio").is_null())
            result.aspect = p.at("aspectRatio");
    } else {
        const auto& p = value.at("orthographic");
        result.projection = std::uint32_t(CameraProjection::Orthographic);
        result.near_plane = p.at("znear");
        result.far_plane = p.at("zfar");
        const auto x = p.at("xmag").get<double>(), y = p.at("ymag").get<double>();
        result.orthographic_width = 2 * std::abs(x);
        result.orthographic_height = 2 * std::abs(y);
        result.flip_x = x < 0;
        result.flip_y = y < 0;
    }
    validate_camera(result);
    return result;
}
Light model_light_component(const nlohmann::json& source) {
    const auto value = model_light_value(source, true);
    Light result;
    result.basis = std::uint32_t(ViewBasis::GltfNegativeZ);
    result.kind = std::uint32_t(value.at("type") == "directional" ? LightKind::Directional
                                : value.at("type") == "point"     ? LightKind::Point
                                                                  : LightKind::Spot);
    const auto channel = [&](unsigned i) {
        const auto v = value.at("color").at(i).get<double>();
        const auto f = static_cast<float>(v);
        if (v != 0 && f == 0)
            throw std::runtime_error("Imported light color underflows authored float precision");
        return f;
    };
    result.color_r = channel(0);
    result.color_g = channel(1);
    result.color_b = channel(2);
    result.intensity = value.at("intensity");
    if (value.contains("range") && !value.at("range").is_null())
        result.range = value.at("range");
    if (result.kind == std::uint32_t(LightKind::Spot)) {
        result.inner_cone = value.at("spot").at("innerConeAngle");
        result.outer_cone = value.at("spot").at("outerConeAngle");
    }
    validate_light(result);
    return result;
}
} // namespace forge::asset_detail
