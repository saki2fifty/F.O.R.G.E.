#include "view_image_hdr.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

namespace forge {
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
    const std::string header =
        "#?RADIANCE\nSOFTWARE=FORGE\nFORMAT=32-bit_rle_rgbe\n"
        "PRIMARIES=0.6400 0.3300 0.3000 0.6000 0.1500 0.0600 0.3127 0.3290\n\n-Y " +
        std::to_string(image.height) + " +X " + std::to_string(image.width) + "\n";
    const bool packets = image.width >= 8;
    const auto row_size =
        std::uint64_t(image.width) * 4 + (packets ? 4 + 4 * ((image.width + 126) / 127) : 0);
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
} // namespace forge
