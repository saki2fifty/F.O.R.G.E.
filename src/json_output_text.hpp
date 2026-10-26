#pragma once
#include <nlohmann/json.hpp>
#include <string>
namespace forge::detail {
// Human-readable native output only. Do not relax structured/source JSON admission.
// Preserve the owning bytes; replacement applies only to this copied display value.
inline nlohmann::json json_output_text(const std::string& text) {
    using Json = nlohmann::json;
    return Json::parse(Json(text).dump(-1, ' ', false, Json::error_handler_t::replace));
}
} // namespace forge::detail
