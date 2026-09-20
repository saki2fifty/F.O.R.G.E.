#include "texture_import.hpp"
#include <charconv>

namespace forge::asset_detail {
void validate_hdr_extent(std::span<const std::byte> bytes, unsigned limit) {
    auto require = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    std::size_t at = 0;
    auto line = [&]() {
        const auto begin = at;
        while (at < bytes.size() && bytes[at] != std::byte{'\n'}) {
            require(bytes[at] != std::byte{}, "NUL in RGBE header");
            require(at - begin < 1023 && at < 65536,
                    "RGBE header exceeds native line/total bounds");
            ++at;
        }
        require(at < bytes.size(), "Truncated RGBE header line");
        const std::string_view text(reinterpret_cast<const char*>(bytes.data() + begin),
                                    at - begin);
        ++at;
        return text;
    };
    const auto signature = line();
    require(signature == "#?RADIANCE" || signature == "#?RGBE", "Unsupported RGBE signature");
    bool format = false;
    for (;;) {
        const auto text = line();
        if (text.empty())
            break;
        if (text.starts_with("FORMAT=")) {
            require(text == "FORMAT=32-bit_rle_rgbe", "Unsupported RGBE encoding");
            format = true;
        }
    }
    require(format, "Missing RGBE encoding declaration");
    auto resolution = line();
    auto dimension = [&](std::string_view axis) {
        require(resolution.starts_with(axis), "RGBE requires -Y/+X orientation");
        resolution.remove_prefix(axis.size());
        unsigned value = 0;
        const auto parsed =
            std::from_chars(resolution.data(), resolution.data() + resolution.size(), value);
        require(parsed.ec == std::errc{} && value && value <= limit, "Invalid RGBE dimensions");
        resolution.remove_prefix(parsed.ptr - resolution.data());
        while (resolution.starts_with(' '))
            resolution.remove_prefix(1);
        return value;
    };
    const auto height = dimension("-Y "), width = dimension("+X ");
    require(resolution.empty(), "Invalid RGBE resolution suffix");
    auto consume = [&](std::size_t n) {
        require(n <= bytes.size() - at, "Truncated RGBE pixels/packet");
        const auto start = at;
        at += n;
        return start;
    };
    auto get = [&](std::size_t index) { return std::to_integer<unsigned>(bytes[index]); };
    require(bytes.size() - at >= 4, "Missing RGBE pixels");
    const bool rle =
        width >= 8 && width < 32768 && get(at) == 2 && get(at + 1) == 2 && !(get(at + 2) & 128);
    if (!rle) {
        const auto start = consume(std::size_t(width) * height * 4);
        for (auto p = start; p < at; p += 4)
            require(!(get(p) == 1 && get(p + 1) == 1 && get(p + 2) == 1),
                    "Legacy RGBE repeat encoding requires conversion");
    } else {
        for (unsigned row = 0; row < height; ++row) {
            const auto start = consume(4);
            require(get(start) == 2 && get(start + 1) == 2 &&
                        (get(start + 2) * 256 + get(start + 3)) == width,
                    "RGBE scanline encoding/width changed");
            for (unsigned channel = 0; channel < 4; ++channel) {
                unsigned remaining = width;
                while (remaining) {
                    const auto code = get(consume(1));
                    const auto count = code > 128 ? code - 128 : code;
                    require(count && count <= remaining, "RGBE run exceeds scanline");
                    consume(code > 128 ? 1 : count);
                    remaining -= count;
                }
            }
        }
    }
    require(at == bytes.size(), "Trailing bytes after RGBE pixels");
}
} // namespace forge::asset_detail
