#pragma once
#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/PipelineState.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "camera.hpp"
#include <forge/scene.hpp>
namespace forge {
class Viewport {
  public:
    explicit Viewport(Diligent::IRenderDevice* device);
    Diligent::ITextureView* render(Diligent::IDeviceContext* context, const Json& scene,
                                   unsigned width, unsigned height, const EditorCamera& camera);

  private:
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device_;
    Diligent::RefCntAutoPtr<Diligent::ITexture> color_, depth_;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pipeline_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> constants_;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> resources_;
};
} // namespace forge
