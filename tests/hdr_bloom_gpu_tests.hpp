#pragma once
#include "hdr_bloom.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
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
                                                 std::to_string(expected[index][c]));
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
    std::cout << comparisons << " HDR channels vs independent 2D convolution; view isolation, "
              << "alpha, identity, target reuse/resize, finite clamp and failed admission PASS\n";
}
} // namespace forge::test_bloom
