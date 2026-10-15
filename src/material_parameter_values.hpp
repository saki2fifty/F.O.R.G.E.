#pragma once
#include <cmath>
#include <forge/material_parameter.hpp>
#include <string_view>
namespace forge::detail {
inline void validate_material_key(std::string_view value) {
    if (value.empty() || value.size() > 256)
        throw std::runtime_error("Invalid material key length");
    for (const unsigned char c : value)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '.' || c == '-'))
            throw std::runtime_error("Invalid material key character");
}
inline void validate_material_parameter(const MaterialParameter& value) {
    const auto width = material_parameter_width(value.type);
    for (unsigned lane = 0; lane < 4; ++lane)
        if (!std::isfinite(value.value[lane]) || (lane >= width && value.value[lane] != 0))
            throw std::runtime_error("Invalid material parameter value/unused lane");
}
} // namespace forge::detail
