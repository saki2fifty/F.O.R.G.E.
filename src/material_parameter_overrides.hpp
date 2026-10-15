#pragma once
#include "material_parameter_values.hpp"
#include "material_slot.hpp"
#include <forge/material_asset.hpp>
#include <forge/surface_shader.hpp>
namespace forge::detail {
inline void
validate_material_parameter_overrides(std::span<const MaterialParameterOverride> values) {
    if (values.size() > material_parameter_override_limit)
        throw std::runtime_error("Too many object material parameter overrides (64)");
    std::set<std::pair<std::string_view, std::string_view>> keys;
    for (const auto& value : values) {
        validate_material_slot_key(value.slot);
        validate_material_key(value.key);
        validate_material_parameter(value.parameter);
        if (!keys.emplace(value.slot, value.key).second)
            throw std::runtime_error("Duplicate object material slot/parameter override");
    }
}
// Numeric candidate only. The caller owns the admitted immutable material/shader
// selection; this function never reads sources, mutates resources or compiles.
MaterialData
project_material_parameters(const MaterialData&, const SurfaceShaderDefinition*,
                            std::string_view slot, std::span<const MaterialParameterOverride>,
                            std::vector<MaterialParameterOverride>* differences = nullptr);
// Renderer request/derived comparisons preserve all float bits, including signed
// zero. Source order is compared here; admitted differences are sorted separately.
bool same_material_parameter_values(std::span<const MaterialParameterOverride>,
                                    std::span<const MaterialParameterOverride>);
bool object_material_parameter_supported(std::string_view key,
                                         const SurfaceShaderDefinition* surface);
} // namespace forge::detail
