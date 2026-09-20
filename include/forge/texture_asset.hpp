#pragma once
#include <forge/asset_ref.hpp>
#include <span>
#include <vector>

namespace forge {
struct TextureAsset {
    static constexpr const char* type = "texture";
};
enum class TextureDimension { D2, D2Array, Cube, CubeArray, D3 };
enum class TextureFormat {
    R8,
    RG8,
    RGBA8,
    RGBA8Srgb,
    R16,
    RG16,
    RGBA16,
    R16Float,
    RG16Float,
    RGBA16Float,
    R32Float,
    RG32Float,
    RGBA32Float,
    BC1,
    BC1Srgb,
    BC2,
    BC2Srgb,
    BC3,
    BC3Srgb,
    BC4,
    BC4Snorm,
    BC5,
    BC5Snorm,
    BC6Unsigned,
    BC6Signed,
    BC7,
    BC7Srgb
};
enum class TextureSemantic { Color, Data, Normal, HdrColor };
enum class TextureAlpha { Opaque, Straight, Premultiplied, Unknown };
enum class TextureFilter { Nearest, Linear };
enum class TextureWrap { Repeat, MirroredRepeat, ClampEdge, ClampBorder };
enum class TextureCompare {
    None,
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always
};
struct SamplerState {
    TextureFilter min = TextureFilter::Linear, mag = TextureFilter::Linear,
                  mip = TextureFilter::Linear;
    TextureWrap u = TextureWrap::Repeat, v = TextureWrap::Repeat, w = TextureWrap::Repeat;
    TextureCompare compare = TextureCompare::None;
    unsigned anisotropy = 1;
    float lod_bias = 0, min_lod = 0, max_lod = 1000;
    std::array<float, 4> border{};
    auto operator<=>(const SamplerState&) const = default;
};
struct TextureFormatInfo {
    unsigned block_width = 1, block_height = 1, block_bytes = 0, channels = 0, float_bits = 0;
    bool srgb = false, compressed = false, signed_values = false;
};
struct TextureLayout {
    unsigned width = 0, height = 0, depth = 0;
    std::size_t row_bytes = 0, slice_bytes = 0, bytes = 0;
};
struct TextureData {
    TextureDimension dimension = TextureDimension::D2;
    TextureFormat format = TextureFormat::RGBA8;
    TextureSemantic semantic = TextureSemantic::Color;
    TextureAlpha alpha = TextureAlpha::Unknown;
    unsigned width = 0, height = 0, depth = 1, layers = 1, mips = 1;
    // Array element, cube face (+X,-X,+Y,-Y,+Z,-Z), mip, then tightly packed
    // depth slices. Rows begin at the image top; normal maps use +Y tangent space.
    // Float scalars are little endian. No GPU upload padding in cooked payloads.
    std::vector<std::vector<std::byte>> subresources;
    SamplerState sampler;
    std::size_t byte_size() const;
    std::size_t resident_bytes() const;
};
struct TextureLimits {
    std::size_t bytes = 512 * 1024 * 1024;
    unsigned dimension = 16384, depth = 2048, layers = 2048, subresources = 32768;
};
TextureFormatInfo texture_format_info(TextureFormat format);
TextureLayout texture_layout(const TextureData& texture, unsigned mip);
void validate_sampler(const SamplerState& sampler);
// Validates dimensions, semantics and the complete storage budget before payload allocation.
void validate_texture_metadata(const TextureData& texture, TextureLimits limits = {});
void validate_texture(const TextureData& texture, TextureLimits limits = {});
std::vector<std::byte> encode_texture(const TextureData& texture, TextureLimits limits = {});
TextureData decode_texture(std::span<const std::byte> bytes, TextureLimits limits = {});
} // namespace forge
