#pragma once
#include "presentation_diligent.hpp"
#include <forge/scene_render_settings.hpp>

namespace forge {
// Spatial operator only. The existing view supplies current indirect radiance
// separately from direct/emissive color and owns every texture and camera.
class AmbientOcclusion {
  public:
    explicit AmbientOcclusion(DiligentPresentation& presentation) : presentation_(presentation) {}
    AmbientOcclusion(const AmbientOcclusion&) = delete;
    AmbientOcclusion& operator=(const AmbientOcclusion&) = delete;
    struct Input {
        Diligent::ITextureView* color = nullptr;
        Diligent::ITextureView* indirect = nullptr;
        Diligent::ITextureView* depth = nullptr;
        std::array<float, 16> projection{};
        SceneAmbientOcclusion settings;
        Diligent::ITextureView* surface = nullptr;
    };
    void prepare(bool material_normals = false);
    void apply(Diligent::IDeviceContext*, const Input&, Diligent::ITextureView* output);

  private:
    DiligentPresentation& presentation_;
    const std::thread::id thread_ = std::this_thread::get_id();
    struct Mode {
        Diligent::RefCntAutoPtr<Diligent::IPipelineState> pipeline;
        Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> binding;
        Diligent::RefCntAutoPtr<Diligent::IBuffer> values;
    };
    Mode modes_[2];
};
} // namespace forge
