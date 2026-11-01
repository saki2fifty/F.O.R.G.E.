#pragma once
#include <forge/material_asset.hpp>
#include <optional>
namespace forge {
enum class ShaderEntryRole;
inline constexpr const char* surface_material_model = "forge.surface.v1";
inline constexpr const char* surface_sprite_color_entry = "ForgeSurfaceSpriteColor";
inline constexpr const char* surface_sprite_mip_entry = "ForgeSurfaceSpriteMipColor";
// Logical surface interface. No native stage registers, byte offsets or device
// objects are authored. The selected compiler verifies the generated interface
// against actual reflection before a material can consume its cooked program.
struct SurfaceShaderDefinition {
    std::vector<unsigned> uv_sets;
    std::map<std::string, MaterialParameter> parameters;
    std::map<std::string, MaterialTextureSlot> textures;
    bool physically_based = false;
    // Display metadata; binding names remain stable and independent of labels.
    std::map<std::string, std::string> labels;
    // Explicit Sprite domain and its stable primary-image binding key.
    // An absent value retains the existing Mesh surface contract.
    std::optional<std::string> sprite_image = {};
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
std::string surface_shader_header(const SurfaceShaderDefinition&, bool emulated_samplers = false,
                                  bool temporal = false);
std::string sprite_surface_shader_header(const SurfaceShaderDefinition&, bool frame_mips,
                                         bool emulated_samplers = false);
std::string sprite_surface_shader_wrapper(std::string_view source, std::string_view function,
                                          bool frame_mips);
std::string surface_shader_wrapper(std::string_view source, std::string_view function,
                                   bool depth_only, bool indirect_output = false,
                                   bool reflection_output = false, bool normal_output = false);
// Extend the engine-generated color wrapper; the authored function keeps its
// ordinary surface input contract. The alternate header is an owned virtual file.
std::string temporal_surface_wrapper(std::string color_wrapper);
// The shared generated helper has an explicit indirect output parameter.
// Built-in PBR graphs supply environment radiance; final-color functions supply zero.
std::string indirect_surface_wrapper(std::string helper, bool temporal, bool reflection = false,
                                     bool normal = false);
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
                                            const nlohmann::json& reflection,
                                            bool area_lights = false, bool projected_decals = false,
                                            bool projected_surface_channels = false);
// Explicit stage/capability dispatch keeps packet compatibility with its owner.
SurfaceBindingLayout surface_binding_layout(const SurfaceShaderDefinition&,
                                            const nlohmann::json& reflection, ShaderEntryRole,
                                            unsigned wrapper_version);
std::vector<std::byte> surface_parameter_bytes(const MaterialData&, const SurfaceBindingLayout&);
std::vector<std::array<float, 4>> surface_uv_rows(const MaterialData&,
                                                  const SurfaceShaderDefinition&);
} // namespace forge
