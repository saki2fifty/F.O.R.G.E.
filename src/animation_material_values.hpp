#pragma once
#include "model_draw_candidate.hpp"
#include <forge/transform_animation.hpp>
namespace forge {
// Detached numeric candidates. Existing MeshRenderer/resource/source owners
// retain authority; these functions perform no IO, resource requests or writes.
std::vector<MaterialParameterOverride>
sample_animation_material_parameters(const TransformAnimationClip&, double time_us,
                                     std::span<const MaterialParameterOverride> current);
// Validate every selected part's exact semantic declaration and whole curve
// range, including model-owned restrictions and fixed render-pass eligibility.
void validate_animation_material_target(const TransformAnimationClip&,
                                        const asset_detail::PreparedModelDraw&,
                                        std::span<const MaterialParameterOverride> current);
} // namespace forge
