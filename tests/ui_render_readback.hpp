#pragma once
#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace forge::test {
template <class Pixel>
std::vector<Pixel> readback_pixels(Diligent::IRenderDevice* device,
                                   Diligent::IDeviceContext* context, Diligent::ITextureView* view,
                                   Diligent::TEXTURE_FORMAT format) {
    using namespace Diligent;
    const auto require = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    auto* texture = view->GetTexture();
    auto desc = texture->GetDesc();
    require(desc.Format == format, "UI readback pixel format mismatch");
    desc.Name = "FORGE test readback";
    desc.Usage = USAGE_STAGING;
    desc.BindFlags = BIND_NONE;
    desc.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<ITexture> staging;
    device->CreateTexture(desc, nullptr, &staging);
    require(bool(staging), "Readback texture allocation failed");
    context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    CopyTextureAttribs copy;
    copy.pSrcTexture = texture;
    copy.pDstTexture = staging;
    copy.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    copy.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    context->CopyTexture(copy);
    context->WaitForIdle();
    MappedTextureSubresource data;
    context->MapTextureSubresource(staging, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr, data);
    require(data.pData != nullptr, "Readback map failed");
    std::vector<Pixel> result(desc.Width * desc.Height);
    for (unsigned y = 0; y < desc.Height; ++y)
        std::memcpy(result.data() + y * desc.Width,
                    static_cast<const unsigned char*>(data.pData) + y * data.Stride,
                    desc.Width * sizeof(Pixel));
    context->UnmapTextureSubresource(staging, 0, 0);
    context->FinishFrame();
    return result;
}
inline std::vector<std::array<unsigned char, 4>> readback(Diligent::IRenderDevice* device,
                                                          Diligent::IDeviceContext* context,
                                                          Diligent::ITextureView* view) {
    return readback_pixels<std::array<unsigned char, 4>>(device, context, view,
                                                         Diligent::TEX_FORMAT_RGBA8_UNORM);
}
} // namespace forge::test
