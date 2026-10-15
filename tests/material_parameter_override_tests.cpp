#include "material_parameter_overrides.hpp"
#include "material_shader.hpp"
#include "pbr_material.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
using namespace forge;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F run) {
    bool rejected = false;
    try {
        run();
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "Invalid object material values were accepted");
}
} // namespace
int main() {
    try {
        MaterialData source;
        source.model = "forge.gltf.metallic-roughness.v1";
        const auto original = source;
        std::vector<MaterialParameterOverride> overrides{
            {"default", "roughnessFactor", {MaterialParameterType::Scalar, {.2f}}},
            {"default",
             "baseColorFactor",
             {MaterialParameterType::LinearColor4, {.8f, .1f, .2f, 1}}},
            {"other", "roughnessFactor", {MaterialParameterType::Scalar, {.9f}}}};
        const auto first =
            detail::project_material_parameters(source, nullptr, "default", overrides);
        const auto second =
            detail::project_material_parameters(source, nullptr, "other", overrides);
        require(source == original && first.parameters.at("roughnessFactor").value[0] == .2f &&
                    second.parameters.at("roughnessFactor").value[0] == .9f &&
                    second.parameters.at("baseColorFactor").value ==
                        std::array<float, 4>{1, 1, 1, 1},
                "Object-local values leaked between slots or mutated the shared material");
        const auto base = prepare_pbr_material(source);
        const auto shader = material_shader(base, {});
        const auto modified_shader = material_shader(prepare_pbr_material(first), {});
        require(shader.source == modified_shader.source &&
                    shader.uniforms.size() == modified_shader.uniforms.size() &&
                    shader.uniforms != modified_shader.uniforms,
                "Numeric variation changed shader code/layout or omitted uniform changes");
        for (const auto& field : material_parameter_uniforms(base.values)) {
            require(field.offset % (4 * sizeof(float)) == 0 &&
                        shader.uniforms.at(field.offset / (4 * sizeof(float))) ==
                            base.values.parameters.at(field.key).value &&
                        field.width == material_parameter_width(field.type),
                    "Native numeric offsets differ from shader uniform packing");
        }
        std::vector<MaterialParameterOverride> differences;
        auto equal = overrides;
        equal[0].parameter.value[0] = 1; // Canonical roughness default.
        (void)detail::project_material_parameters(source, nullptr, "default", equal, &differences);
        require(differences.size() == 1 && differences[0].key == "baseColorFactor" &&
                    equal.size() == 3 && equal[0].key == "roughnessFactor",
                "Derived normalization erased authored equal-to-default intent");
        const auto retained_differences = differences;
        equal[0].parameter.value[0] = 2;
        rejects([&] {
            (void)detail::project_material_parameters(source, nullptr, "default", equal,
                                                      &differences);
        });
        require(detail::same_material_parameter_values(differences, retained_differences),
                "Rejected material projection partially published derived values");
        auto signed_zero = overrides;
        signed_zero[0].parameter.value[1] = -0.f;
        require(!detail::same_material_parameter_values(overrides, signed_zero),
                "Request comparison lost signed-zero float bits");
        signed_zero[0].parameter.value[0] = -0.f;
        (void)detail::project_material_parameters(source, nullptr, "default", signed_zero,
                                                  &differences);
        const auto scalar = std::find_if(differences.begin(), differences.end(),
                                         [](const auto& v) { return v.key == "roughnessFactor"; });
        require(scalar != differences.end() && std::signbit(scalar->parameter.value[0]) &&
                    !std::signbit(scalar->parameter.value[1]),
                "Derived packing lost used float bits or retained unused negative zero");
        std::reverse(overrides.begin(), overrides.end());
        require(detail::project_material_parameters(source, nullptr, "default", overrides) == first,
                "Reordering overrides retargeted slot/parameter values");
        require(detail::project_material_parameters(source, nullptr, "unused", overrides) ==
                    base.values,
                "Unrelated slot consumed object values");
        auto invalid = overrides;
        invalid.push_back(invalid.front());
        rejects([&] { detail::validate_material_parameter_overrides(invalid); });
        invalid = overrides;
        invalid[0].slot = "slot/ordinal";
        rejects([&] { detail::validate_material_parameter_overrides(invalid); });
        invalid = overrides;
        invalid[0].key = "label/with/slash";
        rejects([&] { detail::validate_material_parameter_overrides(invalid); });
        invalid = overrides;
        invalid[0].parameter.value[1] = 1;
        rejects([&] { detail::validate_material_parameter_overrides(invalid); });
        invalid = overrides;
        invalid[0].parameter.type = static_cast<MaterialParameterType>(99);
        rejects([&] { detail::validate_material_parameter_overrides(invalid); });
        invalid = overrides;
        invalid[0].parameter.value[0] = std::numeric_limits<float>::infinity();
        rejects([&] { detail::validate_material_parameter_overrides(invalid); });
        invalid.assign(material_parameter_override_limit + 1, overrides.front());
        rejects([&] { detail::validate_material_parameter_overrides(invalid); });
        const auto project_one = [&](std::string key, MaterialParameter parameter) {
            const std::array values{
                MaterialParameterOverride{"default", std::move(key), parameter}};
            return detail::project_material_parameters(source, nullptr, "default", values);
        };
        rejects([&] {
            project_one("baseColorFactor", {MaterialParameterType::Vector4, {1, 1, 1, 1}});
        });
        rejects([&] {
            project_one("baseColorFactor", {MaterialParameterType::LinearColor4, {-1, 0, 0, 1}});
        });
        rejects([&] { project_one("roughnessFactor", {MaterialParameterType::Scalar, {2}}); });
        rejects([&] { project_one("missingKey", {MaterialParameterType::Scalar, {1}}); });
        for (const auto key : {"sheenColorFactor", "anisotropyStrength", "transmissionFactor",
                               "ior", "thicknessFactor"})
            require(!detail::object_material_parameter_supported(key, nullptr),
                    "Feature-selecting/cached optical parameter was advertised as dynamic");
        rejects([&] { project_one("transmissionFactor", {MaterialParameterType::Scalar, {.5f}}); });
        MaterialData glass = source;
        glass.parameters["transmissionFactor"] = {MaterialParameterType::Scalar, {.8f}};
        glass.parameters["metallicFactor"] = {MaterialParameterType::Scalar, {0}};
        const std::array metal{MaterialParameterOverride{
            "default", "metallicFactor", {MaterialParameterType::Scalar, {1}}}};
        rejects([&] { detail::project_material_parameters(glass, nullptr, "default", metal); });
        SurfaceShaderDefinition surface;
        surface.physically_based = true;
        surface.uv_sets = {0};
        surface.parameters["stable_gain"] = {MaterialParameterType::Scalar, {1}};
        surface.parameters["stable_color"] = {MaterialParameterType::LinearColor3, {.1f, .2f, .3f}};
        auto custom = surface_material_defaults(surface);
        const auto custom_original = custom;
        const std::array custom_values{MaterialParameterOverride{
            "model:stable-slot", "stable_gain", {MaterialParameterType::Scalar, {2}}}};
        const auto changed = detail::project_material_parameters(
            custom, &surface, "model:stable-slot", custom_values);
        surface.labels["stable_gain"] = "A renamed visible label";
        require(changed.parameters.at("stable_gain").value[0] == 2 && custom == custom_original &&
                    detail::project_material_parameters(custom, &surface, "model:stable-slot",
                                                        custom_values) == changed,
                "Surface values changed the shared source or retargeted on label rename");
        surface.parameters.erase("stable_gain");
        rejects([&] {
            detail::project_material_parameters(custom, &surface, "model:stable-slot",
                                                custom_values);
        });
        std::cout << "Typed object material candidates, semantic/range limits, stable keys, "
                     "isolation and uniform-only shader projection passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
