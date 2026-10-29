#pragma once
#include "ambient_occlusion.hpp"
#include "motion_blur.hpp"
#include "temporal_filter.hpp"
#include "temporal_geometry.hpp"
#include "temporal_sample_policy.hpp"
#include <forge/render_view.hpp>

namespace forge {
// Derived targets and completed camera state for one presentation view. The host
// owns frame/time/resource reset policy and calls complete only after success.
class TemporalView {
  public:
    explicit TemporalView(DiligentPresentation&, std::uint64_t byte_limit);
    struct Options {
        bool anti_aliasing = true;
        SceneMotionBlur motion_blur;
        MotionSnapshotAction snapshot = MotionSnapshotAction::Live;
        bool reset_accumulation = false;
        SceneAmbientOcclusion ambient_occlusion;
        bool motion_output() const {
            return anti_aliasing ||
                   (motion_blur.enabled && motion_blur.shutter != 0 && motion_blur.max_pixels != 0);
        }
        bool indirect_output() const {
            return ambient_occlusion.enabled && ambient_occlusion.radius != 0 &&
                   ambient_occlusion.strength != 0;
        }
    };
    void prepare(const CameraView& view, bool reset) { prepare(view, reset, Options{}); }
    void prepare(const CameraView&, bool reset, const Options&);
    static std::uint64_t payload_per_pixel(const Options& options) {
        const bool blur = options.motion_blur.enabled && options.motion_blur.shutter != 0 &&
                          options.motion_blur.max_pixels != 0;
        return 12 + (options.motion_output() ? 16 : 0) + (options.indirect_output() ? 16 : 0) +
               (options.anti_aliasing ? 20 : 0) + (blur ? 8 : 0) +
               (blur && options.snapshot != MotionSnapshotAction::Live ? 20 : 0);
    }
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
    std::uint64_t payload_bytes() const {
        return color_ ? std::uint64_t(color_->GetDesc().Width) * color_->GetDesc().Height *
                                (12 + (motion_ ? 16 : 0) + (indirect_ ? 16 : 0) +
                                 (blurred_ ? 8 : 0) + (frozen_motion_ ? 20 : 0)) +
                            (filter_ ? filter_->history_bytes() : 0)
                      : 0;
    }
    const TemporalMeshParts& previous_parts() const { return previous_parts_; }
    TemporalMeshParts& pending_parts() { return pending_parts_; }
    // Default AA mode: 28 input + 20 history bytes/pixel. Other modes use payload_per_pixel.
    static constexpr std::uint64_t bytes_per_pixel = 48;
    static bool discontinuous(const CameraView& previous, const CameraView& current);

  private:
    DiligentPresentation& presentation_;
    const std::uint64_t byte_limit_;
    std::unique_ptr<TemporalFilter> filter_;
    std::unique_ptr<MotionBlur> blur_;
    std::unique_ptr<AmbientOcclusion> occlusion_;
    Options options_;
    Diligent::RefCntAutoPtr<Diligent::ITexture> blurred_, frozen_motion_, frozen_depth_;
    std::array<float, 2> frozen_jitter_{};
    bool frozen_valid_ = false;
    Diligent::RefCntAutoPtr<Diligent::ITexture> color_, motion_, depth_, indirect_, occluded_;
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
