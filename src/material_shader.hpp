#pragma once
#include "pbr_material.hpp"
namespace forge {
inline constexpr const char* material_sampler_variable = "g_MaterialSamplers";
// Shader declaration lowering only; authored material data and SRB array identity
// are identical. Named elements use Diligent's emulated-array suffix contract.
enum class MaterialSamplerBinding { Array, NamedElements };
struct MaterialShaderTexture {
    std::string role, texture_variable;
    unsigned uv_slot{}, sampler_slot{};
    MaterialTextureSlot settings;
};
struct MaterialShader {
    std::string source;
    std::vector<std::array<float, 4>> uniforms;
    std::vector<MaterialShaderTexture> textures;
    std::vector<SamplerState> samplers;
};
// Engine-owned shader binding adapter. Parameter values and UV transforms are
// uniforms, so changing a factor never creates a new shader permutation.
// Texture resources use native color-space views and independent binding samplers.
MaterialShader material_shader(const PbrMaterialProfile&, std::span<const unsigned> uv_sets,
                               MaterialSamplerBinding = MaterialSamplerBinding::Array);
} // namespace forge
