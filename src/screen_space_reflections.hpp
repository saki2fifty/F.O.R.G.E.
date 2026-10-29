#pragma once
#include "presentation_diligent.hpp"
#include <forge/scene_render_settings.hpp>
namespace forge {
// Current visible-depth operator. The existing view owns every input/result;
// material evaluation supplies real base response and replaceable specular light.
class ScreenSpaceReflections {
  public:
    explicit ScreenSpaceReflections(DiligentPresentation& presentation)
        : presentation_(presentation) {}
    ScreenSpaceReflections(const ScreenSpaceReflections&) = delete;
    ScreenSpaceReflections& operator=(const ScreenSpaceReflections&) = delete;
    struct Input {
        Diligent::ITextureView* color = nullptr;
        Diligent::ITextureView* indirect = nullptr;
        Diligent::ITextureView* depth = nullptr;
        Diligent::ITextureView* surface = nullptr;
        Diligent::ITextureView* response = nullptr;
        Diligent::ITextureView* specular = nullptr;
        std::array<float, 16> projection{};
        SceneScreenSpaceReflections settings;
    };
    void prepare();
    void apply(Diligent::IDeviceContext*, const Input&, Diligent::ITextureView* color_output,
               Diligent::ITextureView* indirect_output);

  private:
    DiligentPresentation& presentation_;
    const std::thread::id thread_ = std::this_thread::get_id();
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pipeline_;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> binding_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> values_;
};
} // namespace forge
