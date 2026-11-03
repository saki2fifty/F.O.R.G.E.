#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace forge::ui_filter_detail {
inline constexpr unsigned profile_version = 1;
inline constexpr unsigned max_layers = 4, max_chain = 8, max_filters = 256;
inline constexpr unsigned max_images = 32;
inline constexpr std::uint64_t image_budget = 256ull * 1024 * 1024;
inline constexpr unsigned max_blur_radius = 48;
enum class Kind : std::uint32_t { Copy, Blur, Opacity, Brightness, Contrast, Grayscale };
struct Filter {
    Kind kind = Kind::Copy;
    float value = 1;
    unsigned radius = 0;
    // Symmetric nonnegative kernel, packed in float4 lanes by the graphics adapter.
    std::array<float, 52> weights{};
};
// Admits a single native filter parameter. RML/RCSS parsing stays with RmlUi;
// this owner defines the supported resource and numeric bounds.
Filter make_filter(std::string_view name, double value);
} // namespace forge::ui_filter_detail
