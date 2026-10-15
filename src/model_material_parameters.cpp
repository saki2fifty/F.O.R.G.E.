#include "material_parameter_overrides.hpp"
#include "model_draw_candidate.hpp"
#include <algorithm>
#include <set>
#include <tuple>
namespace forge::asset_detail {
std::vector<MaterialParameterOverride>
prepare_model_material_parameters(const PreparedModelDraw& prepared,
                                  std::span<const MaterialParameterOverride> values) {
    detail::validate_material_parameter_overrides(values);
    if (values.empty())
        return {};
    std::map<std::uint32_t, std::string_view> slots;
    for (const auto& binding : prepared.selection.bindings)
        slots.emplace(binding.physical_slot, binding.key);
    std::set<std::string_view> wanted;
    for (const auto& value : values)
        wanted.insert(value.slot);
    MaterialResourceData default_material;
    default_material.values.model = "forge.gltf.metallic-roughness.v1";
    std::vector<MaterialParameterOverride> result;
    std::set<std::pair<std::string_view, AssetId>> selections;
    for (std::size_t l = 0; l < prepared.mesh->mesh.lods.size(); ++l) {
        const auto& parts = prepared.mesh->mesh.lods[l].parts;
        const auto& materials = prepared.selection.parts.at(l);
        if (parts.size() != materials.size())
            throw std::runtime_error("Object material selection part count differs from mesh");
        for (std::size_t p = 0; p < parts.size(); ++p) {
            const auto slot = slots.at(parts[p].material_slot);
            if (wanted.contains(slot))
                selections.emplace(slot, materials[p].id);
        }
    }
    std::set<std::pair<std::string, std::string>> changed_keys;
    for (const auto& [slot, requested] : selections) {
        wanted.erase(slot);
        // Preserve the renderer's explicit missing-material error fallback. It
        // is not a successful admission of that missing declaration's parameters.
        // A recovered real material revalidates all original object values.
        if (prepared.material_fallbacks.contains(requested))
            continue;
        const auto actual = prepared.material_asset(requested);
        const auto& material = actual ? prepared.materials.at(actual).get() : default_material;
        if (material.surface && !material.surface->program.surface)
            throw std::runtime_error("Object material Shader has no surface declaration");
        const auto* surface = material.surface ? &*material.surface->program.surface : nullptr;
        std::vector<MaterialParameterOverride> changed;
        (void)detail::project_material_parameters(material.values, surface, slot, values, &changed);
        for (auto& value : changed)
            if (changed_keys.emplace(value.slot, value.key).second)
                result.push_back(std::move(value));
    }
    if (!wanted.empty())
        throw std::runtime_error("Object material slot is unavailable: " +
                                 std::string(*wanted.begin()));
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return std::tie(a.slot, a.key) < std::tie(b.slot, b.key);
    });
    return result;
}
} // namespace forge::asset_detail
