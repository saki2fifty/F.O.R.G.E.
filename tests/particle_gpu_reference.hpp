#pragma once
#include <cstdint>
namespace forge::particle_gpu_test {
struct Random {
    std::uint32_t state;
    Random(std::uint32_t seed, std::uint64_t ordinal) {
        std::uint32_t hash = 2166136261u;
        for (unsigned byte = 0; byte < 12; ++byte) {
            const auto value = byte < 4 ? std::uint8_t(seed >> (byte * 8))
                                        : std::uint8_t(ordinal >> ((byte - 4) * 8));
            hash = (hash ^ value) * 16777619u;
        }
        state = hash % 2147483646u + 1u;
    }
    double unit() {
        state = std::uint32_t(std::uint64_t(state) * 48271u % 2147483647u);
        return double(state >> 7) / 16777216.;
    }
};
} // namespace forge::particle_gpu_test
