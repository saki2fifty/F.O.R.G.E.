#pragma once
#include "presentation_diligent.hpp"
#include "shadow_lighting.hpp"
#include "shadow_view.hpp"
#include <forge/render_scene.hpp>
#include <functional>
namespace forge {
struct ShadowCasterBounds {
    RenderBounds bounds;
    std::uint32_t layers;
};
class ShadowRenderer {
  public:
    explicit ShadowRenderer(DiligentPresentation& presentation) : presentation_(presentation) {}
    using Draw = std::function<void(const CameraView&, std::uint32_t)>;
    void render(Diligent::IDeviceContext*, const RenderScene&, const CameraView&,
                std::uint32_t layers, std::span<const ShadowCasterBounds>, const Draw&);
    const ShadowLighting& lighting() const { return lighting_; }
    int selection(EntityId id) const;
    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }

  private:
    struct Map {
        unsigned resolution{}, slices{};
        std::unique_ptr<Diligent::ShadowMapManager> native;
    };
    DiligentPresentation& presentation_;
    std::array<Map, shadow_light_limit> maps_;
    std::map<EntityId, int> selections_;
    ShadowLighting lighting_;
    std::vector<Diagnostic> diagnostics_;
};
} // namespace forge
