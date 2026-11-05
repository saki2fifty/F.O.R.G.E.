#pragma once
#include "presentation_diligent.hpp"
#include <array>
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
                                    bool reset_exposure = false,
                                    const SceneFastApproximateAntiAliasing& = {},
                                    const SceneMorphologicalAntiAliasing& = {});
    Diligent::ITextureView* output() const;
    Diligent::ITextureView* target() const;

  private:
    void prepare_automatic();
    void prepare_fast_aa();
    void filter_fast_aa(Diligent::IDeviceContext*, const SceneFastApproximateAntiAliasing&);
    void prepare_morphological_aa();
    void filter_morphological_aa(Diligent::IDeviceContext*, const SceneMorphologicalAntiAliasing&);
    std::array<Diligent::RefCntAutoPtr<Diligent::IPipelineState>, 3> morphological_pipelines_;
    std::array<Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>, 3>
        morphological_bindings_;
    std::array<Diligent::RefCntAutoPtr<Diligent::ITexture>, 2> morphological_images_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> morphological_values_;
    DiligentPresentation& presentation_;
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device_;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pipeline_;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> binding_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> values_;
    Diligent::RefCntAutoPtr<Diligent::ITexture> output_;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> automatic_pipeline_;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> automatic_binding_;
    std::unique_ptr<AutoExposure> automatic_;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> fast_pipeline_;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> fast_binding_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> fast_values_;
    Diligent::RefCntAutoPtr<Diligent::ITexture> spatial_output_;
    bool spatial_active_ = false;
};
} // namespace forge
