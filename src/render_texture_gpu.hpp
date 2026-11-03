#pragma once
#include "gpu_residency.hpp"
#include "render_texture_resource.hpp"
#include <set>

namespace forge {
namespace gpu_detail {
template <> struct Traits<RenderTextureAsset> {
    using Data = Diligent::RefCntAutoPtr<Diligent::ITexture>;
    static std::uint64_t bytes(const RenderTextureData& value) {
        validate_render_texture(value);
        return std::uint64_t(value.width) * value.height * 8;
    }
    static Data upload(Diligent::IRenderDevice* device, Diligent::IDeviceContext*,
                       const RenderTextureData& value) {
        validate_render_texture(value);
        const auto limit = device->GetAdapterInfo().Texture.MaxTexture2DDimension;
        if (value.width > limit || value.height > limit)
            throw std::runtime_error("Render texture exceeds this device's image extent");
        Diligent::TextureDesc desc;
        desc.Name = "FORGE scoped camera output";
        desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
        desc.Width = value.width;
        desc.Height = value.height;
        desc.Format = Diligent::TEX_FORMAT_RGBA16_FLOAT;
        desc.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_RENDER_TARGET;
        Data image;
        device->CreateTexture(desc, nullptr, &image);
        if (!image)
            throw std::runtime_error("Render texture allocation failed");
        return image;
    }
};
} // namespace gpu_detail
using RenderTextureImages = std::map<AssetId, GpuLease<RenderTextureAsset>>;
// Borrowed pass inputs, never persisted. Only this frame's completed producers
// are readable; retained leases alone do not authorize sampling previous pixels.
struct RenderTextureInputs {
    RenderTextureImages images;
    std::set<AssetId> produced;
};
class RenderTexturePending : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};
} // namespace forge
