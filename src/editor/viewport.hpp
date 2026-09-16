#pragma once
#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/PipelineState.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "scene_cache.hpp"
#include <forge/scene.hpp>
namespace forge {
class Viewport {
  public:
    std::uint64_t redraws = 0, retained = 0;
    explicit Viewport(Diligent::IRenderDevice* device);
    Diligent::ITextureView* render(Diligent::IDeviceContext* context, const Json& scene,
                                   unsigned width, unsigned height, const EditorCamera& camera,
                                   std::uint64_t generation, bool live);

  private:
    std::optional<ViewportFrameKey> frame_;
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device_;
    Diligent::RefCntAutoPtr<Diligent::ITexture> color_, depth_;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pipeline_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> constants_, vertices_;
    std::array<unsigned, 4> starts_{}, counts_{};
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> resources_;
};
} // namespace forge
