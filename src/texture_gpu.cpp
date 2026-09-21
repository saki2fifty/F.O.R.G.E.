#include "texture_gpu.hpp"
#include "render_backend.hpp"
#include "texture_formats.hpp"
#include <bit>
#include <stdexcept>
namespace forge {
using namespace Diligent;
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
TEXTURE_ADDRESS_MODE address(TextureWrap mode) {
    switch (mode) {
    case TextureWrap::Repeat:
        return TEXTURE_ADDRESS_WRAP;
    case TextureWrap::MirroredRepeat:
        return TEXTURE_ADDRESS_MIRROR;
    case TextureWrap::ClampEdge:
        return TEXTURE_ADDRESS_CLAMP;
    case TextureWrap::ClampBorder:
        return TEXTURE_ADDRESS_BORDER;
    }
    throw std::runtime_error("Unsupported texture wrap mode");
}
COMPARISON_FUNCTION comparison(TextureCompare mode) {
    switch (mode) {
    case TextureCompare::None:
        return COMPARISON_FUNC_NEVER;
    case TextureCompare::Never:
        return COMPARISON_FUNC_NEVER;
    case TextureCompare::Less:
        return COMPARISON_FUNC_LESS;
    case TextureCompare::Equal:
        return COMPARISON_FUNC_EQUAL;
    case TextureCompare::LessEqual:
        return COMPARISON_FUNC_LESS_EQUAL;
    case TextureCompare::Greater:
        return COMPARISON_FUNC_GREATER;
    case TextureCompare::NotEqual:
        return COMPARISON_FUNC_NOT_EQUAL;
    case TextureCompare::GreaterEqual:
        return COMPARISON_FUNC_GREATER_EQUAL;
    case TextureCompare::Always:
        return COMPARISON_FUNC_ALWAYS;
    }
    throw std::runtime_error("Unsupported texture comparison mode");
}
} // namespace
RefCntAutoPtr<ISampler> upload_sampler(IRenderDevice* device, const SamplerState& input) {
    require(device != nullptr, "Texture sampler requires a render device");
    validate_sampler(input);
    const bool compare = input.compare != TextureCompare::None;
    auto filter = [&](TextureFilter f) {
        if (input.anisotropy > 1)
            return compare ? FILTER_TYPE_COMPARISON_ANISOTROPIC : FILTER_TYPE_ANISOTROPIC;
        if (f == TextureFilter::Nearest)
            return compare ? FILTER_TYPE_COMPARISON_POINT : FILTER_TYPE_POINT;
        return compare ? FILTER_TYPE_COMPARISON_LINEAR : FILTER_TYPE_LINEAR;
    };
    SamplerDesc desc;
    desc.MinFilter = filter(input.min);
    desc.MagFilter = filter(input.mag);
    desc.MipFilter = filter(input.mip);
    desc.AddressU = address(input.u);
    desc.AddressV = address(input.v);
    desc.AddressW = address(input.w);
    desc.ComparisonFunc = comparison(input.compare);
    desc.MaxAnisotropy = input.anisotropy;
    desc.MipLODBias = input.lod_bias;
    desc.MinLOD = input.min_lod;
    desc.MaxLOD = input.max_lod;
    for (unsigned i = 0; i < 4; ++i)
        desc.BorderColor[i] = input.border[i];
    prepare_renderer_sampler(device->GetAdapterInfo().Sampler, sampler_backend_limits(device),
                             desc);
    RefCntAutoPtr<ISampler> candidate;
    device->CreateSampler(desc, &candidate);
    require(bool(candidate), "Diligent sampler allocation failed");
    return candidate;
}
RefCntAutoPtr<ITexture> upload_texture(IRenderDevice* device, const forge::TextureData& input) {
    static_assert(std::endian::native == std::endian::little);
    require(device != nullptr, "Texture upload requires a render device");
    validate_texture(input);
    TextureDesc desc;
    desc.Name = "FORGE immutable cooked texture";
    desc.Width = input.width;
    desc.Height = input.height;
    desc.MipLevels = input.mips;
    desc.Format = asset_detail::diligent_texture_format(input.format);
    desc.Usage = USAGE_IMMUTABLE;
    desc.BindFlags = BIND_SHADER_RESOURCE;
    const auto& limits = device->GetAdapterInfo().Texture;
    unsigned extent = limits.MaxTexture2DDimension;
    const bool volume = input.dimension == TextureDimension::D3;
    const bool cube =
        input.dimension == TextureDimension::Cube || input.dimension == TextureDimension::CubeArray;
    switch (input.dimension) {
    case TextureDimension::D2:
        desc.Type = RESOURCE_DIM_TEX_2D;
        break;
    case TextureDimension::D2Array:
        desc.Type = RESOURCE_DIM_TEX_2D_ARRAY;
        break;
    case TextureDimension::Cube:
        desc.Type = RESOURCE_DIM_TEX_CUBE;
        break;
    case TextureDimension::CubeArray:
        require(limits.CubemapArraysSupported, "This device does not support cube arrays");
        desc.Type = RESOURCE_DIM_TEX_CUBE_ARRAY;
        break;
    case TextureDimension::D3:
        desc.Type = RESOURCE_DIM_TEX_3D;
        break;
    }
    if (volume) {
        desc.Depth = input.depth;
        extent = limits.MaxTexture3DDimension;
    } else {
        desc.ArraySize = input.layers * (cube ? 6 : 1);
        require(desc.ArraySize <= limits.MaxTexture2DArraySlices,
                "Texture slices exceed this device's capability");
        if (cube)
            extent = limits.MaxTextureCubeDimension;
    }
    require(input.width <= extent && input.height <= extent && input.depth <= extent,
            "Texture dimensions exceed this device's capability");
    const auto& format = device->GetTextureFormatInfoExt(desc.Format);
    require((format.BindFlags & BIND_SHADER_RESOURCE) != 0 &&
                (static_cast<unsigned>(format.Dimensions) & (1u << desc.Type)) != 0,
            "Texture format/dimension cannot be sampled on this device");
    std::vector<TextureSubResData> subresources;
    subresources.reserve(input.subresources.size());
    for (std::size_t i = 0; i < input.subresources.size(); ++i) {
        const auto layout = texture_layout(input, unsigned(i % input.mips));
        TextureSubResData data;
        data.pData = input.subresources[i].data();
        data.Stride = layout.row_bytes;
        data.DepthStride = layout.slice_bytes;
        subresources.push_back(data);
    }
    Diligent::TextureData initial;
    initial.pSubResources = subresources.data();
    initial.NumSubresources = static_cast<Uint32>(subresources.size());
    RefCntAutoPtr<ITexture> candidate;
    device->CreateTexture(desc, &initial, &candidate);
    require(candidate && candidate->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
            "Diligent texture upload failed");
    return candidate;
}
} // namespace forge
