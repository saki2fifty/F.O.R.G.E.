#include "cooked_envelope.hpp"
#include <bit>
#include <cmath>
#include <forge/texture_asset.hpp>
#include <limits>
namespace forge {
namespace {
using Json = nlohmann::json;
constexpr std::array<std::byte, 8> magic{std::byte{'F'}, std::byte{'R'}, std::byte{'G'},
                                         std::byte{'T'}, std::byte{'E'}, std::byte{'X'},
                                         std::byte{0},   std::byte{0}};
constexpr std::size_t metadata_limit = 4 * 1024 * 1024;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
std::size_t multiply(std::size_t a, std::size_t b) {
    require(!b || a <= SIZE_MAX / b, "Texture size overflow");
    return a * b;
}
void add(std::size_t& a, std::size_t b) {
    require(a <= SIZE_MAX - b, "Texture size overflow");
    a += b;
}
std::size_t number(const Json& j, std::size_t max) {
    require(j.is_number_integer() && (j.is_number_unsigned() || j.get<std::int64_t>() >= 0),
            "Texture count must be nonnegative integer");
    const auto n = j.get<std::uint64_t>();
    require(n <= max, "Texture count exceeds limit");
    return static_cast<std::size_t>(n);
}
float scalar(const Json& j) {
    require(j.is_number(), "Invalid sampler scalar");
    const auto x = j.get<double>();
    require(std::isfinite(x) && std::abs(x) <= std::numeric_limits<float>::max(),
            "Invalid sampler scalar");
    const auto f = static_cast<float>(x);
    require(x == 0 || f != 0, "Sampler scalar underflow");
    return f == 0 ? 0 : f;
}
bool cube(TextureDimension d) {
    return d == TextureDimension::Cube || d == TextureDimension::CubeArray;
}
unsigned surfaces(const TextureData& t) {
    require(t.layers <= UINT32_MAX / 6, "Texture layer overflow");
    return t.layers * (cube(t.dimension) ? 6 : 1);
}
void validate_header(const TextureData& t, TextureLimits l) {
    require(unsigned(t.dimension) <= unsigned(TextureDimension::D3) &&
                unsigned(t.semantic) <= unsigned(TextureSemantic::HdrColor) &&
                unsigned(t.alpha) <= unsigned(TextureAlpha::Custom),
            "Unknown texture metadata enum");
    const auto f = texture_format_info(t.format);
    require(t.width && t.height && t.depth && t.layers && t.mips && t.width <= l.dimension &&
                t.height <= l.dimension && t.depth <= l.depth && t.layers <= l.layers,
            "Texture dimensions exceed bounds");
    require(t.mips <= std::bit_width(std::max({t.width, t.height, t.depth})),
            "Too many texture mips");
    require(t.dimension == TextureDimension::D3 || t.depth == 1, "Only volume textures have depth");
    require(t.dimension == TextureDimension::D2Array ||
                t.dimension == TextureDimension::CubeArray || t.layers == 1,
            "Only array textures have layers");
    require(!cube(t.dimension) || t.width == t.height, "Cubemap faces must be square");
    require(!f.srgb || t.semantic == TextureSemantic::Color,
            "Data/normal/HDR textures cannot use sRGB storage");
    require(t.semantic != TextureSemantic::Normal || f.channels >= 2,
            "Normal textures require at least two linear channels");
    require(t.semantic != TextureSemantic::HdrColor || f.float_bits ||
                t.format == TextureFormat::BC6Unsigned || t.format == TextureFormat::BC6Signed,
            "HDR texture needs floating-point storage");
    const auto count = multiply(surfaces(t), t.mips);
    require(count <= l.subresources, "Texture subresource limit exceeded");
    std::size_t bytes = 0;
    for (unsigned mip = 0; mip < t.mips; ++mip)
        add(bytes, multiply(texture_layout(t, mip).bytes, surfaces(t)));
    require(bytes <= l.bytes, "Texture payload exceeds budget");
    validate_sampler(t.sampler);
}
} // namespace
TextureFormatInfo texture_format_info(TextureFormat format) {
    using F = TextureFormat;
    switch (format) {
    case F::R8:
        return {1, 1, 1, 1};
    case F::RG8:
        return {1, 1, 2, 2};
    case F::RGBA8:
        return {1, 1, 4, 4};
    case F::RGBA8Srgb:
        return {1, 1, 4, 4, 0, true};
    case F::R16:
        return {1, 1, 2, 1};
    case F::RG16:
        return {1, 1, 4, 2};
    case F::RGBA16:
        return {1, 1, 8, 4};
    case F::R16Float:
        return {1, 1, 2, 1, 16};
    case F::RG16Float:
        return {1, 1, 4, 2, 16};
    case F::RGBA16Float:
        return {1, 1, 8, 4, 16};
    case F::R32Float:
        return {1, 1, 4, 1, 32};
    case F::RG32Float:
        return {1, 1, 8, 2, 32};
    case F::RGBA32Float:
        return {1, 1, 16, 4, 32};
    case F::BC1:
    case F::BC1Srgb:
        return {4, 4, 8, 4, 0, format == F::BC1Srgb, true};
    case F::BC2:
    case F::BC2Srgb:
        return {4, 4, 16, 4, 0, format == F::BC2Srgb, true};
    case F::BC3:
    case F::BC3Srgb:
        return {4, 4, 16, 4, 0, format == F::BC3Srgb, true};
    case F::BC4:
    case F::BC4Snorm:
        return {4, 4, 8, 1, 0, false, true, format == F::BC4Snorm};
    case F::BC5:
    case F::BC5Snorm:
        return {4, 4, 16, 2, 0, false, true, format == F::BC5Snorm};
    case F::BC6Unsigned:
    case F::BC6Signed:
        return {4, 4, 16, 3, 0, false, true, format == F::BC6Signed};
    case F::BC7:
    case F::BC7Srgb:
        return {4, 4, 16, 4, 0, format == F::BC7Srgb, true};
    }
    throw std::runtime_error("Unsupported texture format");
}
TextureLayout texture_layout(const TextureData& t, unsigned mip) {
    require(mip < t.mips && mip < 32 && t.width && t.height && t.depth, "Invalid texture mip");
    const auto f = texture_format_info(t.format);
    TextureLayout r{std::max(1u, t.width >> mip), std::max(1u, t.height >> mip),
                    std::max(1u, t.depth >> mip)};
    // Quotient/remainder avoids overflow from width + block_width - 1.
    r.row_bytes = multiply(r.width / f.block_width + (r.width % f.block_width != 0), f.block_bytes);
    r.slice_bytes =
        multiply(r.row_bytes, r.height / f.block_height + (r.height % f.block_height != 0));
    r.bytes = multiply(r.slice_bytes, r.depth);
    return r;
}
void validate_sampler(const SamplerState& s) {
    require(unsigned(s.min) <= 1 && unsigned(s.mag) <= 1 && unsigned(s.mip) <= 1 &&
                unsigned(s.u) <= 3 && unsigned(s.v) <= 3 && unsigned(s.w) <= 3 &&
                unsigned(s.compare) <= 8,
            "Invalid sampler enum");
    require(s.anisotropy >= 1 && std::isfinite(s.lod_bias) && std::isfinite(s.min_lod) &&
                std::isfinite(s.max_lod) && s.max_lod >= s.min_lod,
            "Invalid sampler range");
    for (float x : s.border)
        require(std::isfinite(x), "Invalid sampler border color");
    require(s.anisotropy == 1 || (s.min == TextureFilter::Linear &&
                                  s.mag == TextureFilter::Linear && s.mip == TextureFilter::Linear),
            "Anisotropy requires linear filters");
}
void to_json(nlohmann::json& j, const SamplerState& s) {
    validate_sampler(s);
    j = {{"min", unsigned(s.min)},
         {"mag", unsigned(s.mag)},
         {"mip", unsigned(s.mip)},
         {"u", unsigned(s.u)},
         {"v", unsigned(s.v)},
         {"w", unsigned(s.w)},
         {"compare", unsigned(s.compare)},
         {"anisotropy", s.anisotropy},
         {"lod_bias", s.lod_bias},
         {"min_lod", s.min_lod},
         {"max_lod", s.max_lod},
         {"border", s.border}};
}
void from_json(const nlohmann::json& sj, SamplerState& result) {
    SamplerState s;
    s.min = TextureFilter(number(sj.at("min"), 1));
    s.mag = TextureFilter(number(sj.at("mag"), 1));
    s.mip = TextureFilter(number(sj.at("mip"), 1));
    s.u = TextureWrap(number(sj.at("u"), 3));
    s.v = TextureWrap(number(sj.at("v"), 3));
    s.w = TextureWrap(number(sj.at("w"), 3));
    s.compare = TextureCompare(number(sj.at("compare"), 8));
    s.anisotropy = unsigned(number(sj.at("anisotropy"), std::numeric_limits<unsigned>::max()));
    s.lod_bias = scalar(sj.at("lod_bias"));
    s.min_lod = scalar(sj.at("min_lod"));
    s.max_lod = scalar(sj.at("max_lod"));
    const auto& border = sj.at("border");
    require(border.is_array() && border.size() == 4, "Invalid sampler border shape");
    for (unsigned i = 0; i < 4; ++i)
        s.border[i] = scalar(border[i]);
    validate_sampler(s);
    result = s;
}
std::size_t TextureData::byte_size() const {
    std::size_t n = 0;
    for (const auto& s : subresources)
        add(n, s.size());
    return n;
}
std::size_t TextureData::resident_bytes() const {
    std::size_t n = sizeof(*this);
    add(n, multiply(subresources.capacity(), sizeof(subresources[0])));
    for (const auto& s : subresources)
        add(n, s.capacity());
    return n;
}
void validate_texture_metadata(const TextureData& t, TextureLimits l) { validate_header(t, l); }
void validate_texture(const TextureData& t, TextureLimits l) {
    validate_header(t, l);
    require(t.subresources.size() == multiply(surfaces(t), t.mips),
            "Texture subresource count mismatch");
    const auto f = texture_format_info(t.format);
    for (std::size_t i = 0; i < t.subresources.size(); ++i) {
        const auto& bytes = t.subresources[i];
        require(bytes.size() == texture_layout(t, unsigned(i % t.mips)).bytes,
                "Texture mip byte count mismatch");
        if (f.float_bits == 32) {
            std::size_t p = 0;
            while (p < bytes.size())
                require(std::isfinite(std::bit_cast<float>(asset_detail::cooked_read32(bytes, p))),
                        "Nonfinite float texture pixel");
        } else if (f.float_bits == 16)
            for (std::size_t p = 0; p < bytes.size(); p += 2) {
                const auto bits = std::to_integer<unsigned>(bytes[p]) |
                                  (std::to_integer<unsigned>(bytes[p + 1]) << 8);
                require((bits & 0x7c00) != 0x7c00, "Nonfinite half texture pixel");
            }
    }
}
std::vector<std::byte> encode_texture(const TextureData& t, TextureLimits l) {
    validate_texture(t, l);
    Json metadata{{"dimension", unsigned(t.dimension)},
                  {"format", unsigned(t.format)},
                  {"semantic", unsigned(t.semantic)},
                  {"alpha", unsigned(t.alpha)},
                  {"width", t.width},
                  {"height", t.height},
                  {"depth", t.depth},
                  {"layers", t.layers},
                  {"mips", t.mips},
                  {"sampler", t.sampler}};
    std::vector<std::byte> payload;
    payload.reserve(t.byte_size());
    for (const auto& bytes : t.subresources)
        payload.insert(payload.end(), bytes.begin(), bytes.end());
    return asset_detail::encode_envelope(metadata, payload, magic, metadata_limit);
}
TextureData decode_texture(std::span<const std::byte> bytes, TextureLimits l) {
    const auto envelope = asset_detail::decode_envelope(bytes, magic, metadata_limit, l.bytes);
    const auto& j = envelope.metadata;
    TextureData t;
    t.dimension = TextureDimension(number(j.at("dimension"), 4));
    t.format = TextureFormat(number(j.at("format"), 26));
    t.semantic = TextureSemantic(number(j.at("semantic"), 3));
    t.alpha = TextureAlpha(number(j.at("alpha"), unsigned(TextureAlpha::Custom)));
    t.width = unsigned(number(j.at("width"), l.dimension));
    t.height = unsigned(number(j.at("height"), l.dimension));
    t.depth = unsigned(number(j.at("depth"), l.depth));
    t.layers = unsigned(number(j.at("layers"), l.layers));
    t.mips = unsigned(number(j.at("mips"), 32));
    t.sampler = j.at("sampler").get<SamplerState>();
    validate_header(t, l);
    std::size_t expected = 0;
    for (unsigned mip = 0; mip < t.mips; ++mip)
        add(expected, multiply(texture_layout(t, mip).bytes, surfaces(t)));
    require(expected == envelope.payload.size(), "Texture payload size mismatch");
    t.subresources.reserve(multiply(surfaces(t), t.mips));
    std::size_t at = 0;
    for (unsigned surface = 0; surface < surfaces(t); ++surface)
        for (unsigned mip = 0; mip < t.mips; ++mip) {
            const auto n = texture_layout(t, mip).bytes;
            t.subresources.emplace_back(envelope.payload.begin() + at,
                                        envelope.payload.begin() + at + n);
            at += n;
        }
    validate_texture(t, l);
    return t;
}
} // namespace forge
