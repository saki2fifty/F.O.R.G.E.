#pragma once
#include "ui_filter_profile.hpp"
#include <RmlUi/Core/Variant.h>
#include <stdexcept>

namespace forge::ui_filter_detail {
// Native parser payload admission shared by drawing and non-drawing hosts.
inline Filter admit_filter(const Rml::String& name, const Rml::Dictionary& parameters) {
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
