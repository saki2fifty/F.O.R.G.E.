#pragma once
#include "auto_exposure_plan.hpp"
#include "presentation_diligent.hpp"
#include <forge/scene_render_settings.hpp>

namespace forge {
// Optional derived state of one display owner. No clock, scene, UI, asset IDs,
// CPU readback or shared material ownership. All GPU creation is staged first.
class AutoExposure {
  public:
    explicit AutoExposure(DiligentPresentation& presentation)
        : presentation_(presentation), device_(presentation.device()) {}
    void prepare();
    Diligent::IBufferView* apply(Diligent::IDeviceContext*, Diligent::ITextureView*,
                                 const SceneAutoExposure&, float delta_seconds, bool reset);
    void clear_source();

  private:
    DiligentPresentation& presentation_;
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device_;
    std::array<Diligent::RefCntAutoPtr<Diligent::IPipelineState>, 2> pipelines_;
    std::array<Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>, 2> bindings_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> histogram_, exposure_, values_;
};
} // namespace forge
