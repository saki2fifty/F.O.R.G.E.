#pragma once
#include "hdr_bloom.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string_view>
#include <utility>
namespace forge::test_bloom {
using Pixel = std::array<float, 4>;
inline void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
inline std::uint16_t half(float value) {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    const auto sign = (bits >> 16) & 0x8000;
    const int exponent = int((bits >> 23) & 255) - 127 + 15;
    const auto mantissa = bits & 0x7fffff;
    if (exponent >= 31)
        return std::uint16_t(sign | 0x7c00 | (mantissa ? 0x200 : 0));
    if (exponent <= 0) {
        if (exponent < -10)
            return std::uint16_t(sign);
        const unsigned shift = unsigned(14 - exponent);
        const auto full = mantissa | 0x800000;
        const auto rounded = (full + ((1u << (shift - 1)) - 1) + ((full >> shift) & 1)) >> shift;
        return std::uint16_t(sign | rounded);
    }
    return std::uint16_t(
        sign | ((unsigned(exponent) << 10) + ((mantissa + 4095 + ((mantissa >> 13) & 1)) >> 13)));
}
inline float value(std::uint16_t half_value) {
    const unsigned exponent = (half_value >> 10) & 31, fraction = half_value & 1023;
    const float magnitude = exponent == 0 ? std::ldexp(float(fraction), -24)
                            : exponent == 31
                                ? (fraction ? std::numeric_limits<float>::quiet_NaN()
                                            : std::numeric_limits<float>::infinity())
                                : std::ldexp(float(1024 + fraction), int(exponent) - 25);
    return half_value & 0x8000 ? -magnitude : magnitude;
}
inline Diligent::RefCntAutoPtr<Diligent::ITexture> texture(Diligent::IRenderDevice* device,
                                                           unsigned width, unsigned height,
                                                           const std::vector<Pixel>& pixels) {
    using namespace Diligent;
    check(pixels.size() == std::size_t(width) * height, "Bloom fixture pixel count wrong");
    std::vector<std::uint16_t> bytes;
    for (const auto& pixel : pixels)
        for (auto channel : pixel)
            bytes.push_back(half(channel));
    TextureDesc desc;
    desc.Name = "FORGE bloom numeric HDR input";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = width;
    desc.Height = height;
    desc.Format = TEX_FORMAT_RGBA16_FLOAT;
    desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    TextureSubResData level;
    level.pData = bytes.data();
    level.Stride = width * 8;
    Diligent::TextureData data{&level, 1};
    RefCntAutoPtr<ITexture> result;
    device->CreateTexture(desc, &data, &result);
    check(bool(result), "Bloom fixture texture creation failed");
    return result;
}
inline std::vector<Pixel> read(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
                               Diligent::ITexture* source) {
    using namespace Diligent;
    auto desc = source->GetDesc();
    desc.Name = "FORGE bloom numeric readback";
    desc.Usage = USAGE_STAGING;
    desc.BindFlags = BIND_NONE;
    desc.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<ITexture> staging;
    device->CreateTexture(desc, nullptr, &staging);
    check(bool(staging), "Bloom readback texture creation failed");
    context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    CopyTextureAttribs copy{source, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging,
                            RESOURCE_STATE_TRANSITION_MODE_TRANSITION};
    context->CopyTexture(copy);
    context->WaitForIdle();
    MappedTextureSubresource mapped;
    context->MapTextureSubresource(staging, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr, mapped);
    check(mapped.pData != nullptr, "Bloom HDR readback map failed");
    std::vector<Pixel> pixels(std::size_t(desc.Width) * desc.Height);
    for (unsigned y = 0; y < desc.Height; ++y) {
        const auto* row = static_cast<const std::byte*>(mapped.pData) + y * mapped.Stride;
        for (unsigned x = 0; x < desc.Width; ++x)
            for (unsigned c = 0; c < 4; ++c) {
                std::uint16_t raw;
                std::memcpy(&raw, row + (x * 4 + c) * 2, 2);
                pixels[y * desc.Width + x][c] = value(raw);
            }
    }
    context->UnmapTextureSubresource(staging, 0, 0);
    return pixels;
}
// Independent double-precision oracle: a 25-tap 2D convolution of extracted
// view-local highlights, rather than the GPU's two five-tap intermediate passes.
inline std::vector<Pixel> oracle(const std::vector<Pixel>& input, unsigned width,
                                 PixelViewport area, const SceneBloom& settings) {
    auto result = input;
    const unsigned hw = (area.width + 1) / 2, hh = (area.height + 1) / 2;
    using RGB = std::array<double, 3>;
    std::vector<RGB> highlights(hw * hh), filtered(hw * hh);
    for (unsigned y = 0; y < hh; ++y)
        for (unsigned x = 0; x < hw; ++x)
            for (unsigned dy = 0; dy < 2; ++dy)
                for (unsigned dx = 0; dx < 2; ++dx) {
                    const auto& pixel =
                        input[(area.y + std::min(2 * y + dy, area.height - 1)) * width + area.x +
                              std::min(2 * x + dx, area.width - 1)];
                    const double peak = std::max({0.f, pixel[0], pixel[1], pixel[2]});
                    for (unsigned c = 0; c < 3; ++c)
                        highlights[y * hw + x][c] +=
                            std::max(0.f, pixel[c]) *
                            (peak ? std::max(0., peak - settings.threshold) / peak / 4 : 0);
                }
    auto sample = [&](const auto& pixels, double x, double y, unsigned c) {
        x = std::clamp(x, 0., double(hw - 1));
        y = std::clamp(y, 0., double(hh - 1));
        const auto ix = unsigned(x), iy = unsigned(y);
        const auto nx = std::min(ix + 1, hw - 1), ny = std::min(iy + 1, hh - 1);
        return std::lerp(std::lerp(pixels[iy * hw + ix][c], pixels[iy * hw + nx][c], x - ix),
                         std::lerp(pixels[ny * hw + ix][c], pixels[ny * hw + nx][c], x - ix),
                         y - iy);
    };
    constexpr double weights[]{1, 4, 6, 4, 1};
    for (unsigned y = 0; y < hh; ++y)
        for (unsigned x = 0; x < hw; ++x)
            for (int ky = -2; ky <= 2; ++ky)
                for (int kx = -2; kx <= 2; ++kx)
                    for (unsigned c = 0; c < 3; ++c)
                        filtered[y * hw + x][c] +=
                            sample(highlights, x + kx * double(settings.radius),
                                   y + ky * double(settings.radius), c) *
                            weights[kx + 2] * weights[ky + 2] / 256;
    for (unsigned y = 0; y < area.height; ++y)
        for (unsigned x = 0; x < area.width; ++x)
            for (unsigned c = 0; c < 3; ++c) {
                auto& output = result[(area.y + y) * width + area.x + x][c];
                output = float(
                    std::min(65504., output + settings.intensity *
                                                  sample(filtered, (x + .5) * hw / area.width - .5,
                                                         (y + .5) * hh / area.height - .5, c)));
            }
    return result;
}
inline void exercise(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
                     HdrBloom& bloom, std::uint64_t budget = hdr_bloom_payload_limit) {
    std::size_t comparisons = 0;
    auto check_case = [&](unsigned width, unsigned height, PixelViewport area,
                          const std::vector<Pixel>& pixels, SceneBloom settings) {
        auto image = texture(device, width, height, pixels);
        const auto before = read(device, context, image);
        const auto allocations = bloom.allocations(), draws = bloom.draws();
        bloom.apply(context, image, area, settings);
        check(bloom.payload_bytes() <= budget, "Bloom exceeded its aggregate payload budget");
        const auto actual = read(device, context, image);
        if (!settings.enabled || settings.intensity == 0) {
            check(actual == before && bloom.allocations() == allocations && bloom.draws() == draws,
                  "Disabled/zero bloom changed pixels, allocated or drew");
            return actual;
        }
        check(bloom.draws() == draws + 4, "Bloom did not submit exactly four draw passes");
        const auto expected = oracle(before, width, area, settings);
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x) {
                const auto index = y * width + x;
                const bool inside = x >= area.x && y >= area.y && x - area.x < area.width &&
                                    y - area.y < area.height;
                check(actual[index][3] == before[index][3], "Bloom changed source alpha");
                if (!inside)
                    check(actual[index] == before[index], "Bloom escaped its camera rectangle");
                for (unsigned c = 0; c < 3; ++c) {
                    const auto tolerance =
                        std::max(.003, std::abs(double(expected[index][c])) * .004);
                    if (!std::isfinite(actual[index][c]) ||
                        std::abs(double(actual[index][c]) - expected[index][c]) > tolerance)
                        throw std::runtime_error("Bloom pixel disagreed with 2D oracle at " +
                                                 std::to_string(x) + "," + std::to_string(y) +
                                                 " channel " + std::to_string(c) + " actual " +
                                                 std::to_string(actual[index][c]) + " expected " +
                                                 std::to_string(expected[index][c]) + " radius " +
                                                 std::to_string(settings.radius) + " intensity " +
                                                 std::to_string(settings.intensity));
                    ++comparisons;
                }
            }
        return actual;
    };
    std::vector<Pixel> below(19 * 13, {.5f, .25f, .125f, .5f});
    check_case(19, 13, {3, 2, 11, 7}, below, {});
    check_case(19, 13, {3, 2, 11, 7}, below, {true, 1, 0, 2});
    check(bloom.allocations() == 0, "Disabled bloom created GPU targets");
    check(check_case(19, 13, {3, 2, 11, 7}, below, {true, 1, 1, 2}) == below,
          "Below-threshold light generated glow");
    const auto allocated = bloom.allocations();
    auto impulse = below;
    impulse[5 * 19 + 8] = {16, 8, 4, .25f};
    impulse[2 * 19 + 2] = {128, 64, 32, .75f}; // Outside the view must not contribute.
    for (float radius : {.5f, 1.f, 2.f, 3.25f, 16.f})
        check_case(19, 13, {3, 2, 11, 7}, impulse, {true, 1, .75f, radius});
    check(bloom.allocations() == allocated, "Bloom recreated same-size targets each frame");
    check_case(1, 1, {0, 0, 1, 1}, {{16, 8, 4, .125f}}, {true, 1, .5f, 1});
    check_case(7, 5, {1, 1, 5, 3}, std::vector<Pixel>(35, {8, 4, 2, .75f}), {true, 2, 4, 2});
    check_case(3, 3, {0, 0, 3, 3}, std::vector<Pixel>(9, {65504, 32752, 16376, 1}),
               {true, 0, 4, 2});
    check_case(19, 13, {3, 2, 11, 7}, impulse, {true, 1, .75f, 2});
    const auto warmed = bloom.allocations();
    for (unsigned i = 0; i < 3; ++i) {
        check_case(7, 5, {1, 1, 5, 3}, std::vector<Pixel>(35, {8, 4, 2, .75f}), {true, 2, 4, 2});
        check_case(19, 13, {3, 2, 11, 7}, impulse, {true, 1, .75f, 2});
    }
    check(bloom.allocations() == warmed && bloom.payload_bytes() <= hdr_bloom_payload_limit,
          "Alternating camera sizes allocated after warmup or exceeded aggregate budget");
    for (unsigned width = 11; width <= 22; ++width)
        check_case(width, 3, {0, 0, width, 3}, std::vector<Pixel>(width * 3, {4, 2, 1, .25f}),
                   {true, 1, .5f, 1});
    const auto evicted = bloom.allocations();
    check_case(19, 13, {3, 2, 11, 7}, impulse, {true, 1, .75f, 2});
    check(bloom.allocations() == evicted + 3, "Bounded bloom cache did not evict old dimensions");
    const auto last_payload = bloom.payload_bytes(), last_draws = bloom.draws();
    auto image = texture(device, 19, 13, impulse);
    const auto unchanged = read(device, context, image);
    for (auto area : {PixelViewport{18, 0, 2, 1}, PixelViewport{0, 0, 0, 1}}) {
        bool rejected = false;
        try {
            bloom.apply(context, image, area, {true, 1, 1, 1});
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && bloom.payload_bytes() == last_payload && bloom.draws() == last_draws &&
                  read(device, context, image) == unchanged,
              "Rejected rectangle changed last-good targets or source");
    }
    auto desc = image->GetDesc();
    desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
    Diligent::RefCntAutoPtr<Diligent::ITexture> ldr;
    device->CreateTexture(desc, nullptr, &ldr);
    check(bool(ldr), "LDR rejection fixture creation failed");
    bool rejected = false;
    try {
        bloom.apply(context, ldr, {0, 0, 19, 13}, {true, 1, 1, 1});
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected && bloom.payload_bytes() == last_payload && bloom.draws() == last_draws,
          "Unsupported source format changed pass resources");
    if (budget < hdr_bloom_plan(25, 25, {0, 0, 25, 25}, 25).bytes) {
        auto large = texture(device, 25, 25, std::vector<Pixel>(625, {4, 2, 1, 1}));
        rejected = false;
        try {
            bloom.apply(context, large, {0, 0, 25, 25}, {true, 1, 1, 1});
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && bloom.payload_bytes() == last_payload && bloom.draws() == last_draws,
              "Oversized backend profile request changed previous pass state");
    }
    // Nonfinite source pixels remain diagnostic inputs to display resolve.
    // They must not poison nearby finite pixels through highlight extraction.
    std::vector<Pixel> diagnostic(25, {.5f, .25f, .125f, .5f});
    diagnostic[6][0] = std::numeric_limits<float>::quiet_NaN();
    diagnostic[7][1] = std::numeric_limits<float>::infinity();
    diagnostic[11][2] = -std::numeric_limits<float>::infinity();
    diagnostic[12][3] = std::numeric_limits<float>::quiet_NaN();
    diagnostic[18] = {-.5f, -.25f, -.125f, .5f};
    auto diagnostic_image = texture(device, 5, 5, diagnostic);
    const auto diagnostic_before = read(device, context, diagnostic_image);
    bloom.apply(context, diagnostic_image, {1, 1, 3, 3}, {true, 1, 1, .5f});
    const auto diagnostic_after = read(device, context, diagnostic_image);
    for (unsigned p = 0; p < diagnostic.size(); ++p)
        for (unsigned c = 0; c < 4; ++c)
            check(std::isnan(diagnostic_before[p][c])
                      ? std::isnan(diagnostic_after[p][c])
                      : diagnostic_after[p][c] == diagnostic_before[p][c],
                  "Bloom lost a nonfinite diagnostic or contaminated below-threshold neighbors");
    std::cout << comparisons << " HDR channels vs independent 2D convolution; view isolation, "
              << "alpha, identity, target reuse/resize, finite clamp and failed admission PASS\n";
}
// Native acceptance captures. Keep the original four-pass CPU
// path and first three shaders; substitute the final output to expose its inputs.
inline void diagnose(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                     const std::filesystem::path& root) {
    using namespace Diligent;
    const std::array<std::pair<const char*, const char*>, 8> variants{
        {{"output", "return float4(min(original.rgb+glow,65504),original.a);"},
         {"filtered", "return float4(g_Source.Load(int3(clamp(p,int2(0,0),"
                      "int2(width-1,height-1)),0)).rgb,original.a);"},
         {"coordinates", "return float4(glow_position,width,height)+"
                         "(g_Source.Load(int3(0,0,0)).r<0?original:0);"},
         {"device-dimensions", "uint reported_width,reported_height;"
                               "g_Source.GetDimensions(reported_width,reported_height);"
                               "return float4(g_SourceSize.xy,reported_width,reported_height);"},
         {"device-float-dimensions",
          "float reported_width,reported_height;"
          "g_Source.GetDimensions(reported_width,reported_height);"
          "return float4(g_SourceSize.xy,reported_width,reported_height);"},
         {"device-mip-dimensions",
          "uint reported_width,reported_height,levels;"
          "g_Source.GetDimensions(0,reported_width,reported_height,levels);"
          "return float4(g_SourceSize.x,reported_width,reported_height,levels);"},
         {"rectangle", "return g_Rect+(g_Source.Load(int3(0,0,0)).r<0?original:0);"},
         {"settings", "return g_Settings+(g_Source.Load(int3(0,0,0)).r<0?original:0);"}}};
    std::vector<Pixel> below(19 * 13, {.5f, .25f, .125f, .5f});
    auto impulse = below;
    impulse[5 * 19 + 8] = {16, 8, 4, .25f};
    impulse[2 * 19 + 2] = {128, 64, 32, .75f};
    for (bool warm : {false, true})
        for (const auto& [name, replacement] : variants) {
            HdrBloom bloom(
                presentation.device(),
                [&](const ShaderCreateInfo& input, IShader** out) {
                    auto selected = input;
                    std::string source(input.Source);
                    if (std::string_view(input.Desc.Name).ends_with("composite")) {
                        const std::string original =
                            "return float4(min(original.rgb+glow,65504),original.a);";
                        const auto at = source.find(original);
                        check(at != std::string::npos, "Bloom diagnostic output boundary missing");
                        source.replace(at, original.size(), replacement);
                    }
                    selected.Source = source.c_str();
                    presentation.shader(selected, out);
                },
                [&](const auto& input, auto** out) { presentation.graphics(input, out); });
            auto execute = [&](const auto& pixels, float radius) {
                auto image = texture(presentation.device(), 19, 13, pixels);
                (void)read(presentation.device(), context, image);
                bloom.apply(context, image, {3, 2, 11, 7}, {true, 1, .75f, radius});
                return read(presentation.device(), context, image);
            };
            if (warm) {
                (void)execute(below, 2);
                (void)execute(impulse, .5f);
            }
            const auto pixels = execute(impulse, 1);
            const auto label = std::string(warm ? "warm-" : "fresh-") + name;
            std::ofstream file(root / ("bloom-" + label + ".csv"));
            file << "x,y,r,g,b,a\n";
            for (unsigned y = 2; y < 9; ++y)
                for (unsigned x = 3; x < 14; ++x) {
                    file << x << ',' << y;
                    for (const auto value : pixels[y * 19 + x])
                        file << ',' << value;
                    file << '\n';
                }
            check(bool(file.flush()), "Bloom diagnostic capture write failed");
            std::cout << "Bloom " << label << " at 3,3:";
            for (const auto value : pixels[3 * 19 + 3])
                std::cout << ' ' << value;
            std::cout << '\n';
        }
}
} // namespace forge::test_bloom
