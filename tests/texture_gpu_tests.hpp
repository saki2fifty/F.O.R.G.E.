#pragma once
#include "texture_formats.hpp"
#include "texture_gpu.hpp"
void check_texture_upload(forge::DiligentPresentation& presentation,
                          Diligent::IDeviceContext* context) {
    using namespace Diligent;
    auto* device = presentation.device();
    auto verify = [&](const forge::TextureData& input) {
        auto texture = forge::upload_texture(device, input);
        const auto& original = texture->GetDesc();
        require(original.Format == forge::asset_detail::diligent_texture_format(input.format),
                "GPU texture changed the cooked format/color space");
        auto desc = original;
        desc.Usage = USAGE_STAGING;
        desc.BindFlags = BIND_NONE;
        desc.CPUAccessFlags = CPU_ACCESS_READ;
        RefCntAutoPtr<ITexture> staging;
        device->CreateTexture(desc, nullptr, &staging);
        require(bool(staging), "Cooked texture staging allocation failed");
        for (std::size_t i = 0; i < input.subresources.size(); ++i) {
            CopyTextureAttribs copy;
            copy.pSrcTexture = texture;
            copy.pDstTexture = staging;
            copy.SrcMipLevel = copy.DstMipLevel = Uint32(i % input.mips);
            copy.SrcSlice = copy.DstSlice = Uint32(i / input.mips);
            copy.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
            copy.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
            context->CopyTexture(copy);
        }
        // Diligent command submission keeps the copied resource alive after its
        // caller releases the last application reference, before GPU completion.
        texture.Release();
        context->WaitForIdle();
        for (std::size_t i = 0; i < input.subresources.size(); ++i) {
            const auto mip = Uint32(i % input.mips), slice = Uint32(i / input.mips);
            MappedTextureSubresource mapped;
            context->MapTextureSubresource(staging, mip, slice, MAP_READ, MAP_FLAG_DO_NOT_WAIT,
                                           nullptr, mapped);
            require(mapped.pData != nullptr, "Cooked texture readback failed");
            const auto layout = forge::texture_layout(input, mip);
            const auto format = forge::texture_format_info(input.format);
            const auto rows = (layout.height + format.block_height - 1) / format.block_height;
            bool match = true;
            for (unsigned z = 0; z < layout.depth; ++z)
                for (unsigned y = 0; y < rows; ++y)
                    match &= std::memcmp(static_cast<const char*>(mapped.pData) +
                                             z * mapped.DepthStride + y * mapped.Stride,
                                         input.subresources[i].data() + z * layout.slice_bytes +
                                             y * layout.row_bytes,
                                         layout.row_bytes) == 0;
            context->UnmapTextureSubresource(staging, mip, slice);
            require(match, "GPU texture changed subresource bytes/row or slice ordering");
        }
        context->FinishFrame();
    };
    for (const auto dimension : {forge::TextureDimension::D2, forge::TextureDimension::D2Array,
                                 forge::TextureDimension::Cube, forge::TextureDimension::CubeArray,
                                 forge::TextureDimension::D3}) {
        forge::TextureData texture;
        texture.dimension = dimension;
        texture.semantic = forge::TextureSemantic::Data;
        texture.width = texture.height = 16;
        texture.depth = dimension == forge::TextureDimension::D3 ? 4 : 1;
        texture.layers = dimension == forge::TextureDimension::D2Array ||
                                 dimension == forge::TextureDimension::CubeArray
                             ? 2
                             : 1;
        texture.mips = 3;
        const bool cube = dimension == forge::TextureDimension::Cube ||
                          dimension == forge::TextureDimension::CubeArray;
        for (unsigned i = 0; i < texture.layers * (cube ? 6 : 1) * texture.mips; ++i) {
            auto& bytes = texture.subresources.emplace_back(
                forge::texture_layout(texture, i % texture.mips).bytes);
            for (std::size_t n = 0; n < bytes.size(); ++n)
                bytes[n] = std::byte((n + i * 29) % 256);
        }
        verify(texture);
    }
    for (unsigned f = 0; f <= unsigned(forge::TextureFormat::BC7Srgb); ++f) {
        forge::TextureData texture;
        texture.width = texture.height = 16;
        texture.mips = 3;
        texture.format = forge::TextureFormat(f);
        const auto info = forge::texture_format_info(texture.format);
        texture.semantic = info.srgb         ? forge::TextureSemantic::Color
                           : info.float_bits ? forge::TextureSemantic::HdrColor
                                             : forge::TextureSemantic::Data;
        for (unsigned i = 0; i < texture.mips; ++i)
            texture.subresources.emplace_back(forge::texture_layout(texture, i).bytes);
        verify(texture);
    }
    forge::SamplerState sampler;
    sampler.u = forge::TextureWrap::MirroredRepeat;
    sampler.v = forge::TextureWrap::ClampEdge;
    sampler.w = forge::TextureWrap::ClampBorder;
    sampler.min_lod = .5f;
    sampler.max_lod = 3.f;
    sampler.lod_bias = .25f;
    sampler.border = {.25f, .5f, .75f, 1.f};
    for (unsigned c = 0; c <= unsigned(forge::TextureCompare::Always); ++c) {
        sampler.compare = forge::TextureCompare(c);
        auto resource = forge::upload_sampler(device, sampler);
        const auto& desc = resource->GetDesc();
        require(desc.AddressU == TEXTURE_ADDRESS_MIRROR && desc.AddressV == TEXTURE_ADDRESS_CLAMP &&
                    desc.AddressW == TEXTURE_ADDRESS_BORDER && desc.MinLOD == .5f &&
                    desc.MaxLOD == 3.f && desc.MipLODBias == .25f && desc.BorderColor[2] == .75f,
                "GPU sampler lost binding settings");
        require(desc.MinFilter == (c == 0 ? FILTER_TYPE_LINEAR : FILTER_TYPE_COMPARISON_LINEAR),
                "GPU sampler comparison mode mismatch");
    }
    sampler.compare = forge::TextureCompare::None;
    sampler.anisotropy = 4;
    auto anisotropic = forge::upload_sampler(device, sampler);
    require(anisotropic->GetDesc().MaxAnisotropy == 4 &&
                anisotropic->GetDesc().MinFilter == FILTER_TYPE_ANISOTROPIC,
            "GPU sampler lost anisotropy");
    forge::TextureData invalid;
    invalid.width = invalid.height = 2;
    invalid.subresources = {{std::byte{1}}};
    bool rejected = false;
    try {
        forge::upload_texture(device, invalid);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "GPU upload accepted a truncated cooked subresource");
}
