#pragma once
#include <nlohmann/json.hpp>
namespace forge::asset_detail {
// Shared source/cooked admission. Omitted source projection/range fields become
// explicit null in cooked values; source nulls are not accepted as omissions.
nlohmann::json model_camera_value(const nlohmann::json& value, bool cooked = false);
nlohmann::json model_light_value(const nlohmann::json& value, bool cooked = false);
} // namespace forge::asset_detail
