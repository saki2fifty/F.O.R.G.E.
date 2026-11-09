#pragma once
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <stdexcept>
#include <vector>

namespace forge::test {
inline std::uint32_t font_u32(std::span<const std::byte> data, std::size_t at) {
    if (at > data.size() || data.size() - at < 4)
        throw std::runtime_error("Truncated font fixture word");
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value = (value << 8) | std::to_integer<unsigned>(data[at + i]);
    return value;
}
inline void font_u32(std::vector<std::byte>& data, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        data.at(at + i) = std::byte((value >> (24 - 8 * i)) & 255);
}
// Original test container writer, using the documented TTC header and absolute
// sfnt table offsets. Font tables remain the supplied fonts' original data.
inline std::vector<std::byte>
font_collection(std::initializer_list<std::span<const std::byte>> fonts, bool version2 = false) {
    if (fonts.size() == 0 || fonts.size() > 32)
        throw std::runtime_error("Use 1..32 fixture fonts");
    std::vector<std::byte> result(12 + 4 * fonts.size() + (version2 ? 12 : 0));
    font_u32(result, 0, 0x74746366);
    font_u32(result, 4, version2 ? 0x00020000 : 0x00010000);
    font_u32(result, 8, std::uint32_t(fonts.size()));
    unsigned index = 0;
    for (const auto source : fonts) {
        if (source.size() < 12 ||
            (font_u32(source, 0) != 0x00010000 && font_u32(source, 0) != 0x4f54544f))
            throw std::runtime_error("Fixture needs standalone sfnt fonts");
        while (result.size() % 4)
            result.push_back(std::byte{});
        const auto offset = result.size();
        font_u32(result, 12 + 4 * index++, std::uint32_t(offset));
        result.insert(result.end(), source.begin(), source.end());
        const auto count = font_u32(source, 4) >> 16;
        for (unsigned table = 0; table < count; ++table) {
            const auto entry = 12 + table * 16;
            const auto original = font_u32(source, entry + 8);
            font_u32(result, offset + entry + 8, std::uint32_t(offset + original));
            if (font_u32(source, entry) == 0x68656164 && font_u32(source, entry + 12) >= 12)
                font_u32(result, offset + original + 8, 0);
        }
    }
    return result;
}
} // namespace forge::test
