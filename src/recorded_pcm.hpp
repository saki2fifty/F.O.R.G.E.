#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace forge {
inline void append_recorded_pcm(std::vector<std::byte>& destination, std::span<const float> pcm) {
    for (const auto value : pcm) {
        if (!std::isfinite(value))
            throw std::runtime_error("Game recording mixer produced nonfinite PCM");
        const auto integer =
            std::clamp(std::lround(std::clamp(value, -1.f, 1.f) * 32768.f), -32768l, 32767l);
        const auto bits = std::uint16_t(std::int16_t(integer));
        destination.push_back(std::byte(bits & 255));
        destination.push_back(std::byte(bits >> 8));
    }
}
} // namespace forge
