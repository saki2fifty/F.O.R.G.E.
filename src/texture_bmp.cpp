#include "texture_import.hpp"
#include <bit>
#include <climits>

namespace forge::asset_detail {
void validate_bmp_extent(std::span<const std::byte> bytes, unsigned limit) {
    auto require = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    auto integer = [&](std::size_t at, unsigned count) {
        require(at <= bytes.size() && count <= bytes.size() - at, "Truncated BMP header");
        unsigned n = 0;
        for (unsigned i = 0; i < count; ++i)
            n |= std::to_integer<unsigned>(bytes[at + i]) << (i * 8);
        return n;
    };
    require(bytes.size() <= INT_MAX && integer(0, 2) == 0x4d42, "Invalid BMP framing");
    const auto offset = integer(10, 4), header = integer(14, 4);
    require(header == 12 || header == 40 || header == 108 || header == 124,
            "BMP requires CORE, INFO, V4 or V5 header; other variants need conversion");
    require(bytes.size() >= 14 + header && offset <= bytes.size(), "Truncated BMP header/palette");
    const bool core = header == 12;
    const auto width = integer(18, core ? 2 : 4);
    const auto raw_height = integer(core ? 20 : 22, core ? 2 : 4);
    require(raw_height != 0x80000000u, "Invalid BMP signed height");
    const auto height = raw_height > INT_MAX ? (~raw_height + 1u) : raw_height;
    require(width && height && width <= limit && height <= limit, "BMP dimensions exceed bounds");
    require(integer(core ? 22 : 26, 2) == 1, "BMP requires one plane");
    const auto bits = integer(core ? 24 : 28, 2);
    const auto compression = core ? 0 : integer(30, 4);
    require(bits == 1 || bits == 4 || bits == 8 || bits == 16 || bits == 24 || bits == 32,
            "Unsupported BMP pixel depth");
    require(compression == 0 || (compression == 3 && (bits == 16 || bits == 32)),
            "BMP RLE, embedded codecs and unsupported compression require conversion");
    // Exact stb's CORE palette-size calculation does not match its 12-byte header.
    require(!core || bits == 24, "Only 24-bit CORE BMP is admitted by the pinned decoder");
    unsigned prefix = 14 + header;
    if (compression == 3) {
        if (header == 40)
            prefix += 12;
        unsigned occupied = 0;
        const unsigned masks = header >= 108 ? 4 : 3;
        for (unsigned i = 0; i < masks; ++i) {
            const unsigned mask = integer(54 + i * 4, 4);
            if (!mask) {
                require(i == 3, "BMP color mask cannot be zero");
                continue;
            }
            const auto shifted = mask >> std::countr_zero(mask);
            require((shifted & (shifted + 1u)) == 0 && std::popcount(mask) <= 8 &&
                        !(occupied & mask) && (bits == 32 || mask < (1u << bits)),
                    "BMP bitfields require disjoint contiguous channels of at most 8 bits");
            occupied |= mask;
        }
    }
    if (header >= 108) {
        require(integer(70, 4) == 0x73524742,
                "BMP color profile requires explicit conversion to sRGB");
        if (header == 124)
            require(integer(126, 4) == 0 && integer(130, 4) == 0,
                    "BMP embedded/linked profiles require explicit conversion");
    }
    require(offset >= prefix, "BMP pixels overlap header/masks");
    unsigned entries = 0;
    if (bits < 16) {
        entries = integer(46, 4);
        if (!entries)
            entries = 1u << bits;
        require(entries <= (1u << bits) && offset - prefix >= entries * 4 &&
                    offset - prefix <= 256 * 4 && (offset - prefix) % 4 == 0,
                "BMP palette size/offset disagree");
    } else {
        // Native stb skips a non-paletted header gap twice. Do not misdecode it.
        require(offset == prefix, "BMP header gap requires conversion for the pinned decoder");
    }
    const auto row = ((std::uint64_t(width) * bits + 31) / 32) * 4;
    require(row <= (bytes.size() - offset) / height, "Truncated BMP pixel rows");
    const auto file_size = integer(2, 4);
    require(!file_size || (file_size <= bytes.size() && file_size >= offset + row * height),
            "BMP declared file size excludes pixels");
    if (entries)
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x) {
                const auto packed =
                    std::to_integer<unsigned>(bytes[offset + y * row + x * bits / 8]);
                const auto index = (packed >> (8 - bits - ((x * bits) % 8))) & ((1u << bits) - 1);
                require(index < entries, "BMP pixel references a missing palette entry");
            }
}
} // namespace forge::asset_detail
