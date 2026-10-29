#pragma once
#include "temporal_filter.hpp"
#include "temporal_geometry.hpp"
#include <forge/render_view.hpp>

namespace forge {
// Derived targets and completed camera state for one presentation view. The host
// owns frame/time/resource reset policy and calls complete only after success.
class TemporalView {
  public:
    explicit TemporalView(DiligentPresentation&, std::uint64_t byte_limit);
    void prepare(const CameraView&, bool reset);
    void begin(Diligent::IDeviceContext*, Diligent::ITexture* composed_color,
               Diligent::ITextureView* composed_depth);
    void resolve(Diligent::IDeviceContext*, float stability, bool complete_geometry = true);
    void complete();
    void invalidate();
    const CameraView& current() const { return current_; }
    const CameraView& previous() const { return valid_ ? previous_ : current_; }
    std::array<float, 2> jitter() const { return jitter_; }
    std::array<float, 2> previous_jitter() const { return valid_ ? previous_jitter_ : jitter_; }
    bool valid() const { return valid_; }
    std::uint64_t samples() const { return samples_; }
    const TemporalMeshParts& previous_parts() const { return previous_parts_; }
    TemporalMeshParts& pending_parts() { return pending_parts_; }
    // 28 bytes of input targets + 20 bytes of history per pixel, excluding native overhead.
    static constexpr std::uint64_t bytes_per_pixel = 48;
    static bool discontinuous(const CameraView& previous, const CameraView& current);

  private:
    DiligentPresentation& presentation_;
    const std::uint64_t byte_limit_;
    TemporalFilter filter_;
    Diligent::RefCntAutoPtr<Diligent::ITexture> color_, motion_, depth_;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> composite_;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> binding_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> rectangle_;
    Diligent::RefCntAutoPtr<Diligent::ITexture> destination_;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> destination_depth_;
    CameraView current_{}, previous_{};
    PixelViewport area_{};
    std::array<float, 2> jitter_{}, previous_jitter_{};
    std::uint64_t samples_ = 0;
    bool valid_ = false, prepared_ = false, begun_ = false, resolved_ = false;
    TemporalMeshParts previous_parts_, pending_parts_;
};
} // namespace forge
