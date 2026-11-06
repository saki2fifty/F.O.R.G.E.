#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace forge {
inline std::string encode_base64(std::span<const std::byte> bytes) {
    constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    if (bytes.size() > result.max_size() / 4 * 3)
        throw std::length_error("Base64 output exceeds string storage");
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
// A bounded visitor also serves header-only validators without allocating the
// complete binary payload. Callers retain their format-specific admission policy.
template <class Sink>
std::size_t visit_base64(std::string_view encoded, std::size_t limit, Sink sink) {
    if (encoded.size() % 4)
        throw std::runtime_error("Invalid base64 length");
    if (encoded.empty())
        return 0;
    const auto padding =
        unsigned(encoded.back() == '=') + unsigned(encoded[encoded.size() - 2] == '=');
    const auto size = encoded.size() / 4 * 3 - padding;
    if (size > limit)
        throw std::runtime_error("Decoded base64 exceeds its byte limit");
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
        throw std::runtime_error("Invalid base64 character");
    };
    std::size_t bytes = 0;
    for (std::size_t i = 0; i < encoded.size(); i += 4) {
        const bool last = i + 4 == encoded.size(), pad2 = encoded[i + 2] == '=',
                   pad3 = encoded[i + 3] == '=';
        if ((!last && (pad2 || pad3)) || (pad2 && !pad3))
            throw std::runtime_error("Invalid base64 padding");
        const auto a = digit(encoded[i]), b = digit(encoded[i + 1]);
        const auto c = pad2 ? 0 : digit(encoded[i + 2]), d = pad3 ? 0 : digit(encoded[i + 3]);
        if ((pad2 && (b & 15)) || (pad3 && !pad2 && (c & 3)))
            throw std::runtime_error("Noncanonical base64 padding");
        const std::array<unsigned char, 3> part{static_cast<unsigned char>((a << 2) | (b >> 4)),
                                                static_cast<unsigned char>((b << 4) | (c >> 2)),
                                                static_cast<unsigned char>((c << 6) | d)};
        for (unsigned j = 0; j < (pad2 ? 1u : pad3 ? 2u : 3u); ++j)
            sink(bytes++, std::byte(part[j]));
    }
    return bytes;
}
inline std::vector<std::byte> decode_base64(std::string_view encoded, std::size_t limit) {
    std::vector<std::byte> result;
    // Admission happens before allocation, including for malicious wire lengths.
    if (encoded.size() / 4 > limit / 3 + 1)
        throw std::runtime_error("Decoded base64 exceeds its byte limit");
    result.reserve(std::min(limit, encoded.size() / 4 * 3));
    visit_base64(encoded, limit, [&](std::size_t, std::byte value) { result.push_back(value); });
    return result;
}
} // namespace forge
