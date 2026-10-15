#include "animation_material_values.hpp"
#include "material_parameter_overrides.hpp"
#include <algorithm>
namespace forge {
std::vector<MaterialParameterOverride>
sample_animation_material_parameters(const TransformAnimationClip& clip, double time,
                                     std::span<const MaterialParameterOverride> current) {
    detail::validate_material_parameter_overrides(current);
    if (clip.material_bindings.size() > material_parameter_override_limit)
        throw std::runtime_error("Too many animated material bindings");
    std::array<AnimationMaterialValues, material_parameter_override_limit> samples;
    clip.sample_materials(time, std::span(samples).first(clip.material_bindings.size()));
    std::vector<MaterialParameterOverride> result(current.begin(), current.end());
    for (std::size_t i = 0; i < clip.material_bindings.size(); ++i) {
        const auto& binding = clip.material_bindings[i];
        const auto found = std::find_if(result.begin(), result.end(), [&](const auto& value) {
            return value.slot == binding.slot && value.key == binding.key;
        });
        MaterialParameter value{binding.type, {}};
        for (unsigned lane = 0; lane < material_parameter_width(binding.type); ++lane)
            value.value[lane] = float(samples[i][lane]);
        if (found != result.end()) {
            if (found->parameter.type != binding.type)
                throw std::runtime_error("Animated material override semantic type differs: " +
                                         binding.key);
            found->parameter = value;
        } else {
            if (result.size() == material_parameter_override_limit)
                throw std::runtime_error("Animated material values exceed the object limit (64)");
            result.push_back({binding.slot, binding.key, value});
        }
    }
    detail::validate_material_parameter_overrides(result);
    return result;
}
void validate_animation_material_target(const TransformAnimationClip& clip,
                                        const asset_detail::PreparedModelDraw& draw,
                                        std::span<const MaterialParameterOverride> current) {
    if (clip.material_bindings.empty())
        return;
    if (!draw.mesh || draw.has_fallbacks() || !draw.selection.unresolved.empty())
        throw std::runtime_error("Animated material target has an unavailable resource selection");
    auto values = sample_animation_material_parameters(clip, 0, current);
    (void)asset_detail::prepare_model_material_parameters(draw, values);
    // Current surface declarations admit independent finite lanes. Builtin PBR
    // adds independent model bounds and a single-lane metallic/pass constraint.
    // Project each analytic extremum through those existing owners, rather than
    // duplicating their declarations or checking only authored key endpoints.
    // Runtime candidates still pass the same complete projector before writes.
    for (std::size_t i = 0; i < clip.material_bindings.size(); ++i) {
        const auto& binding = clip.material_bindings[i];
        auto& value = *std::find_if(values.begin(), values.end(), [&](const auto& value) {
            return value.slot == binding.slot && value.key == binding.key;
        });
        const auto initial = value.parameter;
        for (unsigned lane = 0; lane < material_parameter_width(binding.type); ++lane) {
            for (const auto bound : clip.material_range(i, lane)) {
                value.parameter.value[lane] = float(bound);
                (void)asset_detail::prepare_model_material_parameters(draw, values);
            }
            value.parameter = initial;
        }
    }
}
} // namespace forge
