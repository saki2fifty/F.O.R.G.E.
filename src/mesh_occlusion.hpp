#pragma once
#include "mesh_occlusion_plan.hpp"
#include "presentation_diligent.hpp"
#include <forge/render_view.hpp>
#include <span>

namespace forge {
struct MeshOcclusionQuery {
    // Viewport-local pixel edges: left, top, right-exclusive, bottom-exclusive.
    std::array<std::uint32_t, 4> rectangle{};
    float nearest_depth = 0, depth_bias = 0;
    std::uint32_t enabled = 0, reserved = 0;
};
struct MeshOcclusionCommand {
    // Existing indexed command's five words; nonindexed commands use the first four.
    std::array<std::uint32_t, 5> words{};
};
static_assert(sizeof(MeshOcclusionQuery) == 32);
static_assert(sizeof(MeshOcclusionCommand) == 20);
inline constexpr unsigned mesh_occlusion_query_limit = 4096;
// Derived GPU scratch owned by MeshSceneRenderer. No scene/entity/resource
// identity, history, source editing, CPU visibility readback or queue ownership.
class MeshOcclusion {
  public:
    explicit MeshOcclusion(DiligentPresentation& presentation)
        : presentation_(presentation), device_(presentation.device()) {}
    bool supported() const;
    Diligent::ITexture* prepare_depth(unsigned width, unsigned height);
    // A declined call requires ordinary submission; previous masks are never reused.
    bool build(Diligent::IDeviceContext*, Diligent::ITextureView* current_depth,
               const PixelViewport&, std::span<const MeshOcclusionQuery>,
               std::span<const MeshOcclusionCommand>);
    Diligent::IBuffer* arguments() const { return active_ ? arguments_.RawPtr() : nullptr; }
    Diligent::IBuffer* hierarchy() const { return active_ ? hierarchy_.RawPtr() : nullptr; }
    const MeshOcclusionImagePlan* plan() const { return active_ ? &plan_ : nullptr; }

  private:
    void prepare();
    DiligentPresentation& presentation_;
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device_;
    std::array<Diligent::RefCntAutoPtr<Diligent::IPipelineState>, 3> pipelines_;
    std::array<Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>, 3> bindings_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> hierarchy_, queries_, arguments_, values_;
    Diligent::RefCntAutoPtr<Diligent::ITexture> depth_;
    MeshOcclusionImagePlan plan_{};
    bool active_ = false;
};
} // namespace forge
