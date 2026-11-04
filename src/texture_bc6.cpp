#include "texture_bc6.hpp"
#include <string_view>
namespace forge::asset_detail {
namespace {
struct Mode {
    unsigned code, precision;
    std::array<unsigned, 3> delta;
    std::array<std::uint8_t, 75> destinations{};
    unsigned count = 0;
};
// Numeric mode/partition facts follow the published BPTC format specification.
// The decoder and compact layout reader are independently authored FORGE code.
// https://registry.khronos.org/DataFormat/specs/1.4/dataformat.1.4.html#bptc_bc6h
consteval Mode layout(unsigned code, unsigned precision, std::array<unsigned, 3> delta,
                      std::string_view fields) {
    Mode result{code, precision, delta};
    std::array<bool, 192> assigned{};
    std::size_t cursor = 0;
    const auto number = [&] {
        unsigned value = 0, digits = 0;
        while (cursor < fields.size() && fields[cursor] >= '0' && fields[cursor] <= '9') {
            value = value * 10 + unsigned(fields[cursor++] - '0');
            ++digits;
        }
        if (!digits || value > 15)
            throw "Invalid BC6 field bit";
        return int(value);
    };
    while (cursor < fields.size()) {
        const char channel = fields[cursor++];
        if (channel != 'R' && channel != 'G' && channel != 'B')
            throw "Invalid BC6 channel";
        const unsigned component = channel == 'R' ? 0u : channel == 'G' ? 1u : 2u;
        const unsigned endpoint = unsigned(fields[cursor++] - '0');
        if (endpoint > 3 || fields[cursor++] != '.')
            throw "Invalid BC6 endpoint";
        const int first = number();
        int last = first;
        if (cursor < fields.size() && fields[cursor] == ':') {
            ++cursor;
            last = number();
        }
        for (int bit = first;; bit += first <= last ? 1 : -1) {
            if (result.count == result.destinations.size())
                throw "BC6 layout overflow";
            const unsigned destination = component * 64 + endpoint * 16 + unsigned(bit);
            if (assigned[destination])
                throw "Repeated BC6 endpoint bit";
            assigned[destination] = true;
            result.destinations[result.count++] = std::uint8_t(destination);
            if (bit == last)
                break;
        }
        if (cursor < fields.size() && fields[cursor++] != ' ')
            throw "Invalid BC6 separator";
    }
    const unsigned expected = code < 2 ? 75u : (code & 3) == 3 ? 60u : 72u;
    if (result.count != expected)
        throw "Incomplete BC6 layout";
    for (unsigned channel = 0; channel < 3; ++channel)
        for (unsigned endpoint = 0; endpoint < 4; ++endpoint) {
            const unsigned width = endpoint && delta[channel] ? delta[channel] : precision;
            const unsigned endpoints = (code & 3) == 3 ? 2u : 4u;
            for (unsigned bit = 0; bit < 16; ++bit)
                if (assigned[channel * 64 + endpoint * 16 + bit] !=
                    (endpoint < endpoints && bit < width))
                    throw "BC6 endpoint coverage differs from its precision";
        }
    return result;
}
constexpr std::array modes{
    layout(0, 10, {5, 5, 5},
           "G2.4 B2.4 B3.4 R0.0:9 G0.0:9 B0.0:9 R1.0:4 G3.4 G2.0:3 G1.0:4 B3.0 G3.0:3 B1.0:4 B3.1 "
           "B2.0:3 R2.0:4 B3.2 R3.0:4 B3.3"),
    layout(1, 7, {6, 6, 6},
           "G2.5 G3.4 G3.5 R0.0:6 B3.0 B3.1 B2.4 G0.0:6 B2.5 B3.2 G2.4 B0.0:6 B3.3 B3.5 B3.4 "
           "R1.0:5 G2.0:3 G1.0:5 G3.0:3 B1.0:5 B2.0:3 R2.0:5 R3.0:5"),
    layout(2, 11, {5, 4, 4},
           "R0.0:9 G0.0:9 B0.0:9 R1.0:4 R0.10 G2.0:3 G1.0:3 G0.10 B3.0 G3.0:3 B1.0:3 B0.10 B3.1 "
           "B2.0:3 R2.0:4 B3.2 R3.0:4 B3.3"),
    layout(6, 11, {4, 5, 4},
           "R0.0:9 G0.0:9 B0.0:9 R1.0:3 R0.10 G3.4 G2.0:3 G1.0:4 G0.10 G3.0:3 B1.0:3 B0.10 B3.1 "
           "B2.0:3 R2.0:3 B3.0 B3.2 R3.0:3 G2.4 B3.3"),
    layout(10, 11, {4, 4, 5},
           "R0.0:9 G0.0:9 B0.0:9 R1.0:3 R0.10 B2.4 G2.0:3 G1.0:3 G0.10 B3.0 G3.0:3 B1.0:4 B0.10 "
           "B2.0:3 R2.0:3 B3.1 B3.2 R3.0:3 B3.4 B3.3"),
    layout(14, 9, {5, 5, 5},
           "R0.0:8 B2.4 G0.0:8 G2.4 B0.0:8 B3.4 R1.0:4 G3.4 G2.0:3 G1.0:4 B3.0 G3.0:3 B1.0:4 B3.1 "
           "B2.0:3 R2.0:4 B3.2 R3.0:4 B3.3"),
    layout(18, 8, {6, 5, 5},
           "R0.0:7 G3.4 B2.4 G0.0:7 B3.2 G2.4 B0.0:7 B3.3 B3.4 R1.0:5 G2.0:3 G1.0:4 B3.0 G3.0:3 "
           "B1.0:4 B3.1 B2.0:3 R2.0:5 R3.0:5"),
    layout(22, 8, {5, 6, 5},
           "R0.0:7 B3.0 B2.4 G0.0:7 G2.5 G2.4 B0.0:7 G3.5 B3.4 R1.0:4 G3.4 G2.0:3 G1.0:5 G3.0:3 "
           "B1.0:4 B3.1 B2.0:3 R2.0:4 B3.2 R3.0:4 B3.3"),
    layout(26, 8, {5, 5, 6},
           "R0.0:7 B3.1 B2.4 G0.0:7 B2.5 G2.4 B0.0:7 B3.5 B3.4 R1.0:4 G3.4 G2.0:3 G1.0:4 B3.0 "
           "G3.0:3 B1.0:5 B2.0:3 R2.0:4 B3.2 R3.0:4 B3.3"),
    layout(30, 6, {0, 0, 0},
           "R0.0:5 G3.4 B3.0 B3.1 B2.4 G0.0:5 G2.5 B2.5 B3.2 G2.4 B0.0:5 G3.5 B3.3 B3.5 B3.4 "
           "R1.0:5 G2.0:3 G1.0:5 G3.0:3 B1.0:5 B2.0:3 R2.0:5 R3.0:5"),
    layout(3, 10, {0, 0, 0}, "R0.0:9 G0.0:9 B0.0:9 R1.0:9 G1.0:9 B1.0:9"),
    layout(7, 11, {9, 9, 9}, "R0.0:9 G0.0:9 B0.0:9 R1.0:8 R0.10 G1.0:8 G0.10 B1.0:8 B0.10"),
    layout(11, 12, {8, 8, 8},
           "R0.0:9 G0.0:9 B0.0:9 R1.0:7 R0.11:10 G1.0:7 G0.11:10 B1.0:7 B0.11:10"),
    layout(15, 16, {4, 4, 4},
           "R0.0:9 G0.0:9 B0.0:9 R1.0:3 R0.15:10 G1.0:3 G0.15:10 B1.0:3 B0.15:10"),
};
constexpr std::array<std::uint16_t, 32> partitions{
    0xcccc, 0x8888, 0xeeee, 0xecc8, 0xc880, 0xfeec, 0xfec8, 0xec80, 0xc800, 0xffec, 0xfe80,
    0xe800, 0xffe8, 0xff00, 0xfff0, 0xf000, 0xf710, 0x8e,   0x7100, 0x8ce,  0x8c,   0x7310,
    0x3100, 0x8cce, 0x88c,  0x3110, 0x6666, 0x366c, 0x17e8, 0xff0,  0x718e, 0x399c,
};
constexpr std::array<unsigned, 32> anchors{15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
                                           15, 15, 15, 15, 15, 15, 2,  8,  2,  2,  8,
                                           8,  15, 2,  8,  2,  2,  8,  8,  2,  2};
constexpr auto mode_indices = [] {
    std::array<int, 32> indices{};
    indices.fill(-1);
    for (std::size_t i = 0; i < modes.size(); ++i)
        indices[modes[i].code] = int(i);
    return indices;
}();
constexpr std::array<unsigned, 8> weights3{0, 9, 18, 27, 37, 46, 55, 64};
constexpr std::array<unsigned, 16> weights4{0,  4,  9,  13, 17, 21, 26, 30,
                                            34, 38, 43, 47, 51, 55, 60, 64};
int signed_word(unsigned value, unsigned width) noexcept {
    const unsigned sign = 1u << (width - 1);
    return int(value) - ((value & sign) ? int(sign * 2) : 0);
}
int expand_endpoint(int value, unsigned width, bool signed_samples) noexcept {
    if (width >= (signed_samples ? 16u : 15u))
        return value;
    const int magnitude = value < 0 ? -value : value;
    const int maximum = (1 << (width - unsigned(signed_samples))) - 1;
    const int full = signed_samples ? 32767 : 65535;
    const int expanded = !magnitude ? 0
                         : magnitude >= maximum
                             ? full
                             : int((std::int64_t(magnitude) * 32768 + 16384) / (1u << (width - 1)));
    return value < 0 ? -expanded : expanded;
}
std::uint16_t half_word(int value, bool signed_samples) noexcept {
    const unsigned magnitude = unsigned(value < 0 ? -value : value);
    const unsigned scaled = magnitude * 31 / (signed_samples ? 32u : 64u);
    return std::uint16_t(scaled | (value < 0 ? 0x8000u : 0u));
}
} // namespace
std::array<std::uint16_t, 48> decode_bc6_block(std::span<const std::byte, 16> block,
                                               bool signed_samples) noexcept {
    std::array<std::uint16_t, 48> result{};
    const unsigned first = std::to_integer<unsigned>(block[0]);
    const unsigned code = (first & 3) < 2 ? first & 3 : first & 31;
    const int selected = mode_indices[code];
    if (selected < 0)
        return result;
    const auto& mode = modes[std::size_t(selected)];
    const bool split = (code & 3) != 3, transformed = code != 3 && code != 30;
    unsigned cursor = code < 2 ? 2 : 5;
    const auto read = [&](unsigned count) {
        unsigned value = 0;
        for (unsigned bit = 0; bit < count; ++bit, ++cursor)
            value |= ((std::to_integer<unsigned>(block[cursor / 8]) >> (cursor % 8)) & 1u) << bit;
        return value;
    };
    std::array<std::array<unsigned, 4>, 3> stored{};
    for (unsigned i = 0; i < mode.count; ++i) {
        const unsigned destination = mode.destinations[i];
        stored[destination / 64][(destination / 16) % 4] |= read(1) << (destination % 16);
    }
    const unsigned partition = split ? read(5) : 0;
    std::array<std::array<int, 4>, 3> endpoints{};
    const unsigned mask = (1u << mode.precision) - 1;
    for (unsigned channel = 0; channel < 3; ++channel) {
        const int base = signed_samples ? signed_word(stored[channel][0], mode.precision)
                                        : int(stored[channel][0]);
        for (unsigned endpoint = 0; endpoint < (split ? 4u : 2u); ++endpoint) {
            int value = base;
            if (endpoint) {
                const unsigned width = transformed ? mode.delta[channel] : mode.precision;
                value = (transformed || signed_samples)
                            ? signed_word(stored[channel][endpoint], width)
                            : int(stored[channel][endpoint]);
                if (transformed) {
                    const unsigned wrapped = unsigned(base + value) & mask;
                    value = signed_samples ? signed_word(wrapped, mode.precision) : int(wrapped);
                }
            }
            endpoints[channel][endpoint] = expand_endpoint(value, mode.precision, signed_samples);
        }
    }
    for (unsigned pixel = 0; pixel < 16; ++pixel) {
        const bool anchor = pixel == 0 || (split && pixel == anchors[partition]);
        const unsigned index = read((split ? 3u : 4u) - unsigned(anchor));
        const unsigned weight = split ? weights3[index] : weights4[index];
        const unsigned subset = split ? (partitions[partition] >> pixel) & 1u : 0;
        for (unsigned channel = 0; channel < 3; ++channel) {
            const auto& values = endpoints[channel];
            const int sum =
                int(64 - weight) * values[subset * 2] + int(weight) * values[subset * 2 + 1] + 32;
            // Floor division keeps negative interpolation independent of signed shifts.
            const int interpolated = sum < 0 ? -((-sum + 63) / 64) : sum / 64;
            result[pixel * 3 + channel] = half_word(interpolated, signed_samples);
        }
    }
    return result;
}
} // namespace forge::asset_detail
