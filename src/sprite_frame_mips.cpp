#include "sprite_frame_mips.hpp"
#include "Common/interface/Float16.hpp"
#include "Graphics/GraphicsAccessories/interface/ColorConversion.h"
#include "Graphics/GraphicsTools/interface/GraphicsUtilities.h"

namespace forge {
TextureData prepare_sprite_frame_mips(const TextureData& source, const SpriteFrameMipKey& key,
                                      std::uint64_t byte_limit, std::stop_token stop) {
    const auto expected = sprite_frame_mip_bytes(key, byte_limit);
    auto base = sprite_frame_mip_base(source, key, byte_limit, stop);
    const bool srgb = base.format == TextureFormat::RGBA8Srgb;
    auto width = base.width, height = base.height;
    TextureData result;
    result.width = width;
    result.height = height;
    result.format = TextureFormat::RGBA16Float;
    result.alpha = TextureAlpha::Premultiplied;
    result.sampler = base.sampler;
    result.mips = 0;
    std::vector<float> current(std::size_t(width) * height * 4);
    const auto cancelled = [&] {
        if (stop.stop_requested())
            throw std::runtime_error("Sprite frame mip preparation cancelled");
    };
    for (unsigned y = 0; y < height; ++y) {
        cancelled();
        for (unsigned x = 0; x < width; ++x) {
            const auto at = (std::size_t(y) * width + x) * 4;
            const auto* pixel = base.subresources.front().data() + at;
            const auto alpha = std::to_integer<unsigned>(pixel[3]) / 255.f;
            Diligent::float3 color{std::to_integer<unsigned>(pixel[0]) / 255.f,
                                   std::to_integer<unsigned>(pixel[1]) / 255.f,
                                   std::to_integer<unsigned>(pixel[2]) / 255.f};
            if (srgb)
                color = Diligent::SRGBToLinear(color);
            current[at] = color.r * alpha;
            current[at + 1] = color.g * alpha;
            current[at + 2] = color.b * alpha;
            current[at + 3] = alpha;
        }
    }
    base = {}; // Release reconstructed 8-bit pixels before native working levels.
    for (;;) {
        cancelled();
        auto& packed = result.subresources.emplace_back(current.size() * 2);
        for (unsigned y = 0; y < height; ++y) {
            cancelled();
            const auto start = std::size_t(y) * width * 4;
            for (std::size_t at = start; at < start + std::size_t(width) * 4; ++at) {
                const auto bits = Diligent::Float16{current[at]}.Raw();
                packed[at * 2] = std::byte(bits & 255);
                packed[at * 2 + 1] = std::byte(bits >> 8);
            }
        }
        ++result.mips;
        if (width == 1 && height == 1)
            break;
        const auto next_width = std::max(1u, width / 2);
        const auto next_height = std::max(1u, height / 2);
        std::vector<float> next(std::size_t(next_width) * next_height * 4);
        Diligent::ComputeMipLevelAttribs attributes;
        // This pin accepts FLOAT32 only. Never pass packed half-float output.
        attributes.Format = Diligent::TEX_FORMAT_RGBA32_FLOAT;
        attributes.FineMipWidth = width;
        attributes.FineMipHeight = height;
        attributes.pFineMipData = current.data();
        attributes.FineMipStride = std::size_t(width) * 4 * sizeof(float);
        attributes.pCoarseMipData = next.data();
        attributes.CoarseMipStride = std::size_t(next_width) * 4 * sizeof(float);
        attributes.FilterType = Diligent::MIP_FILTER_TYPE_BOX_AVERAGE;
        Diligent::ComputeMipLevel(attributes);
        cancelled();
        current = std::move(next);
        width = next_width;
        height = next_height;
    }
    if (result.byte_size() != expected)
        throw std::runtime_error("Sprite frame mip payload differs from its admitted plan");
    validate_texture(result);
    return result;
}
} // namespace forge
