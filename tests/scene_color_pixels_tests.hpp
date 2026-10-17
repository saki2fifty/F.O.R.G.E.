#pragma once
#include <algorithm>
#include <cmath>
#include <forge/scene_render_settings.hpp>
#include <limits>

// Shared D3D12/Vulkan checks of the actual DisplayResolve. The existing tone
// mapper is measured as a baseline, not reimplemented or copied into the test.
template <class Render> void check_scene_color_pixels(const Render& render) {
    using forge::SceneColorAdjustments;
    const auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    const auto decode = [](double byte) {
        const auto value = byte / 255;
        return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4);
    };
    const auto encode = [](double value) {
        value = std::clamp(value, 0., 1.);
        return 255 * (value <= .0031308 ? 12.92 * value : 1.055 * std::pow(value, 1 / 2.4) - .055);
    };
    for (const std::array<float, 4> input : {std::array<float, 4>{.5f, .25f, .125f, .375f},
                                             {.03f, .4f, 2.f, 0},
                                             {4.f, .5f, .1f, 1},
                                             {.18f, .18f, .18f, .5f},
                                             {0, 0, 0, 1}}) {
        const auto baseline = render(input, 0, SceneColorAdjustments{});
        check(render(input, 0, SceneColorAdjustments{true}) == baseline,
              "Enabled neutral color adjustments changed existing pixels");
        check(render(input, 0, SceneColorAdjustments{false, {0, 4, 1}, 0, 2}) == baseline,
              "Disabled color adjustments affected pixels");
        for (const auto settings : {SceneColorAdjustments{true, {1, 1, 1}, 0, 1},
                                    SceneColorAdjustments{true, {1, 1, 1}, 1.7f, .5f},
                                    SceneColorAdjustments{true, {1, 1, 1}, .6f, 1.7f},
                                    SceneColorAdjustments{true, {1, 1, 1}, 2, 0}}) {
            const auto graded = render(input, 0, settings);
            check(graded.size() == baseline.size() && graded[0][3] == baseline[0][3],
                  "Color grading changed output dimensions or alpha");
            double rgb[3];
            for (unsigned channel = 0; channel < 3; ++channel)
                rgb[channel] = decode(baseline[0][channel]);
            const double luminance = .2126 * rgb[0] + .7152 * rgb[1] + .0722 * rgb[2];
            for (const auto& pixel : graded)
                for (unsigned channel = 0; channel < 3; ++channel) {
                    const double saturated =
                        luminance + settings.saturation * (rgb[channel] - luminance);
                    const double expected = encode(.18 + settings.contrast * (saturated - .18));
                    // Baseline is already quantized to 8 bits. Inverse transfer
                    // and up to2x contrast/saturation amplify that uncertainty.
                    check(std::abs(pixel[channel] - expected) <= 3,
                          "Actual graded pixel differs from the linear double-precision reference");
                }
            if (settings.saturation == 0 || settings.contrast == 0)
                check(graded[0][0] == graded[0][1] && graded[0][1] == graded[0][2],
                      "Zero saturation/contrast did not produce neutral gray");
        }
    }
    // Binary-exact HDR values/multipliers: a pre-tone-map filter must match
    // filtering the input itself, including exposure and highlight compression.
    const std::array<float, 4> source{.5f, .25f, .125f, .375f};
    const auto expected = render({.25f, .5f, .125f, .375f}, 1.5f, SceneColorAdjustments{});
    check(render(source, 1.5f, SceneColorAdjustments{true, {.5f, 2, 1}, 1, 1}) == expected,
          "Color filter was applied after tone mapping or lost HDR/exposure ordering");
    check(render(source, 0, SceneColorAdjustments{}) != expected,
          "Color fixture did not exercise a visible adjustment");
    const auto invalid = render({std::numeric_limits<float>::infinity(), 1, 1, 1}, 0,
                                SceneColorAdjustments{true, {0, 0, 0}, 0, 0});
    check(invalid[0] == std::array<unsigned char, 4>{255, 0, 255, 255},
          "Color adjustments concealed nonfinite HDR diagnostics");
}
