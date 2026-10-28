#pragma once
#include "presentation_diligent.hpp"
#include <forge/scene_render_settings.hpp>
#include <memory>
namespace forge {
class AutoExposure;
// Presentation-owned SDR output from a linear HDR target. The UNORM output stores
// sRGB-encoded values and must be displayed without another gamma conversion.
class DisplayResolve {
  public:
    explicit DisplayResolve(DiligentPresentation&);
    ~DisplayResolve();
    Diligent::ITextureView* resolve(Diligent::IDeviceContext*, Diligent::ITextureView*,
                                    float exposure_ev, const SceneColorAdjustments& = {},
                                    const SceneAutoExposure& = {}, float delta_seconds = 0,
                                    bool reset_exposure = false);
    Diligent::ITextureView* output() const;
    Diligent::ITextureView* target() const;

  private:
    void prepare_automatic();
    DiligentPresentation& presentation_;
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device_;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pipeline_;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> binding_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> values_;
    Diligent::RefCntAutoPtr<Diligent::ITexture> output_;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> automatic_pipeline_;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> automatic_binding_;
    std::unique_ptr<AutoExposure> automatic_;
};
} // namespace forge
