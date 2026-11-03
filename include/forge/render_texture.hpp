#pragma once
#include <cstddef>
#include <cstdint>
#include <forge/asset_ref.hpp>
#include <span>
#include <vector>

namespace forge {
inline constexpr std::size_t render_texture_source_byte_limit = 16 * 1024;
inline constexpr std::uint32_t render_texture_max_dimension = 2048;
struct RenderTextureData {
    std::uint32_t width = 512, height = 512;
    // The initial profile is a single-layer linear RGBA16_FLOAT color image.
    // Alpha is opaque; display exposure and tone mapping belong to the consumer.
    std::size_t resident_bytes() const { return sizeof(*this); }
    auto operator<=>(const RenderTextureData&) const = default;
};
void validate_render_texture(const RenderTextureData&);
struct RenderTextureSource {
    nlohmann::json document;
    static RenderTextureSource create(AssetId);
    static RenderTextureSource parse(std::span<const std::byte>);
    AssetId asset() const;
    void validate() const;
    RenderTextureData compiled() const;
    std::vector<std::byte> cook() const;
};
} // namespace forge
