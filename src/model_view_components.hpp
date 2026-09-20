#pragma once
#include <forge/render_components.hpp>
#include <nlohmann/json.hpp>
namespace forge::asset_detail {
Camera model_camera_component(const nlohmann::json& canonical);
Light model_light_component(const nlohmann::json& canonical);
} // namespace forge::asset_detail
