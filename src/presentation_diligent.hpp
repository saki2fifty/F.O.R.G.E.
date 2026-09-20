#pragma once
#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsTools/interface/RenderStateCache.h"
#include "PBR_Renderer.hpp"
#include <cstdint>
#include <memory>
namespace forge {
// One presentation-thread owner per device. No world, asset identity or UI state.
// Native Diligent hashes and caches complete shaders/PSOs; views own their SRBs.
class DiligentPresentation {
  public:
    explicit DiligentPresentation(Diligent::IRenderDevice*);
    Diligent::IRenderDevice* device() const { return device_; }
    void shader(const Diligent::ShaderCreateInfo&, Diligent::IShader**);
    void graphics(const Diligent::GraphicsPipelineStateCreateInfo&, Diligent::IPipelineState**);
    void clear_cache(); // Active native objects remain valid through their strong references.
    Diligent::PBR_Renderer& pbr(Diligent::IDeviceContext*);
    std::uint64_t cache_hits() const { return hits_; }
    std::uint64_t cache_misses() const { return misses_; }

  private:
    void trim();
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device_;
    Diligent::RefCntAutoPtr<Diligent::IRenderStateCache> cache_;
    std::unique_ptr<Diligent::PBR_Renderer> pbr_;
    std::uint64_t hits_{}, misses_{};
    unsigned epoch_creations_{};
};
} // namespace forge
