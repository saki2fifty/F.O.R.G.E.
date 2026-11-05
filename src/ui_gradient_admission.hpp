#pragma once
#include "ui_gradient_profile.hpp"
#include <RmlUi/Core/DecorationTypes.h>
#include <RmlUi/Core/Variant.h>
#include <stdexcept>

namespace forge::ui_gradient_detail {
inline Gradient admit_gradient(const Rml::String& name, const Rml::Dictionary& parameters) {
    if (name != "linear-gradient")
        throw std::runtime_error("Unsupported UI shader decorator; use a bounded linear gradient");
    const auto start = parameters.find("p0"), end = parameters.find("p1"),
               length = parameters.find("length"), repeat = parameters.find("repeating"),
               colors = parameters.find("color_stop_list");
    if (parameters.size() != 5 || start == parameters.end() || end == parameters.end() ||
        length == parameters.end() || repeat == parameters.end() || colors == parameters.end() ||
        start->second.GetType() != Rml::Variant::VECTOR2 ||
        end->second.GetType() != Rml::Variant::VECTOR2 ||
        length->second.GetType() != Rml::Variant::FLOAT ||
        repeat->second.GetType() != Rml::Variant::BOOL ||
        colors->second.GetType() != Rml::Variant::COLORSTOPLIST)
        throw std::runtime_error(
            "UI linear gradient requires native points, length, repeating flag and color stops");
    const auto& native = colors->second.GetReference<Rml::ColorStopList>();
    if (native.size() < 2 || native.size() > max_stops)
        throw std::runtime_error("UI linear gradients require 2..16 resolved color stops");
    std::array<Stop, max_stops> stops{};
    for (std::size_t i = 0; i < native.size(); ++i) {
        if (native[i].position.unit != Rml::Unit::NUMBER)
            throw std::runtime_error(
                "UI gradient color stop units must be resolved by the native parser");
        const auto color = native[i].color;
        stops[i] = {
            native[i].position.number,
            {color.red / 255.f, color.green / 255.f, color.blue / 255.f, color.alpha / 255.f}};
    }
    const auto p0 = start->second.Get<Rml::Vector2f>(), p1 = end->second.Get<Rml::Vector2f>();
    return make_linear_gradient({p0.x, p0.y}, {p1.x, p1.y}, length->second.Get<float>(),
                                repeat->second.Get<bool>(), {stops.data(), native.size()});
}
} // namespace forge::ui_gradient_detail
