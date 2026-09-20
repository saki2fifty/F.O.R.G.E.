#pragma once
#include "pbr_material.hpp"
namespace forge {
struct MaterialShaderTexture {
    std::string role, texture_variable, sampler_variable;
    unsigned uv_slot{};
    MaterialTextureSlot settings;
};
struct MaterialShader {
    std::string source;
    std::vector<std::array<float, 4>> uniforms;
    std::vector<MaterialShaderTexture> textures;
};
// Engine-owned shader binding adapter. Parameter values and UV transforms are
// uniforms, so changing a factor never creates a new shader permutation.
// Texture resources use native color-space views and independent binding samplers.
MaterialShader material_shader(const PbrMaterialProfile&, std::span<const unsigned> uv_sets);
} // namespace forge
