#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace forge::ui_filter_detail {
inline constexpr unsigned profile_version = 2;
inline constexpr unsigned basic_profile_version = 1;
inline constexpr unsigned max_layers = 4, max_chain = 8, max_filters = 256;
inline constexpr unsigned max_images = 32;
inline constexpr std::uint64_t image_budget = 256ull * 1024 * 1024;
inline constexpr unsigned max_blur_radius = 48;
enum class Kind : std::uint32_t {
    Copy,
    Blur,
    Opacity,
    Brightness,
    Contrast,
    Grayscale,
    DropShadow
};
struct Filter {
    Kind kind = Kind::Copy;
    float value = 1;
    unsigned radius = 0;
    std::array<float, 2> offset{};
    // Straight display-space RGBA; the adapter converts to its destination domain.
    std::array<float, 4> color{};
    // Symmetric nonnegative kernel, packed in float4 lanes by the graphics adapter.
    std::array<float, 52> weights{};
};
// Admits a single native filter parameter. RML/RCSS parsing stays with RmlUi;
// this owner defines the supported resource and numeric bounds.
Filter make_filter(std::string_view name, double value);
Filter make_drop_shadow(double sigma, double x, double y, std::array<float, 4> color);
} // namespace forge::ui_filter_detail
