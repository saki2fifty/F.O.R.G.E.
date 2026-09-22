#pragma once
#include <forge/material_asset.hpp>
namespace forge {
inline constexpr const char* surface_material_model = "forge.surface.v1";
// Logical surface interface. No native stage registers, byte offsets or device
// objects are authored. The selected compiler verifies the generated interface
// against actual reflection before a material can consume its cooked program.
struct SurfaceShaderDefinition {
    std::vector<unsigned> uv_sets;
    std::map<std::string, MaterialParameter> parameters;
    std::map<std::string, MaterialTextureSlot> textures;
    bool operator==(const SurfaceShaderDefinition&) const = default;
};
void validate_surface_definition(const SurfaceShaderDefinition&);
nlohmann::json surface_definition_document(const SurfaceShaderDefinition&);
SurfaceShaderDefinition surface_definition(const nlohmann::json&);
MaterialData surface_material_defaults(const SurfaceShaderDefinition&);
MaterialLayout surface_material_layout(const SurfaceShaderDefinition&);
void validate_surface_material(const MaterialData&, const SurfaceShaderDefinition&);
// Generated engine source, captured with the same immutable worker request as
// the authored function. The backend selects native/emulated bounded arrays.
std::string surface_shader_header(const SurfaceShaderDefinition&, bool emulated_samplers = false);
std::string surface_shader_wrapper(std::string_view source, std::string_view function,
                                   bool depth_only);
// Constant data is packed using copied, validated compiler reflection. Bindings
// remain named Diligent resources; these offsets never enter authored assets.
struct SurfaceParameterBinding {
    std::string name;
    unsigned offset{}, width{};
};
struct SurfaceBindingLayout {
    unsigned parameter_bytes{}, uv_bytes{}, sampler_count{};
    bool settings{};
    std::vector<SurfaceParameterBinding> parameters;
    std::vector<std::string> textures;
};
SurfaceBindingLayout surface_binding_layout(const SurfaceShaderDefinition&,
                                            const nlohmann::json& reflection);
std::vector<std::byte> surface_parameter_bytes(const MaterialData&, const SurfaceBindingLayout&);
std::vector<std::array<float, 4>> surface_uv_rows(const MaterialData&,
                                                  const SurfaceShaderDefinition&);
} // namespace forge
