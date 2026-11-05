#pragma once
#include <RmlUi/Core/DecorationTypes.h>
#include <RmlUi/Core/Variant.h>
namespace forge::test {
inline Rml::Dictionary linear_gradient_parameters() {
    Rml::ColorStopList stops{{{255, 0, 0, 255}, {0, Rml::Unit::NUMBER}},
                             {{0, 0, 128, 128}, {1, Rml::Unit::NUMBER}}};
    return {{"p0", Rml::Variant(Rml::Vector2f{4, 8})},
            {"p1", Rml::Variant(Rml::Vector2f{104, 8})},
            {"length", Rml::Variant(100.f)},
            {"repeating", Rml::Variant(false)},
            {"color_stop_list", Rml::Variant(std::move(stops))}};
}
} // namespace forge::test
