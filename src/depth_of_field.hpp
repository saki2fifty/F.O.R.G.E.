#pragma once
#include "depth_of_field_plan.hpp"
#include "presentation_diligent.hpp"

namespace forge {
// One presentation view's spatial scratch pair. The caller owns camera/frame,
// phase ordering and authored settings. No history, clock or resource identity.
class DepthOfField {
  public:
    explicit DepthOfField(DiligentPresentation&,
                          std::uint64_t byte_limit = depth_of_field_payload_limit);
    void prepare(Diligent::ITexture* color, Diligent::ITextureView* depth, const CameraView&,
                 const SceneDepthOfField&);
    void apply(Diligent::IDeviceContext*, Diligent::ITexture* color, Diligent::ITextureView* depth,
               const CameraView&, const SceneDepthOfField&);
    std::uint64_t payload_bytes() const { return bytes_; }
    std::uint64_t allocations() const { return allocations_; }

  private:
    void pipeline();
    DiligentPresentation& presentation_;
    const std::thread::id thread_ = std::this_thread::get_id();
    const std::uint64_t limit_;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pipeline_;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> binding_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> values_;
    Diligent::RefCntAutoPtr<Diligent::ITexture> snapshot_, filtered_;
    std::uint64_t bytes_ = 0, allocations_ = 0;
};
} // namespace forge
