#include "model_scene_values.hpp"
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace forge::asset_detail {
namespace {
using Json = nlohmann::json;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
double number(const Json& value) {
    require(value.is_number() && std::isfinite(value.get<double>()),
            "Nonfinite model camera/light value");
    return value.get<double>();
}
std::string name(const Json& value) {
    const auto result = value.value("name", std::string{});
    require(result.size() <= 4096 && result.find('\0') == std::string::npos,
            "Invalid model camera/light name");
    return result;
}
Json optional_number(const Json& object, const char* key, bool cooked) {
    if (!object.contains(key) || (cooked && object.at(key).is_null()))
        return nullptr;
    return number(object.at(key));
}
} // namespace
nlohmann::json model_camera_value(const nlohmann::json& value, bool cooked) {
    require(value.is_object(), "Model camera must be an object");
    const auto type = value.at("type").get<std::string>();
    require(type == "perspective" || type == "orthographic", "Unsupported model camera projection");
    const auto& p = value.at(type);
    require(p.is_object(), "Model camera projection must be an object");
    const auto near = number(p.at("znear"));
    Json output{{"name", name(value)}, {"type", type}};
    if (type == "perspective") {
        require(!value.contains("orthographic"), "Camera declares conflicting projections");
        const auto fov = number(p.at("yfov"));
        const auto aspect = optional_number(p, "aspectRatio", cooked);
        const auto far = optional_number(p, "zfar", cooked);
        require(near > 0 && fov > 0 && fov < std::numbers::pi &&
                    (aspect.is_null() || aspect.get<double>() > 0) &&
                    (far.is_null() || far.get<double>() > near),
                "Invalid perspective camera parameters");
        output[type] = {{"znear", near}, {"zfar", far}, {"aspectRatio", aspect}, {"yfov", fov}};
    } else {
        require(!value.contains("perspective"), "Camera declares conflicting projections");
        const auto far = number(p.at("zfar")), x = number(p.at("xmag")), y = number(p.at("ymag"));
        // glTF says SHOULD positive, MUST NOT zero: negative magnification is valid.
        require(near >= 0 && far > near && x != 0 && y != 0,
                "Invalid orthographic camera parameters");
        output[type] = {{"znear", near}, {"zfar", far}, {"xmag", x}, {"ymag", y}};
    }
    return output;
}
nlohmann::json model_light_value(const nlohmann::json& value, bool cooked) {
    require(value.is_object(), "Model light must be an object");
    const auto type = value.at("type").get<std::string>();
    require(type == "directional" || type == "point" || type == "spot",
            "Unsupported model punctual light type");
    const auto color = value.value("color", Json::array({1, 1, 1}));
    require(color.is_array() && color.size() == 3, "Light color requires three linear channels");
    for (const auto& channel : color)
        require(number(channel) >= 0 && number(channel) <= 1,
                "Light color channel outside glTF range");
    const auto intensity = number(value.value("intensity", Json(1)));
    require(intensity >= 0, "Negative light intensity");
    Json output{{"name", name(value)}, {"type", type}, {"color", color}, {"intensity", intensity}};
    if (type == "directional")
        require(!value.contains("range"), "Directional light cannot have a distance cutoff");
    else {
        const auto range = optional_number(value, "range", cooked);
        require(range.is_null() || range.get<double>() > 0,
                "Light range must be positive or omitted");
        output["range"] = range;
    }
    if (type == "spot" || value.contains("spot")) {
        require(value.contains("spot") && value.at("spot").is_object(),
                "Spot light requires spot parameters");
        const auto& spot = value.at("spot");
        const auto inner = number(spot.value("innerConeAngle", Json(0)));
        const auto outer = number(spot.value("outerConeAngle", Json(std::numbers::pi / 4)));
        require(inner >= 0 && inner < outer && outer <= std::numbers::pi / 2,
                "Invalid spot cone angles");
        if (type == "spot")
            output["spot"] = {{"innerConeAngle", inner}, {"outerConeAngle", outer}};
    }
    return output;
}
} // namespace forge::asset_detail
