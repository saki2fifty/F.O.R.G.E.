#pragma once
#include "presentation_diligent.hpp"

namespace forge {
// One view's derived GPU accumulation. The frame owner supplies actual motion,
// previous camera projection and reset policy; this pass owns no Scene or clock.
class TemporalFilter {
  public:
    explicit TemporalFilter(DiligentPresentation&, std::uint64_t byte_limit = 256ull * 1024 * 1024);
    struct Input {
        Diligent::ITextureView* color = nullptr;
        // XY: unjittered current-minus-previous NDC; Z: previous projected depth;
        // W: valid previous surface correspondence. Background has W=0.
        Diligent::ITextureView* motion = nullptr;
        Diligent::ITextureView* depth = nullptr;
        std::array<float, 4> previous_depth_projection{}; // P22, P23, P32, P33.
        std::array<float, 2> jitter{}, previous_jitter{};
        float stability = .8f;
        bool reset = false;
    };
    static std::array<float, 2> jitter(std::uint64_t frame, unsigned width, unsigned height);
    Diligent::ITextureView* resolve(Diligent::IDeviceContext*, const Input&);
    void invalidate() { valid_ = false; }
    // Texture payload estimate; native allocation/pipeline overhead is separate.
    std::uint64_t history_bytes() const;

  private:
    void prepare(Diligent::IDeviceContext*, unsigned width, unsigned height);
    DiligentPresentation& presentation_;
    const std::uint64_t byte_limit_;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pipeline_;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> binding_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> values_;
    std::array<Diligent::RefCntAutoPtr<Diligent::ITexture>, 2> history_;
    Diligent::RefCntAutoPtr<Diligent::ITexture> depth_;
    unsigned width_ = 0, height_ = 0, last_ = 0;
    bool valid_ = false;
};
} // namespace forge
