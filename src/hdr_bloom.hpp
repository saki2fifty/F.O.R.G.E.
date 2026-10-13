#pragma once
#include "hdr_bloom_plan.hpp"
#include "presentation_diligent.hpp"
#include <array>
#include <forge/scene_render_settings.hpp>
#include <functional>
#include <thread>
namespace forge {
// One view-local spatial pass. Creation routes through the current presentation
// owner; native probes can supply precompiled shaders without another renderer.
class HdrBloom {
  public:
    using ShaderFactory =
        std::function<void(const Diligent::ShaderCreateInfo&, Diligent::IShader**)>;
    using PipelineFactory = std::function<void(const Diligent::GraphicsPipelineStateCreateInfo&,
                                               Diligent::IPipelineState**)>;
    explicit HdrBloom(DiligentPresentation& presentation)
        : HdrBloom(
              presentation.device(),
              [&presentation](const auto& info, auto** out) { presentation.shader(info, out); },
              [&presentation](const auto& info, auto** out) { presentation.graphics(info, out); }) {
    }
    HdrBloom(Diligent::IRenderDevice*, ShaderFactory, PipelineFactory,
             std::uint64_t payload_budget = hdr_bloom_payload_limit);
    HdrBloom(const HdrBloom&) = delete;
    HdrBloom& operator=(const HdrBloom&) = delete;
    HdrBloom(HdrBloom&&) = delete;
    HdrBloom& operator=(HdrBloom&&) = delete;
    void apply(Diligent::IDeviceContext*, Diligent::ITexture*, PixelViewport, const SceneBloom&);
    std::uint64_t allocations() const { return allocations_; }
    std::uint64_t draws() const { return draws_; }
    std::uint64_t payload_bytes() const { return payload_; }

  private:
    void prepare();
    struct Targets {
        Diligent::RefCntAutoPtr<Diligent::ITexture> snapshot, first, second;
        std::uint64_t bytes = 0, used = 0;
    };
    Targets& targets(const HdrBloomPlan&);
    void draw(Diligent::IDeviceContext*, unsigned pass, Diligent::ITextureView* source,
              Diligent::ITextureView* target, PixelViewport, const SceneBloom&, bool horizontal);
    const std::thread::id thread_ = std::this_thread::get_id();
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device_;
    ShaderFactory shader_;
    PipelineFactory pipeline_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> values_;
    std::array<Diligent::RefCntAutoPtr<Diligent::IPipelineState>, 3> passes_;
    std::array<Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>, 3> bindings_;
    std::array<Targets, 8> cached_targets_;
    Targets* active_ = nullptr;
    std::uint64_t uses_ = 0;
    std::uint64_t budget_ = hdr_bloom_payload_limit;
    std::uint64_t allocations_ = 0, draws_ = 0, payload_ = 0;
};
} // namespace forge
