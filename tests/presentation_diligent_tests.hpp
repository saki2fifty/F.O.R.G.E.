#pragma once
#include "presentation_diligent.hpp"
#include "render_backend_tests.hpp"
#include <cmath>
#include <cstring>
// Included after the viewport fixture's readback/require helpers.
void check_native_pbr(forge::DiligentPresentation& presentation,
                      Diligent::IDeviceContext* context) {
    using namespace Diligent;
    check_renderer_backend_policy();
    auto* device = presentation.device();
    auto& pbr = presentation.pbr(context);
    require(&pbr == &presentation.pbr(context), "PBR resources recreated on repeated access");
    auto white = readback(device, context, pbr.GetWhiteTexSRV());
    require(
        std::all_of(white.begin(), white.end(),
                    [](auto p) { return p == std::array<unsigned char, 4>{255, 255, 255, 255}; }),
        "Native PBR white fallback has incorrect pixels");
    auto normal = readback(device, context, pbr.GetDefaultNormalMapSRV());
    require(std::all_of(normal.begin(), normal.end(),
                        [](auto p) {
                            return std::abs(int(p[0]) - 128) <= 1 &&
                                   std::abs(int(p[1]) - 128) <= 1 && p[2] == 255;
                        }),
            "Native PBR default normal map is not flat");
    const float constant[] = {.25f, .5f, .75f, 1.f};
    auto source = pbr.CreateIrradianceCube(context, "FORGE IBL fixture source",
                                           TEX_FORMAT_RGBA32_FLOAT, 16, constant);
    auto diffuse =
        pbr.CreateIrradianceCube(context, "FORGE IBL fixture diffuse", TEX_FORMAT_RGBA32_FLOAT, 8);
    auto specular = pbr.CreatePrefilteredEnvMap(context, "FORGE IBL fixture specular",
                                                TEX_FORMAT_RGBA32_FLOAT, 16);
    require(source && diffuse && specular, "Native IBL target allocation failed");
    PBR_Renderer::PrecomputeCubemapsAttribs info;
    info.pEnvironmentMapSRV = source->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    info.pIrradianceCube = diffuse;
    info.pPrefilteredEnvMap = specular;
    info.NumDiffuseSamples = 64;
    info.NumSpecularSamples = 64;
    pbr.PrecomputeCubemaps(context, info);
    for (ITexture* result : {diffuse.RawPtr(), specular.RawPtr()}) {
        auto desc = result->GetDesc();
        desc.Usage = USAGE_STAGING;
        desc.BindFlags = BIND_NONE;
        desc.CPUAccessFlags = CPU_ACCESS_READ;
        desc.MiscFlags = MISC_TEXTURE_FLAG_NONE;
        RefCntAutoPtr<ITexture> staging;
        device->CreateTexture(desc, nullptr, &staging);
        require(bool(staging), "IBL staging allocation failed");
        context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
        for (unsigned face = 0; face < 6; ++face)
            for (unsigned mip = 0; mip < desc.MipLevels; ++mip) {
                CopyTextureAttribs copy;
                copy.pSrcTexture = result;
                copy.pDstTexture = staging;
                copy.SrcMipLevel = copy.DstMipLevel = mip;
                copy.SrcSlice = copy.DstSlice = face;
                copy.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
                copy.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
                context->CopyTexture(copy);
            }
        context->WaitForIdle();
        for (unsigned face = 0; face < 6; ++face)
            for (unsigned mip = 0; mip < desc.MipLevels; ++mip) {
                MappedTextureSubresource mapped;
                context->MapTextureSubresource(staging, mip, face, MAP_READ, MAP_FLAG_DO_NOT_WAIT,
                                               nullptr, mapped);
                require(mapped.pData != nullptr, "IBL readback unavailable");
                const unsigned size = std::max(1u, desc.Width >> mip);
                bool valid = true;
                for (unsigned y = 0; y < size; ++y)
                    for (unsigned x = 0; x < size; ++x) {
                        float pixel[4];
                        std::memcpy(pixel,
                                    static_cast<const char*>(mapped.pData) + y * mapped.Stride +
                                        x * sizeof(pixel),
                                    sizeof(pixel));
                        for (unsigned c = 0; c < 3; ++c)
                            valid &=
                                std::isfinite(pixel[c]) && std::abs(pixel[c] - constant[c]) < .003f;
                    }
                context->UnmapTextureSubresource(staging, mip, face);
                require(valid, "Native IBL changed constant radiance or generated NaN");
            }
        context->FinishFrame();
    }
}
