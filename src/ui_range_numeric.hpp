#pragma once
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>

namespace forge {
// A finite envelope keeps native float quantization intermediates representable.
// Normalization, clamping and events remain native widget responsibilities.
inline constexpr double ui_range_magnitude = 1000000;
inline constexpr float ui_range_minimum_step = 0.000001f;
inline bool ui_range_number(double value) {
    return std::isfinite(value) && std::abs(value) <= ui_range_magnitude;
}
inline float parse_ui_range_number(std::string_view text) {
    if (text.empty())
        throw std::runtime_error("Enter a finite slider number");
    float value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        !ui_range_number(value))
        throw std::runtime_error("Use a finite slider number within -1000000..1000000");
    return value;
}
inline std::string format_ui_range_number(double value) {
    if (!ui_range_number(value))
        throw std::runtime_error("Use a finite slider value within -1000000..1000000");
    char buffer[64];
    const auto encoded = std::to_chars(buffer, buffer + sizeof(buffer), static_cast<float>(value));
    if (encoded.ec != std::errc{})
        throw std::runtime_error("Native slider value could not be formatted");
    return {buffer, encoded.ptr};
}
} // namespace forge
