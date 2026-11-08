#pragma once
#include "audio_asset.hpp"
#include <bit>
#include <string_view>

namespace forge::test {
using asset_detail::AudioClipData;
inline std::vector<std::byte> float_wav(const AudioClipData& clip) {
    std::vector<std::byte> bytes;
    auto put = [&](std::uint32_t value, unsigned count) {
        for (unsigned i = 0; i < count; ++i)
            bytes.push_back(std::byte(value >> (i * 8)));
    };
    auto text = [&](std::string_view word) {
        for (char c : word)
            bytes.push_back(std::byte(c));
    };
    text("RIFF");
    put(36 + clip.pcm.size() * 4, 4);
    text("WAVEfmt ");
    put(16, 4);
    put(3, 2);
    put(clip.channels, 2);
    put(clip.rate, 4);
    put(clip.rate * clip.channels * 4, 4);
    put(clip.channels * 4, 2);
    put(32, 2);
    text("data");
    put(clip.pcm.size() * 4, 4);
    for (float sample : clip.pcm)
        put(std::bit_cast<std::uint32_t>(sample), 4);
    return bytes;
}
} // namespace forge::test
