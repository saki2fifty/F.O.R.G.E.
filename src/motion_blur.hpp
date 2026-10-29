#pragma once
#include "presentation_diligent.hpp"
#include <forge/scene_render_settings.hpp>

namespace forge {
// Pixel operator only. The existing view owns targets, completed geometry and
// any frozen capture field; this class owns no history, Scene or clock.
class MotionBlur {
  public:
    explicit MotionBlur(DiligentPresentation& presentation) : presentation_(presentation) {}
    MotionBlur(const MotionBlur&) = delete;
    MotionBlur& operator=(const MotionBlur&) = delete;
    struct Input {
        Diligent::ITextureView* color = nullptr;
        Diligent::ITextureView* motion = nullptr;
        Diligent::ITextureView* depth = nullptr;
        Diligent::ITextureView* motion_depth = nullptr;
        std::array<float, 4> projection{};
        std::array<float, 2> motion_offset{}; // Motion raster minus current raster, in pixels.
        SceneMotionBlur settings;
    };
    void prepare();
    void apply(Diligent::IDeviceContext*, const Input&, Diligent::ITextureView* output);

  private:
    DiligentPresentation& presentation_;
    const std::thread::id thread_ = std::this_thread::get_id();
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pipeline_;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> binding_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> values_;
};
} // namespace forge
