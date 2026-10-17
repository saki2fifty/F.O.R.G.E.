#pragma once
#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/Fence.h"
#include "Graphics/GraphicsEngine/interface/Query.h"
#include "Graphics/GraphicsTools/interface/RenderStateCache.h"
#include "PBR_Renderer.hpp"
#include <array>
#include <cstdint>
#include <forge/services.hpp>
#include <memory>
#include <thread>
namespace forge {
// One presentation-thread owner per device. No world, asset identity or UI state.
// Native Diligent hashes and caches complete shaders/PSOs; views own their SRBs.
class DiligentPresentation {
  public:
    class GpuScope {
      public:
        ~GpuScope();
        GpuScope(const GpuScope&) = delete;
        GpuScope& operator=(const GpuScope&) = delete;

      private:
        friend class DiligentPresentation;
        GpuScope(DiligentPresentation* owner, Diligent::IDeviceContext* context, unsigned slot)
            : owner_(owner), context_(context), slot_(slot) {}
        DiligentPresentation* owner_;
        Diligent::IDeviceContext* context_;
        unsigned slot_;
    };
    explicit DiligentPresentation(Diligent::IRenderDevice*);
    Diligent::IRenderDevice* device() const { return device_; }
    Diligent::IRenderStateCache* cache() const { return cache_; }
    void shader(const Diligent::ShaderCreateInfo&, Diligent::IShader**);
    void graphics(const Diligent::GraphicsPipelineStateCreateInfo&, Diligent::IPipelineState**);
    void compute(const Diligent::ComputePipelineStateCreateInfo&, Diligent::IPipelineState**);
    // Keep at most two submitted frames outstanding, independent of VSync.
    void begin_frame();
    void end_frame(Diligent::IDeviceContext*);
    std::uint64_t pending_frames() const;
    void clear_cache(); // Active native objects remain valid through their strong references.
    Diligent::PBR_Renderer& pbr(Diligent::IDeviceContext*);
    Diligent::ITextureView* black_environment(Diligent::IDeviceContext*);
    Diligent::ITextureView* empty_shadow(Diligent::IDeviceContext*);
    std::uint64_t cache_hits() const { return hits_; }
    std::uint64_t cache_misses() const { return misses_; }
    // Optional, flat duration queries. Scope lifetime must remain inside this owner/frame.
    void gpu_services(ServiceAccess);
    void gpu_profiling(bool enabled);
    nlohmann::json gpu_profile_status() const;
    GpuScope gpu_scope(Diligent::IDeviceContext*, const char* view, const char* stage,
                       bool enabled = true);

  private:
    static constexpr unsigned gpu_capacity = 64;
    struct GpuQuery {
        Diligent::RefCntAutoPtr<Diligent::IQuery> query;
        std::string view, stage;
        std::uint64_t frame{}, epoch{}, capture{};
        bool occupied = false;
    };
    void poll_gpu();
    void gpu_thread() const;
    const std::thread::id thread_ = std::this_thread::get_id();
    ServiceAccess gpu_services_;
    std::array<GpuQuery, gpu_capacity> gpu_queries_{};
    std::array<unsigned, gpu_capacity> gpu_pending_{};
    unsigned gpu_head_{}, gpu_count_{}, gpu_active_ = gpu_capacity, gpu_created_{};
    bool gpu_enabled_ = false, gpu_failed_ = false;
    std::uint64_t gpu_capture_{}, gpu_skipped_{}, gpu_stale_{}, gpu_latency_{};
    void trim();
    Diligent::RefCntAutoPtr<Diligent::IFence> frame_fence_;
    std::uint64_t submitted_frames_{};
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device_;
    Diligent::RefCntAutoPtr<Diligent::IRenderStateCache> cache_;
    std::unique_ptr<Diligent::PBR_Renderer> pbr_;
    Diligent::RefCntAutoPtr<Diligent::ITexture> black_environment_;
    Diligent::RefCntAutoPtr<Diligent::ITexture> empty_shadow_;
    std::uint64_t hits_{}, misses_{};
    unsigned epoch_creations_{};
};
} // namespace forge
