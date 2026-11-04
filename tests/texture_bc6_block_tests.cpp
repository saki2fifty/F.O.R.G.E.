#include "texture_bc6.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace forge::asset_detail;
namespace {
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
struct Bits {
    std::array<std::byte, 16> block{};
    unsigned cursor = 0;
    void put(unsigned value, unsigned count) {
        check(cursor + count <= 128, "Independent fixture overflow");
        for (unsigned i = 0; i < count; ++i, ++cursor)
            block[cursor / 8] |= std::byte(((value >> i) & 1u) << (cursor % 8));
    }
};
// Independently assemble the four single-subset formats; no decoder layout data.
std::array<std::byte, 16> single(unsigned code, std::array<unsigned, 3> base,
                                 std::array<unsigned, 3> second, unsigned index = 0) {
    Bits bits;
    bits.put(code, 5);
    for (auto value : base)
        bits.put(value, 10);
    const unsigned precision = code == 3 ? 10u : code == 7 ? 11u : code == 11 ? 12u : 16u;
    const unsigned delta = code == 3 ? 10u : code == 7 ? 9u : code == 11 ? 8u : 4u;
    for (unsigned c = 0; c < 3; ++c) {
        bits.put(second[c], delta);
        for (unsigned bit = precision; bit > 10; --bit)
            bits.put(base[c] >> (bit - 1), 1);
    }
    check(bits.cursor == 65, "Independent endpoint fixture has wrong length");
    bits.put(index, 3);
    for (unsigned pixel = 1; pixel < 16; ++pixel)
        bits.put(index, 4);
    check(bits.cursor == 128, "Independent index fixture has wrong length");
    return bits.block;
}
void constant(unsigned code, unsigned value, bool sign, std::uint16_t expected) {
    const auto block = single(code, {value, value, value},
                              code == 3 ? std::array{value, value, value} : std::array{0u, 0u, 0u});
    const auto actual = decode_bc6_block(block, sign);
    check(std::all_of(actual.begin(), actual.end(), [&](auto x) { return x == expected; }),
          "Single-subset constant differs from independent numeric vector");
}
void basic_vectors() {
    for (bool sign : {false, true}) {
        for (unsigned code : {0u, 1u, 2u, 6u, 10u, 14u, 18u, 22u, 26u, 30u, 3u, 7u, 11u, 15u}) {
            std::array<std::byte, 16> block{};
            block[0] = std::byte(code);
            const auto actual = decode_bc6_block(block, sign);
            check(std::all_of(actual.begin(), actual.end(), [](auto x) { return x == 0; }),
                  "Zero endpoint vector did not decode to black");
        }
        for (unsigned code : {19u, 23u, 27u, 31u}) {
            std::array<std::byte, 16> block;
            block.fill(std::byte(255));
            block[0] = std::byte(224 | code);
            const auto actual = decode_bc6_block(block, sign);
            check(std::all_of(actual.begin(), actual.end(), [](auto x) { return x == 0; }),
                  "Reserved mode did not return standard black");
        }
    }
    for (unsigned code : {3u, 7u, 11u, 15u}) {
        const unsigned precision = code == 3 ? 10u : code == 7 ? 11u : code == 11 ? 12u : 16u;
        constant(code, 0, false, 0);
        constant(code, (1u << precision) - 1, false, 0x7bff);
        constant(code, (1u << (precision - 1)) - 1, true, 0x7bff);
        constant(code, 1u << (precision - 1), true, code == 15 ? 0xfc00 : 0xfbff);
    }
    constant(3, 512, false, 0x3e0f);
    constant(7, 1024, false, 0x3e07);
    constant(11, 2048, false, 0x3e03);
    constant(15, 32768, false, 0x3e00);
    constant(15, 65535, true, 0x8000);
    auto wrapped = single(7, {0, 0, 0}, {511, 511, 511}, 15);
    check(decode_bc6_block(wrapped, false)[45] == 0x7bff,
          "Unsigned transformed endpoint did not wrap at its base width");
    check(decode_bc6_block(wrapped, true)[45] == 0x802e,
          "Signed transformed endpoint lost its negative delta");
    const auto channels = decode_bc6_block(single(3, {0, 1023, 512}, {0, 1023, 512}), false);
    check(channels[0] == 0 && channels[1] == 0x7bff && channels[2] == 0x3e0f,
          "Endpoint channel order changed");
}
} // namespace
int main() {
    try {
        basic_vectors();
        std::cout << "BC6 core zero/reserved and independent single-subset numeric vectors PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
