#pragma once
#include <cstdint>
#include <forge/render_view.hpp>
#include <stdexcept>
namespace forge {
inline constexpr std::uint64_t hdr_bloom_payload_limit = 128 * 1024 * 1024;
struct HdrBloomPlan {
    unsigned width = 0, height = 0, half_width = 0, half_height = 0;
    std::uint64_t bytes = 0;
};
inline HdrBloomPlan hdr_bloom_plan(unsigned source_width, unsigned source_height,
                                   PixelViewport area, unsigned device_maximum,
                                   std::uint64_t budget = hdr_bloom_payload_limit) {
    if (!area.width || !area.height || area.x >= source_width || area.y >= source_height ||
        area.width > source_width - area.x || area.height > source_height - area.y ||
        source_width > device_maximum || source_height > device_maximum)
        throw std::runtime_error("Bloom requires an admitted HDR camera rectangle");
    HdrBloomPlan result{area.width, area.height, area.width / 2 + area.width % 2,
                        area.height / 2 + area.height % 2, 0};
    auto admit = [&](unsigned width, unsigned height) {
        if (std::uint64_t(width) > (budget - result.bytes) / 8 / height)
            throw std::runtime_error("Bloom exceeds its128MiB texture payload budget");
        result.bytes += std::uint64_t(width) * height * 8;
    };
    admit(result.width, result.height);
    admit(result.half_width, result.half_height);
    admit(result.half_width, result.half_height);
    return result;
}
} // namespace forge
