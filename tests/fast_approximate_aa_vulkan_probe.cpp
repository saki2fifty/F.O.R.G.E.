#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "display_resolve.hpp"
#include "fast_approximate_aa_scene_tests.hpp"
#ifdef FORGE_FAST_AA_UI_TEST
#include "fast_approximate_aa_ui_tests.hpp"
#endif
#include "sprite_draw_render_tests.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

using namespace Diligent;
namespace {
using Color = std::array<float, 4>;
using Pixels = forge::test_sprite_draw::Pixels;
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
RefCntAutoPtr<ITexture> input(IRenderDevice* device, unsigned width, unsigned height,
                              const std::function<Color(unsigned, unsigned)>& at) {
    std::vector<Color> pixels;
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
            pixels.push_back(at(x, y));
    TextureDesc desc;
    desc.Name = "FORGE analytic spatial AA HDR fixture";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = width;
    desc.Height = height;
    desc.Format = TEX_FORMAT_RGBA32_FLOAT;
    desc.BindFlags = BIND_SHADER_RESOURCE;
    TextureSubResData level;
    level.pData = pixels.data();
    level.Stride = width * sizeof(Color);
    TextureData data{&level, 1};
    RefCntAutoPtr<ITexture> texture;
    device->CreateTexture(desc, &data, &texture);
    check(bool(texture), "Spatial AA fixture allocation failed");
    return texture;
}
void exercise(forge::DiligentPresentation& presentation, IDeviceContext* context,
              bool morphological = false) {
    struct Controls {
        bool enabled = false;
        float detail = .5f;
        unsigned steps = 12;
        float threshold = .1f;
    };
    auto* device = presentation.device();
    forge::DisplayResolve display(presentation);
    const auto render = [&](ITexture* image, Controls settings) {
        auto* result = display.resolve(
            context, image->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), 0, {}, {}, 0, false,
            morphological
                ? forge::SceneFastApproximateAntiAliasing{}
                : forge::SceneFastApproximateAntiAliasing{settings.enabled, settings.detail,
                                                          settings.steps},
            morphological
                ? forge::SceneMorphologicalAntiAliasing{settings.enabled, settings.threshold,
                                                        settings.steps, settings.detail}
                : forge::SceneMorphologicalAntiAliasing{});
        check(result->GetTexture() == display.target()->GetTexture(),
              "Post-filter UI target differs from returned display image");
        return forge::test_sprite_draw::read_pixels(device, context, result->GetTexture());
    };
    for (const auto extent : {std::array{1u, 1u}, {1u, 17u}, {17u, 1u}, {48u, 29u}}) {
        const auto image = input(device, extent[0], extent[1],
                                 [](auto, auto) { return Color{.18f, .3f, .5f, .375f}; });
        const auto misses = presentation.cache_misses();
        const auto baseline = render(image, {});
        check(render(image, {false, 1, 1}) == baseline && presentation.cache_misses() == misses,
              "Disabled spatial AA changed pixels or compiled an extra pipeline");
        check(render(image, {true, 1, 16}) == baseline,
              "Spatial AA changed a uniform color, alpha, border or single-pixel extent");
        check(render(image, {}) == baseline, "Disabling spatial AA returned stale filtered data");
    }

    // Independent coverage oracle: integrate an analytic edge over each display
    // pixel. This compares LDR edge reconstruction, not physical GPU performance.
    for (bool transpose : {false, true})
        for (double slope : {.3, -.6, 1.7, 1., -1.})
            for (unsigned channel : {0u, 2u})
                for (float radiance : {1.f, 16.f}) {
                    const unsigned width = transpose ? 64 : 96, height = transpose ? 96 : 64;
                    const auto inside = [=](double x, double y) {
                        return (transpose ? x - slope * y : y - slope * x) >
                               (slope < 0 ? 32.2 : 12.2);
                    };
                    const auto image = input(device, width, height, [&](auto x, auto y) {
                        const auto value = inside(x + .5, y + .5) ? radiance : 0.f;
                        return channel == 0 ? Color{value, value, value, 1} : Color{0, 0, value, 1};
                    });
                    const auto baseline = render(image, {});
                    const auto filtered = render(image, {true, .5f, 12});
                    unsigned brightest = 0, changed = 0;
                    for (const auto& pixel : baseline)
                        brightest = std::max(brightest, unsigned(pixel[channel]));
                    double original_error = 0, filtered_error = 0;
                    for (unsigned y = 0; y < height; ++y)
                        for (unsigned x = 0; x < width; ++x) {
                            unsigned covered = 0;
                            for (unsigned sy = 0; sy < 16; ++sy)
                                for (unsigned sx = 0; sx < 16; ++sx)
                                    covered += inside(x + (sx + .5) / 16, y + (sy + .5) / 16);
                            const double expected = brightest * covered / 256.;
                            const auto index = y * width + x;
                            original_error += std::pow(baseline[index][channel] - expected, 2);
                            filtered_error += std::pow(filtered[index][channel] - expected, 2);
                            changed += baseline[index] != filtered[index];
                            check(filtered[index][3] == baseline[index][3],
                                  "Spatial AA modified central alpha");
                        }
                    std::cout << "Analytic edge transpose=" << transpose << " radiance=" << radiance
                              << " slope=" << slope << " channel=" << channel
                              << " original_error=" << original_error
                              << " filtered_error=" << filtered_error << " changed=" << changed
                              << '\n';
                    check(changed > 10 && filtered_error < original_error * .95,
                          "Spatial AA did not improve independently integrated display-edge "
                          "coverage");
                    check(render(image, {true, .5f, 12}) == filtered,
                          "Spatial AA unexpectedly retained temporal history");
                    const auto last_good = filtered;
                    bool rejected = false;
                    try {
                        render(image, {true, std::numeric_limits<float>::quiet_NaN(), 12});
                    } catch (const std::exception&) {
                        rejected = true;
                    }
                    check(rejected &&
                              forge::test_sprite_draw::read_pixels(
                                  device, context, display.output()->GetTexture()) == last_good,
                          "Invalid spatial AA settings changed the previous output");
                }
    const auto dots = input(device, 64, 48, [](auto x, auto y) {
        const float value = x % 8 == 4 && y % 8 == 4 ? 1.f : 0.f;
        return Color{value, value, value, 1};
    });
    const auto dot_baseline = render(dots, {});
    const auto dot_filtered = render(dots, {true, .5f, 12});
    unsigned original_energy = 0, filtered_energy = 0;
    for (std::size_t i = 0; i < dot_baseline.size(); ++i) {
        original_energy += dot_baseline[i][0];
        filtered_energy += dot_filtered[i][0];
    }
    check(morphological ? dot_filtered == dot_baseline
                        : dot_filtered != dot_baseline && filtered_energy > original_energy * .65 &&
                              filtered_energy < original_energy * 1.35,
          "Default subpixel smoothing erased or amplified isolated fine details");
    if (morphological) {
        for (bool thin : {false, true}) {
            const auto image = input(device, 64, 48, [&](auto x, auto y) {
                const float value =
                    x >= 16 && x < 48 && y >= 12 && y < (thin ? 13u : 36u) ? 1.f : 0.f;
                return Color{value, value, value, 1};
            });
            const auto baseline = render(image, {});
            check(render(image, {true, 0, 16}) == baseline,
                  "Morphological reconstruction rounded a sharp box or erased one-pixel detail");
            if (!thin)
                check(render(image, {true, 1, 16}) != baseline,
                      "Morphological corner rounding control did not affect detected corners");
        }
    }
    if (morphological) {
        const auto inside = [](double x, double y) { return y < .3 * x + 15.25; };
        const auto image = input(device, 64, 48, [&](auto x, auto y) {
            const float value = inside(x + .5, y + .5) ? .22f : .18f;
            return Color{value, value, value, 1};
        });
        const auto baseline = render(image, {});
        check(render(image, {true, 0, 32, .1f}) == baseline,
              "Morphological threshold did not reject low contrast");
        const auto detailed = render(image, {true, 0, 32, .01f});
        check(detailed != baseline,
              "Lower morphological threshold did not reveal low-contrast contours");
        double before = 0, after = 0;
        const double bright = baseline[0][0], dark = baseline[47 * 64][0];
        for (unsigned y = 0; y < 48; ++y)
            for (unsigned x = 0; x < 64; ++x) {
                unsigned covered = 0;
                for (unsigned sy = 0; sy < 16; ++sy)
                    for (unsigned sx = 0; sx < 16; ++sx)
                        covered += inside(x + (sx + .5) / 16, y + (sy + .5) / 16);
                const double expected = dark + (bright - dark) * covered / 256.;
                before += std::pow(baseline[y * 64 + x][0] - expected, 2);
                after += std::pow(detailed[y * 64 + x][0] - expected, 2);
            }
        check(after < before * .95,
              "Low-contrast morphological output did not improve integrated coverage");
        bool conflict = false;
        try {
            display.resolve(context, image->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), 0, {}, {},
                            0, false, {true}, {true});
        } catch (const std::exception&) {
            conflict = true;
        }
        check(conflict && forge::test_sprite_draw::read_pixels(
                              device, context, display.output()->GetTexture()) == detailed,
              "Competing native spatial methods replaced the last usable output");
    }
    const auto alpha_image = input(device, 31, 19, [](auto x, auto y) {
        const float v = (x + y) % 2 ? 1.f : 0.f;
        return Color{v, v, v, (x + y) % 2 ? .25f : .75f};
    });
    const auto alpha_baseline = render(alpha_image, {});
    check(render(alpha_image, {true, 1, 16}) == alpha_baseline,
          "Spatial AA filtered across an alpha discontinuity");

    const auto previous = render(alpha_image, {true, .5f, 12});
    TextureDesc oversized;
    oversized.Name = "FORGE spatial AA pixel budget fixture";
    oversized.Type = RESOURCE_DIM_TEX_2D;
    oversized.Width = 4096;
    oversized.Height = 2049;
    oversized.Format = TEX_FORMAT_RGBA16_FLOAT;
    oversized.BindFlags = BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> large;
    device->CreateTexture(oversized, nullptr, &large);
    check(bool(large), "Spatial AA budget fixture allocation failed");
    bool rejected = false;
    try {
        render(large, {true});
    } catch (const std::exception& error) {
        rejected = std::string_view(error.what()).find("8,388,608") != std::string_view::npos;
    }
    check(rejected && forge::test_sprite_draw::read_pixels(
                          device, context, display.output()->GetTexture()) == previous,
          "Oversized spatial AA request changed the previous display output");
    context->WaitForIdle();
    std::cout << "Spatial AA: disabled identity, uniform/resize/border/alpha, analytic LDR/HDR "
                 "edges, no history and retained budget/invalid output passed\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo info;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(info, &device, &context);
        check(device && context, "Spatial AA Vulkan device unavailable");
        forge::DiligentPresentation presentation(device);
#ifdef FORGE_FAST_AA_UI_TEST
        if (argc == 4 && (std::string_view(argv[1]) == "--ui" ||
                          std::string_view(argv[1]) == "--morphological-ui")) {
            const auto image =
                input(device, 640, 320, [](auto, auto) { return Color{.1f, .2f, .3f, 1}; });
            forge::test_fast_aa_ui::exercise(
                presentation, context, image->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
                std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]),
                std::string_view(argv[1]) == "--morphological-ui");
            std::cout << "Spatial AA: real native Game UI remains byte-identical after display "
                         "filtering\n";
            return 0;
        }
#endif
        const bool morphological = argc >= 2 && std::string_view(argv[1]) == "--morphological";
        exercise(presentation, context, morphological);
        if (morphological) {
            if (argc == 3)
                forge::test_fast_aa_scene::exercise(presentation, context,
                                                    std::filesystem::absolute(argv[2]), true);
            else
                check(argc == 2, "Expected optional morphological Scene fixture");
            return 0;
        }
        if (argc == 2)
            forge::test_fast_aa_scene::exercise(presentation, context,
                                                std::filesystem::absolute(argv[1]));
        else
            check(argc == 1, "Expected optional Scene fixture directory");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
