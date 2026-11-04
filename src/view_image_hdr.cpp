#include "view_image_hdr.hpp"
#include "view_image.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>

namespace forge {
namespace {
// One owner for the writer's interpretation declaration and owned-file reader.
constexpr std::string_view hdr_prefix =
    "#?RADIANCE\nSOFTWARE=FORGE\nFORMAT=32-bit_rle_rgbe\n"
    "PRIMARIES=0.6400 0.3300 0.3000 0.6000 0.1500 0.0600 0.3127 0.3290\n\n";
std::uint64_t minimum_row_bytes(unsigned width) {
    return std::uint64_t(width) * 4 +
           (width >= 8 ? 4 + 4 * ((std::uint64_t(width) + 126) / 127) : 0);
}
} // namespace
std::vector<std::byte> encode_hdr_image(const Rgb32Pixels& image, std::size_t byte_limit,
                                        std::stop_token stop) {
    const auto cancelled = [&] {
        if (stop.stop_requested())
            throw std::runtime_error("HDR image encoding cancelled");
    };
    cancelled();
    if (!image.width || !image.height || image.width > FORGE_VIEW_IMAGE_MAX_EXTENT ||
        image.height > FORGE_VIEW_IMAGE_MAX_EXTENT ||
        image.rgb.size() != std::size_t(image.width) * image.height * 3 ||
        image.rgb.size() > FORGE_HDR_IMAGE_PIXEL_LIMIT / sizeof(float) || !byte_limit ||
        byte_limit > FORGE_VIEW_IMAGE_ENCODED_LIMIT)
        throw std::runtime_error("HDR output requires bounded complete linear RGB float pixels");
    const std::string header = std::string(hdr_prefix) + "-Y " + std::to_string(image.height) +
                               " +X " + std::to_string(image.width) + "\n";
    const bool packets = image.width >= 8;
    const auto row_size = minimum_row_bytes(image.width);
    const auto encoded_size = header.size() + row_size * image.height;
    if (encoded_size > byte_limit)
        throw std::runtime_error("HDR encoded image exceeds its byte budget");
    std::vector<std::byte> result;
    result.reserve(std::size_t(encoded_size));
    result.insert(result.end(), reinterpret_cast<const std::byte*>(header.data()),
                  reinterpret_cast<const std::byte*>(header.data() + header.size()));
    std::vector<std::array<std::byte, 4>> row(image.width);
    for (unsigned y = 0; y < image.height; ++y) {
        cancelled();
        for (unsigned x = 0; x < image.width; ++x) {
            const auto* rgb = image.rgb.data() + (std::size_t(y) * image.width + x) * 3;
            for (unsigned c = 0; c < 3; ++c)
                if (!std::isfinite(rgb[c]) || rgb[c] < 0)
                    throw std::runtime_error("HDR RGBE requires finite nonnegative RGB");
            const float peak = std::max({rgb[0], rgb[1], rgb[2]});
            if (peak == 0) {
                row[x] = {};
                continue;
            }
            int exponent = 0;
            (void)std::frexp(double(peak), &exponent);
            if (exponent < -127 || exponent > 127)
                throw std::runtime_error("HDR RGBE pixel magnitude is outside its exponent range");
            const double scale = std::ldexp(1., 8 - exponent);
            for (unsigned c = 0; c < 3; ++c)
                row[x][c] = std::byte(unsigned(std::floor(double(rgb[c]) * scale)));
            row[x][3] = std::byte(exponent + 128);
        }
        if (!packets) {
            // Every nonblack pixel has a normalized mantissa >=128, so it
            // cannot collide with the old repeat marker (1,1,1,count).
            for (const auto& pixel : row)
                result.insert(result.end(), pixel.begin(), pixel.end());
        } else {
            result.insert(result.end(), {std::byte{2}, std::byte{2}, std::byte(image.width >> 8),
                                         std::byte(image.width & 255)});
            // Bounded literal packets keep encoded sizes deterministic.
            // Each packet contains at most127 channel bytes.
            for (unsigned c = 0; c < 4; ++c)
                for (unsigned begin = 0; begin < image.width;) {
                    const unsigned count = std::min(127u, image.width - begin);
                    result.push_back(std::byte(count));
                    for (unsigned i = 0; i < count; ++i)
                        result.push_back(row[begin + i][c]);
                    begin += count;
                }
        }
    }
    cancelled();
    return result;
}
ViewImagePreview preview_hdr_image(std::span<const std::byte> source, std::stop_token stop) {
    auto require = [](bool value, const char* reason) {
        if (!value)
            throw std::runtime_error(reason);
    };
    auto cancelled = [&] { require(!stop.stop_requested(), "HDR inspection cancelled"); };
    cancelled();
    require(source.size() <= FORGE_VIEW_IMAGE_ENCODED_LIMIT && source.size() > hdr_prefix.size(),
            "Owned HDR inspection exceeds its encoded byte budget or is truncated");
    const std::string_view text(reinterpret_cast<const char*>(source.data()), source.size());
    require(text.starts_with(hdr_prefix),
            "HDR inspection requires the FORGE writer's fixed working-primary profile");
    std::size_t at = hdr_prefix.size();
    const auto resolution_end = text.substr(at, 32).find('\n');
    require(resolution_end != std::string_view::npos, "Invalid owned HDR resolution line");
    const auto* resolution_limit = text.data() + at + resolution_end;
    auto token = [&](std::string_view expected) {
        require(text.substr(at).starts_with(expected), "Invalid owned HDR resolution");
        at += expected.size();
    };
    auto dimension = [&] {
        unsigned value = 0;
        const auto parsed = std::from_chars(text.data() + at, resolution_limit, value);
        require(parsed.ec == std::errc{} && parsed.ptr != text.data() + at && value &&
                    value <= FORGE_VIEW_IMAGE_MAX_EXTENT,
                "Owned HDR inspection axes must be between 1 and 4096");
        at = std::size_t(parsed.ptr - text.data());
        return value;
    };
    token("-Y ");
    const unsigned height = dimension();
    token(" +X ");
    const unsigned width = dimension();
    token("\n");
    require(minimum_row_bytes(width) * height <= source.size() - at,
            "Truncated owned HDR pixel/packet payload");
    const auto count = std::size_t(width) * height * 4;
    require(count <= FORGE_VIEW_IMAGE_PIXEL_LIMIT, "Owned HDR inspection exceeds64 MiB RGBA");
    std::vector<std::uint8_t> rgba(count, 255);
    std::vector<std::array<std::uint8_t, 4>> row(width);
    auto byte = [&] {
        require(at < source.size(), "Truncated owned HDR pixel/packet");
        return std::to_integer<unsigned>(source[at++]);
    };
    for (unsigned y = 0; y < height; ++y) {
        cancelled();
        if (width < 8) {
            for (auto& pixel : row)
                for (auto& channel : pixel)
                    channel = std::uint8_t(byte());
        } else {
            require(byte() == 2 && byte() == 2 && byte() == width / 256 && byte() == width % 256,
                    "Invalid owned HDR scanline header");
            for (unsigned channel = 0; channel < 4; ++channel)
                for (unsigned x = 0; x < width;) {
                    const unsigned length = byte();
                    // This reader admits the writer's bounded literal profile,
                    // never old repeat markers or unrelated source encodings.
                    require(length && length <= 127 && length <= width - x,
                            "Invalid owned HDR literal packet");
                    for (unsigned i = 0; i < length; ++i)
                        row[x++][channel] = std::uint8_t(byte());
                }
        }
        for (unsigned x = 0; x < width; ++x) {
            const auto& pixel = row[x];
            const unsigned peak = std::max({pixel[0], pixel[1], pixel[2]});
            require(pixel[3] ? peak >= 128 : peak == 0,
                    "Owned HDR inspection needs normalized RGBE texels");
            for (unsigned channel = 0; channel < 3; ++channel) {
                const double linear =
                    pixel[3] ? std::ldexp(double(pixel[channel]), int(pixel[3]) - 136) : 0.;
                const double mapped = linear / (1. + linear);
                const double encoded =
                    mapped <= .0031308 ? 12.92 * mapped : 1.055 * std::pow(mapped, 1. / 2.4) - .055;
                rgba[(std::size_t(y) * width + x) * 4 + channel] =
                    std::uint8_t(std::clamp(std::lround(encoded * 255), 0l, 255l));
            }
        }
    }
    require(at == source.size(), "Trailing bytes in owned HDR inspection artifact");
    cancelled();
    auto result = preview_view_pixels(width, height, rgba, true);
    result.profile = "hdr_reinhard_srgb";
    return result;
}
} // namespace forge
