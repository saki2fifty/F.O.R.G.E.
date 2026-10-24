#pragma once
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
    constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const auto a = std::to_integer<unsigned>(bytes[i]);
        const auto b = i + 1 < bytes.size() ? std::to_integer<unsigned>(bytes[i + 1]) : 0;
        const auto c = i + 2 < bytes.size() ? std::to_integer<unsigned>(bytes[i + 2]) : 0;
        result.push_back(alphabet[a >> 2]);
        result.push_back(alphabet[((a & 3) << 4) | (b >> 4)]);
        result.push_back(i + 1 < bytes.size() ? alphabet[((b & 15) << 2) | (c >> 6)] : '=');
        result.push_back(i + 2 < bytes.size() ? alphabet[c & 63] : '=');
    }
    return result;
}
// Validate the bounded canonical wire encoding before forwarding a native image
// block. Producers use the engine PNG codec; the adapter never opens file paths.
inline std::size_t automation_png_wire_bytes(std::string_view encoded) {
    if (encoded.empty() || encoded.size() % 4 ||
        encoded.size() > (automation_image_limit + 2) / 3 * 4)
        throw std::runtime_error("Invalid automation PNG encoding");
    const auto digit = [](unsigned char c) -> unsigned {
        if (c >= 'A' && c <= 'Z')
            return c - 'A';
        if (c >= 'a' && c <= 'z')
            return c - 'a' + 26;
        if (c >= '0' && c <= '9')
            return c - '0' + 52;
        if (c == '+')
            return 62;
        if (c == '/')
            return 63;
        throw std::runtime_error("Invalid automation PNG encoding");
    };
    std::array<unsigned char, 24> header{};
    std::size_t bytes = 0;
    for (std::size_t i = 0; i < encoded.size(); i += 4) {
        const bool last = i + 4 == encoded.size(), pad2 = encoded[i + 2] == '=',
                   pad3 = encoded[i + 3] == '=';
        if ((!last && (pad2 || pad3)) || (pad2 && !pad3))
            throw std::runtime_error("Invalid automation PNG padding");
        const auto a = digit(encoded[i]), b = digit(encoded[i + 1]);
        const auto c = pad2 ? 0 : digit(encoded[i + 2]), d = pad3 ? 0 : digit(encoded[i + 3]);
        if ((pad2 && (b & 15)) || (pad3 && !pad2 && (c & 3)))
            throw std::runtime_error("Noncanonical automation PNG padding");
        const std::array<unsigned char, 3> part{static_cast<unsigned char>((a << 2) | (b >> 4)),
                                                static_cast<unsigned char>((b << 4) | (c >> 2)),
                                                static_cast<unsigned char>((c << 6) | d)};
        for (unsigned j = 0; j < (pad2 ? 1u : pad3 ? 2u : 3u); ++j) {
            if (bytes < header.size())
                header[bytes] = part[j];
            ++bytes;
        }
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
