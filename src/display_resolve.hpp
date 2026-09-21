#pragma once
#include "presentation_diligent.hpp"
namespace forge {
// Presentation-owned SDR output from a linear HDR target. The UNORM output stores
// sRGB-encoded values and must be displayed without another gamma conversion.
class DisplayResolve {
  public:
    explicit DisplayResolve(DiligentPresentation&);
    Diligent::ITextureView* resolve(Diligent::IDeviceContext*, Diligent::ITextureView*,
                                    float exposure_ev);
    Diligent::ITextureView* output() const;
    Diligent::ITextureView* target() const;

  private:
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device_;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pipeline_;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> binding_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> values_;
    Diligent::RefCntAutoPtr<Diligent::ITexture> output_;
};
} // namespace forge
