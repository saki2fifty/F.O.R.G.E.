#include "material_parameter_overrides.hpp"
#include "pbr_material.hpp"
#include <algorithm>
#include <cstring>
#include <tuple>
namespace forge::detail {
bool same_material_parameter_values(std::span<const MaterialParameterOverride> a,
                                    std::span<const MaterialParameterOverride> b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].slot != b[i].slot || a[i].key != b[i].key ||
            a[i].parameter.type != b[i].parameter.type ||
            std::memcmp(a[i].parameter.value.data(), b[i].parameter.value.data(),
                        4 * sizeof(float)))
            return false;
    return true;
}
bool object_material_parameter_supported(std::string_view key,
                                         const SurfaceShaderDefinition* surface) {
    if (surface)
        return surface->parameters.contains(std::string(key));
    // These affect the selected shader/pass or cached optical CPU inputs. A
    // material variation remains the owner for those changes. Other keys still
    // require admission against the selected material model's exact declaration.
    constexpr std::string_view fixed[]{
        "sheenColorFactor", "anisotropyStrength", "transmissionFactor", "thicknessFactor", "ior",
        "dispersion",       "attenuationDistance"};
    return std::find(std::begin(fixed), std::end(fixed), key) == std::end(fixed);
}
MaterialData project_material_parameters(const MaterialData& source,
                                         const SurfaceShaderDefinition* surface,
                                         std::string_view slot,
                                         std::span<const MaterialParameterOverride> overrides,
                                         std::vector<MaterialParameterOverride>* differences) {
    validate_material_parameter_overrides(overrides);
    validate_material_slot_key(slot);
    const auto base = surface ? PbrMaterialProfile{} : prepare_pbr_material(source);
    if (surface)
        validate_surface_material(source, *surface);
    auto candidate = surface ? source : base.values;
    std::vector<MaterialParameterOverride> changed;
    for (const auto& value : overrides) {
        if (value.slot != slot)
            continue;
        const auto found = candidate.parameters.find(value.key);
        if (found == candidate.parameters.end())
            throw std::runtime_error("Object material parameter is unavailable: " + value.key);
        if (found->second.type != value.parameter.type)
            throw std::runtime_error("Object material parameter semantic type differs: " +
                                     value.key);
        if (!object_material_parameter_supported(value.key, surface))
            throw std::runtime_error("Parameter requires a material variation: " + value.key);
        if (differences) {
            const auto width = material_parameter_width(value.parameter.type);
            if (std::memcmp(found->second.value.data(), value.parameter.value.data(),
                            width * sizeof(float))) {
                changed.push_back(value);
                std::fill(changed.back().parameter.value.begin() + width,
                          changed.back().parameter.value.end(), 0.f);
            }
        }
        found->second = value.parameter;
    }
    if (surface)
        validate_surface_material(candidate, *surface);
    else {
        const auto prepared = prepare_pbr_material(candidate);
        // Metallic can change transmission eligibility even though it is a
        // numeric uniform. Keep the exact existing pass selection authoritative.
        if (material_transmits(base) != material_transmits(prepared))
            throw std::runtime_error(
                "Object material values change the selected transmission pass");
    }
    if (differences) {
        std::sort(changed.begin(), changed.end(), [](const auto& a, const auto& b) {
            return std::tie(a.slot, a.key) < std::tie(b.slot, b.key);
        });
        *differences = std::move(changed);
    }
    return candidate;
}
} // namespace forge::detail
