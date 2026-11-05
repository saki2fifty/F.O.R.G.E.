#pragma once
#include "ui_filter_profile.hpp"
#include <RmlUi/Core/Variant.h>
#include <stdexcept>

namespace forge::ui_filter_detail {
// Native parser payload admission shared by drawing and non-drawing hosts.
inline Filter admit_filter(const Rml::String& name, const Rml::Dictionary& parameters) {
    if (name == "forge-luminance-mask") {
        if (!parameters.empty())
            throw std::runtime_error("UI luminance conversion takes no parameters");
        Filter result;
        result.kind = Kind::LuminanceToAlpha;
        return result;
    }
    if (name == "drop-shadow") {
        const auto sigma = parameters.find("sigma"), offset = parameters.find("offset"),
                   color = parameters.find("color");
        if (parameters.size() != 3 || sigma == parameters.end() || offset == parameters.end() ||
            color == parameters.end() || sigma->second.GetType() != Rml::Variant::FLOAT ||
            offset->second.GetType() != Rml::Variant::VECTOR2 ||
            color->second.GetType() != Rml::Variant::COLOURB)
            throw std::runtime_error("UI drop-shadow requires native float sigma, vector offset "
                                     "and straight byte color");
        const auto displacement = offset->second.Get<Rml::Vector2f>();
        const auto tint = color->second.Get<Rml::Colourb>();
        return make_drop_shadow(
            sigma->second.Get<float>(), displacement.x, displacement.y,
            {tint.red / 255.f, tint.green / 255.f, tint.blue / 255.f, tint.alpha / 255.f});
    }
    const auto key = name == "blur" ? "sigma" : "value";
    const auto at = parameters.find(key);
    if (parameters.size() != 1 || at == parameters.end() ||
        at->second.GetType() != Rml::Variant::FLOAT) {
        (void)make_filter(name, 0);
        throw std::runtime_error("UI filter requires one native float parameter");
    }
    return make_filter(name, at->second.Get<float>());
}
} // namespace forge::ui_filter_detail
