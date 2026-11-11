#include "ui_material_effect.hpp"
#include <cmath>
#include <stdexcept>

namespace forge::ui_effect_detail {
MaterialEffect admit_material_effect(const Rml::Dictionary& parameters) {
    const auto value = parameters.find("value"), dimensions = parameters.find("dimensions");
    if (parameters.size() != 2 || value == parameters.end() || dimensions == parameters.end() ||
        value->second.GetType() != Rml::Variant::STRING ||
        dimensions->second.GetType() != Rml::Variant::VECTOR2)
        throw std::runtime_error("UI shader requires native value and element dimensions");
    const auto& text = value->second.GetReference<Rml::String>();
    constexpr std::string_view prefix = "material:";
    if (!text.starts_with(prefix) || text.size() != prefix.size() + 36)
        throw std::runtime_error("UI shader value must be material:<Material asset UUID>");
    MaterialEffect result;
    result.material = {AssetId::parse(std::string_view(text).substr(prefix.size()))};
    const auto& size = dimensions->second.GetReference<Rml::Vector2f>();
    if (!result.material.id || !std::isfinite(size.x) || !std::isfinite(size.y) || size.x < 0 ||
        size.y < 0 || size.x > 65536 || size.y > 65536)
        throw std::runtime_error("UI shader requires a Material and dimensions within 0..65536");
    result.dimensions = {size.x, size.y};
    return result;
}
} // namespace forge::ui_effect_detail
