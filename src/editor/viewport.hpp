#pragma once
#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/PipelineState.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "presentation_diligent.hpp"
#include "scene_cache.hpp"
#include <forge/primitive_catalog.hpp>
#include <forge/scene.hpp>
namespace forge {
class Viewport {
  public:
    std::uint64_t redraws = 0, retained = 0;
    explicit Viewport(DiligentPresentation& presentation);
    Diligent::ITextureView* render(Diligent::IDeviceContext* context, const Json& scene,
                                   unsigned width, unsigned height, const EditorCamera& camera,
                                   std::uint64_t generation, bool live, GridSettings grid = {});

  private:
    std::optional<ViewportFrameKey> frame_;
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device_;
    Diligent::RefCntAutoPtr<Diligent::ITexture> color_, depth_;
    std::array<Diligent::RefCntAutoPtr<Diligent::IPipelineState>, 3> pipelines_;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> grid_pipeline_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> constants_, vertices_, grid_constants_;
    std::array<unsigned, primitive_count> starts_{}, counts_{};
    std::array<Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>, 3> resources_;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> grid_resources_;
};
} // namespace forge
