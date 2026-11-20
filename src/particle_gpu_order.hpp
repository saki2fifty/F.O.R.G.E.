#pragma once
#include "presentation_diligent.hpp"
#include <array>
#include <forge/render_view.hpp>
#include <span>
namespace forge {
// Derived view data owned by MeshSceneRenderer. Source order defines stable
// ties; the caller supplies canonical Scene order, then birth/segment order.
// This resource owns no particles, simulation clock, assets or draw queue.
class ParticleGpuOrder {
  public:
    struct Source {
        Diligent::IBuffer* buffer = nullptr; // Borrowed for this submission.
        unsigned count = 0;
    };
    explicit ParticleGpuOrder(DiligentPresentation&);
    // Unbinds render targets; the camera owner restores them on both paths.
    // Count clears before validation and publishes only after the final gather.
    void evaluate(Diligent::IDeviceContext*, std::span<const Source>, const CameraView&,
                  const std::array<float, 3>& mesh_center);
    Diligent::IBuffer* buffer() const { return ordered_; }
    unsigned count() const { return count_; }
    void clear();

  private:
    void prepare();
    DiligentPresentation& presentation_;
    unsigned count_ = 0;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> combined_, keys_, ordered_, values_;
    std::array<Diligent::RefCntAutoPtr<Diligent::IPipelineState>, 3> pipelines_;
    // Bindings die before the resources they retain.
    std::array<Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>, 3> bindings_;
};
} // namespace forge
