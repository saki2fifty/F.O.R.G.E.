#pragma once
#include "presentation_diligent.hpp"
#include <forge/render_view.hpp>
namespace forge {
inline constexpr std::uint64_t weighted_transparency_payload_limit = 512ull * 1024 * 1024;
// Derived attachments and resolve for the existing Mesh queue. No authored
// materials, identities, resource pool, draw queue or per-fragment lists live here.
class WeightedTransparency {
  public:
    explicit WeightedTransparency(DiligentPresentation&,
                                  std::uint64_t budget = weighted_transparency_payload_limit);
    void prepare(Diligent::ITexture* color, Diligent::ITextureView* depth, PixelViewport);
    void begin(Diligent::IDeviceContext*, Diligent::ITextureView* depth);
    void composite(Diligent::IDeviceContext*, Diligent::ITexture* color,
                   Diligent::ITextureView* depth);
    void clear();

  private:
    void viewport(Diligent::IDeviceContext*) const;
    DiligentPresentation& presentation_;
    std::uint64_t budget_;
    PixelViewport area_{};
    Diligent::TEXTURE_FORMAT color_format_ = Diligent::TEX_FORMAT_UNKNOWN;
    // Bindings die before the textures they reference.
    Diligent::RefCntAutoPtr<Diligent::ITexture> radiance_, optical_;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pipeline_;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> binding_;
};
} // namespace forge
