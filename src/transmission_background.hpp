#pragma once
#include "presentation_diligent.hpp"
#include <forge/render_view.hpp>
namespace forge {
struct TransmissionLighting {
    Diligent::ITextureView* background{};
    PixelViewport viewport;
};
// A cropped immutable-for-the-pass snapshot, not a scene asset or another world.
// The caller draws opaque color first, captures, then restores its color/depth
// targets before drawing transmissive surfaces. Never sample the active RTV.
class TransmissionBackground {
  public:
    explicit TransmissionBackground(DiligentPresentation& presentation,
                                    std::uint64_t budget = 512ull * 1024 * 1024)
        : device_(presentation.device()), budget_(budget) {}
    TransmissionLighting capture(Diligent::IDeviceContext*, Diligent::ITexture*, PixelViewport);
    Diligent::ITextureView* output() const;
    void clear() { texture_.Release(); }

  private:
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device_;
    std::uint64_t budget_;
    Diligent::RefCntAutoPtr<Diligent::ITexture> texture_;
};
} // namespace forge
