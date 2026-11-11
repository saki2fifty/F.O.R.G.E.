#pragma once
#include "pbr_material.hpp"
#include <optional>
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
struct MaterialParameterUniform {
    std::string key;
    MaterialParameterType type;
    unsigned offset, width; // Bytes, then float lanes.
};
// Affine UV rows for an admitted MaterialTextureSlot; fourth lanes are reserved
// for each consumer's UV index or flags. Sampling/resource ownership is unchanged.
std::array<std::array<float, 4>, 2> material_texture_transform_rows(const MaterialTextureSlot&);
// One shared packing authority for built-in shader emission and object values.
// Input is the admitted canonical PBR value set, including model defaults.
std::vector<MaterialParameterUniform> material_parameter_uniforms(const MaterialData&);
// Derived Sprite frames have independent mip chains, so their native sampler
// clamps to that frame and admits the full chain regardless of source LOD caps.
SamplerState sprite_frame_sampler(SamplerState, TextureFilter);
// Engine-owned shader binding adapter. Parameter values and UV transforms are
// uniforms, so changing a factor never creates a new shader permutation.
// Texture resources use native color-space views and independent binding samplers.
// Graph wrappers may specialize their fixed engine defaults; authored graph
// parameters remain in the separate surface uniform block.
MaterialShader material_shader(const PbrMaterialProfile&, std::span<const unsigned> uv_sets,
                               MaterialSamplerBinding = MaterialSamplerBinding::Array,
                               bool sprite_profile = false,
                               std::optional<TextureFilter> sprite_mips = {},
                               bool fixed_values = false);
} // namespace forge
