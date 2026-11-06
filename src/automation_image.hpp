#pragma once
#include "base64.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
namespace forge {
inline constexpr std::size_t automation_image_limit = 8 * 1024 * 1024;
inline std::string automation_image_base64(std::span<const std::byte> bytes) {
    if (bytes.size() > automation_image_limit)
        throw std::runtime_error("Automation image exceeds8MiB");
    return encode_base64(bytes);
}
// Validate the bounded canonical wire encoding before forwarding a native image
// block. Producers use the engine PNG codec; the adapter never opens file paths.
inline std::size_t automation_png_wire_bytes(std::string_view encoded) {
    if (encoded.empty() || encoded.size() % 4 ||
        encoded.size() > (automation_image_limit + 2) / 3 * 4)
        throw std::runtime_error("Invalid automation PNG encoding");
    std::array<unsigned char, 24> header{};
    std::size_t bytes = 0;
    try {
        bytes =
            visit_base64(encoded, automation_image_limit, [&](std::size_t offset, std::byte value) {
                if (offset < header.size())
                    header[offset] = std::to_integer<unsigned char>(value);
            });
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string("Invalid automation PNG encoding: ") + error.what());
    }
    constexpr std::array<unsigned char, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
    if (bytes > automation_image_limit || bytes < 33 ||
        !std::equal(signature.begin(), signature.end(), header.begin()) || header[8] != 0 ||
        header[9] != 0 || header[10] != 0 || header[11] != 13 || header[12] != 'I' ||
        header[13] != 'H' || header[14] != 'D' || header[15] != 'R')
        throw std::runtime_error("Invalid automation PNG header");
    const auto integer = [&](unsigned i) {
        return (std::uint32_t(header[i]) << 24) | (std::uint32_t(header[i + 1]) << 16) |
               (std::uint32_t(header[i + 2]) << 8) | header[i + 3];
    };
    const auto width = integer(16), height = integer(20);
    if (!width || !height || width > 4096 || height > 4096)
        throw std::runtime_error("Automation PNG extent exceeds its profile");
    return bytes;
}
} // namespace forge
