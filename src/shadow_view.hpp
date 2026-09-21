#pragma once
#include "Components/interface/ShadowMapManager.hpp"
#include "render_bounds.hpp"
#include <forge/scene_render_settings.hpp>
namespace forge {
struct ShadowView {
    CameraView camera;
    double begin = 0, end = 0; // Main-camera depth for directional cascade selection.
};
// Native cascade splitting/stabilization, with FORGE's camera-relative origin and
// caster-aware depth extent. These are derived view values, never ECS transforms.
std::vector<ShadowView> directional_shadow_views(Diligent::ShadowMapManager&, const CameraView&,
                                                 const LightView&, const SceneShadows&,
                                                 std::span<const RenderBounds> casters);
// Spot uses its outer cone; point uses six +/-XYZ faces. CameraView preserves the
// same projection and winding conventions as ordinary rendering.
std::vector<ShadowView> punctual_shadow_views(const LightView&, const SceneShadows&);
} // namespace forge
