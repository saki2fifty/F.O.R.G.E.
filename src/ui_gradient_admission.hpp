#pragma once
#include "ui_gradient_profile.hpp"
#include <RmlUi/Core/DecorationTypes.h>
#include <RmlUi/Core/Variant.h>
#include <stdexcept>

namespace forge::ui_gradient_detail {
inline Gradient admit_gradient(const Rml::String& name, const Rml::Dictionary& parameters) {
    const bool linear = name == "linear-gradient", radial = name == "radial-gradient",
               conic = name == "conic-gradient";
    if (!linear && !radial && !conic)
        throw std::runtime_error("Unsupported UI shader decorator; use a bounded gradient");
    const auto field = [&](const char* key, Rml::Variant::Type type) -> const Rml::Variant& {
        const auto found = parameters.find(key);
        if (found == parameters.end() || found->second.GetType() != type)
            throw std::runtime_error(
                "UI gradient requires its exact native typed shape and color stops");
        return found->second;
    };
    if (parameters.size() != (linear ? 5u : 4u))
        throw std::runtime_error("UI gradient requires its exact native parameter set");
    const auto& repeat = field("repeating", Rml::Variant::BOOL);
    const auto& colors = field("color_stop_list", Rml::Variant::COLORSTOPLIST);
    const auto& native = colors.GetReference<Rml::ColorStopList>();
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
    const std::span<const Stop> copied{stops.data(), native.size()};
    if (linear) {
        const auto p0 = field("p0", Rml::Variant::VECTOR2).Get<Rml::Vector2f>();
        const auto p1 = field("p1", Rml::Variant::VECTOR2).Get<Rml::Vector2f>();
        return make_linear_gradient({p0.x, p0.y}, {p1.x, p1.y},
                                    field("length", Rml::Variant::FLOAT).Get<float>(),
                                    repeat.Get<bool>(), copied);
    }
    const auto center = field("center", Rml::Variant::VECTOR2).Get<Rml::Vector2f>();
    if (radial) {
        const auto radius = field("radius", Rml::Variant::VECTOR2).Get<Rml::Vector2f>();
        return make_radial_gradient({center.x, center.y}, {radius.x, radius.y}, repeat.Get<bool>(),
                                    copied);
    }
    return make_conic_gradient({center.x, center.y},
                               field("angle", Rml::Variant::FLOAT).Get<float>(), repeat.Get<bool>(),
                               copied);
}
} // namespace forge::ui_gradient_detail
