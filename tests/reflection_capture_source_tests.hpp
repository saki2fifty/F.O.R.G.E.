#pragma once
#include "reflection_capture_source.hpp"
#include "texture_import.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
namespace forge::test_reflection_capture_source {
inline void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
inline TextureData fixture(unsigned size, unsigned pattern) {
    TextureData result;
    result.dimension = TextureDimension::Cube;
    result.width = result.height = size;
    result.format = TextureFormat::RGBA16Float;
    result.semantic = TextureSemantic::HdrColor;
    result.alpha = TextureAlpha::Opaque;
    constexpr std::array<std::uint16_t, 6> values{0, 0x3400, 0x3c00, 0x4800, 0x6400, 0x7bff};
    for (unsigned face = 0; face < 6; ++face) {
        std::vector<std::byte> pixels(std::size_t(size) * size * 8);
        for (unsigned y = 0; y < size; ++y)
            for (unsigned x = 0; x < size; ++x)
                for (unsigned c = 0; c < 4; ++c) {
                    const auto bits =
                        c == 3 ? 0x3c00u : values[(face + c + pattern * x + y) % values.size()];
                    const auto at = (std::size_t(y) * size + x) * 8 + c * 2;
                    pixels[at] = std::byte(bits & 255);
                    pixels[at + 1] = std::byte(bits >> 8);
                }
        result.subresources.push_back(std::move(pixels));
    }
    return result;
}
inline void run() {
    unsigned cases = 0, rejected = 0;
    for (unsigned size : {1u, 3u, 16u, 31u, 64u})
        for (unsigned pattern = 0; pattern < 3; ++pattern) {
            const auto original = fixture(size, pattern);
            const auto bytes = encode_reflection_capture_source(original);
            const auto imported =
                asset_detail::import_texture_dds(bytes, TextureSemantic::HdrColor);
            check(imported.dimension == original.dimension && imported.format == original.format &&
                      imported.semantic == original.semantic && imported.alpha == original.alpha &&
                      imported.width == size && imported.height == size && imported.depth == 1 &&
                      imported.layers == 1 && imported.mips == 1 &&
                      imported.subresources == original.subresources,
                  "Native DDS import changed cube metadata or face/channel/HDR "
                  "bytes");
            const auto cooked = encode_texture(imported);
            const auto runtime = decode_texture(cooked);
            check(runtime.subresources == original.subresources &&
                      runtime.dimension == TextureDimension::Cube &&
                      runtime.format == TextureFormat::RGBA16Float &&
                      runtime.semantic == TextureSemantic::HdrColor,
                  "Cooked texture round trip changed admitted HDR cube");
            check(encode_reflection_capture_source(original) == bytes,
                  "Identical cube source bytes are not repeatable");

            ++cases;
        }
    const auto reject = [&](auto action) {
        bool failed = false;
        try {
            action();
        } catch (const std::exception&) {
            failed = true;
        }
        check(failed, "Invalid capture source was admitted");
        ++rejected;
    };
    const auto good = fixture(3, 0);
    for (unsigned bad = 0; bad < 8; ++bad) {
        auto value = good;
        switch (bad) {
        case 0:
            value.dimension = TextureDimension::D2;
            break;
        case 1:
            value.format = TextureFormat::RGBA8;
            break;
        case 2:
            value.semantic = TextureSemantic::Normal;
            break;
        case 3:
            value.alpha = TextureAlpha::Straight;
            break;
        case 4:
            value.mips = 2;
            break;
        case 5:
            value.layers = 2;
            break;
        case 6:
            value.subresources.pop_back();
            break;
        case 7:
            value.subresources[0].pop_back();
            break;
        }
        reject([&] { (void)encode_reflection_capture_source(value); });
    }
    for (std::uint16_t bad :
         {std::uint16_t(0x7c00), std::uint16_t(0x7e01), std::uint16_t(0xbc00)}) {
        auto value = good;
        value.subresources[0][0] = std::byte(bad & 255);
        value.subresources[0][1] = std::byte(bad >> 8);
        reject([&] { (void)encode_reflection_capture_source(value); });
    }
    auto transparent = good;
    transparent.subresources[0][7] = std::byte(0);
    reject([&] { (void)encode_reflection_capture_source(transparent); });
    auto limit = TextureLimits{};
    limit.bytes = good.byte_size() + 147;
    reject([&] { (void)encode_reflection_capture_source(good, limit); });
    auto truncated = encode_reflection_capture_source(good);
    truncated.pop_back();
    reject([&] { (void)asset_detail::import_texture_dds(truncated, TextureSemantic::HdrColor); });
    auto extra = encode_reflection_capture_source(good);
    extra.push_back(std::byte(0));
    reject([&] { (void)asset_detail::import_texture_dds(extra, TextureSemantic::HdrColor); });
    std::cout << cases << " HDR cube source/native import/runtime round trips PASS; " << rejected
              << " profile/finite/alpha/budget/container rejections PASS\n";
}
} // namespace forge::test_reflection_capture_source
