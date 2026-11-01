#pragma once
#include <forge/asset_build.hpp>
#include <forge/surface_shader.hpp>
#include <optional>
namespace forge {
// Stages admitted by the initial D3D12 / FXC shader-model5.1 compiler profile.
// Admission is not a claim that every renderer pass uses each stage.
enum class ShaderStage { Vertex, Pixel, Compute, Geometry, Hull, Domain };
inline constexpr unsigned current_surface_wrapper_version = 14;
enum class ShaderEntryRole {
    Program,
    SurfaceColor,
    SurfaceDepth,
    SurfaceTemporalColor,
    SurfaceIndirectColor,
    SurfaceTemporalIndirectColor,
    SurfaceReflectionColor,
    SurfaceTemporalReflectionColor,
    SurfaceNormalColor,
    SurfaceTemporalNormalColor,
    SurfaceAreaColor,
    SurfaceAreaTemporalColor,
    SurfaceAreaIndirectColor,
    SurfaceAreaTemporalIndirectColor,
    SurfaceAreaReflectionColor,
    SurfaceAreaTemporalReflectionColor,
    SurfaceAreaNormalColor,
    SurfaceAreaTemporalNormalColor,
    SurfaceWeightedColor,
    SurfaceAreaWeightedColor,
    SurfaceSpriteColor,
    SurfaceSpriteMipColor
};
inline constexpr bool sprite_surface_role(ShaderEntryRole role) {
    return role == ShaderEntryRole::SurfaceSpriteColor ||
           role == ShaderEntryRole::SurfaceSpriteMipColor;
}
inline constexpr bool weighted_surface_role(ShaderEntryRole role) {
    return role == ShaderEntryRole::SurfaceWeightedColor ||
           role == ShaderEntryRole::SurfaceAreaWeightedColor;
}
inline constexpr bool area_surface_role(ShaderEntryRole role) {
    return (role >= ShaderEntryRole::SurfaceAreaColor &&
            role <= ShaderEntryRole::SurfaceAreaTemporalNormalColor) ||
           role == ShaderEntryRole::SurfaceAreaWeightedColor;
}
inline constexpr ShaderEntryRole ordinary_surface_role(ShaderEntryRole role) {
    switch (role) {
    case ShaderEntryRole::SurfaceAreaWeightedColor:
        return ShaderEntryRole::SurfaceWeightedColor;
    case ShaderEntryRole::SurfaceAreaColor:
        return ShaderEntryRole::SurfaceColor;
    case ShaderEntryRole::SurfaceAreaTemporalColor:
        return ShaderEntryRole::SurfaceTemporalColor;
    case ShaderEntryRole::SurfaceAreaIndirectColor:
        return ShaderEntryRole::SurfaceIndirectColor;
    case ShaderEntryRole::SurfaceAreaTemporalIndirectColor:
        return ShaderEntryRole::SurfaceTemporalIndirectColor;
    case ShaderEntryRole::SurfaceAreaReflectionColor:
        return ShaderEntryRole::SurfaceReflectionColor;
    case ShaderEntryRole::SurfaceAreaTemporalReflectionColor:
        return ShaderEntryRole::SurfaceTemporalReflectionColor;
    case ShaderEntryRole::SurfaceAreaNormalColor:
        return ShaderEntryRole::SurfaceNormalColor;
    case ShaderEntryRole::SurfaceAreaTemporalNormalColor:
        return ShaderEntryRole::SurfaceTemporalNormalColor;
    default:
        return role;
    }
}
inline constexpr bool temporal_surface_role(ShaderEntryRole role) {
    role = ordinary_surface_role(role);
    return role == ShaderEntryRole::SurfaceTemporalColor ||
           role == ShaderEntryRole::SurfaceTemporalIndirectColor ||
           role == ShaderEntryRole::SurfaceTemporalReflectionColor ||
           role == ShaderEntryRole::SurfaceTemporalNormalColor;
}
inline constexpr bool indirect_surface_role(ShaderEntryRole role) {
    role = ordinary_surface_role(role);
    return role == ShaderEntryRole::SurfaceIndirectColor ||
           role == ShaderEntryRole::SurfaceTemporalIndirectColor ||
           role == ShaderEntryRole::SurfaceReflectionColor ||
           role == ShaderEntryRole::SurfaceTemporalReflectionColor ||
           role == ShaderEntryRole::SurfaceNormalColor ||
           role == ShaderEntryRole::SurfaceTemporalNormalColor;
}
inline constexpr bool normal_surface_role(ShaderEntryRole role) {
    role = ordinary_surface_role(role);
    return role == ShaderEntryRole::SurfaceNormalColor ||
           role == ShaderEntryRole::SurfaceTemporalNormalColor;
}
inline constexpr bool reflection_surface_role(ShaderEntryRole role) {
    role = ordinary_surface_role(role);
    return role == ShaderEntryRole::SurfaceReflectionColor ||
           role == ShaderEntryRole::SurfaceTemporalReflectionColor;
}
inline constexpr ShaderEntryRole surface_color_role(bool temporal, bool indirect,
                                                    bool reflection = false, bool normal = false,
                                                    bool area = false, bool weighted = false) {
    if (weighted)
        return area ? ShaderEntryRole::SurfaceAreaWeightedColor
                    : ShaderEntryRole::SurfaceWeightedColor;
    if (area) {
        if (reflection)
            return temporal ? ShaderEntryRole::SurfaceAreaTemporalReflectionColor
                            : ShaderEntryRole::SurfaceAreaReflectionColor;
        if (normal)
            return temporal ? ShaderEntryRole::SurfaceAreaTemporalNormalColor
                            : ShaderEntryRole::SurfaceAreaNormalColor;
        return indirect ? (temporal ? ShaderEntryRole::SurfaceAreaTemporalIndirectColor
                                    : ShaderEntryRole::SurfaceAreaIndirectColor)
                        : (temporal ? ShaderEntryRole::SurfaceAreaTemporalColor
                                    : ShaderEntryRole::SurfaceAreaColor);
    }
    if (reflection)
        return temporal ? ShaderEntryRole::SurfaceTemporalReflectionColor
                        : ShaderEntryRole::SurfaceReflectionColor;
    if (normal)
        return temporal ? ShaderEntryRole::SurfaceTemporalNormalColor
                        : ShaderEntryRole::SurfaceNormalColor;
    return indirect
               ? (temporal ? ShaderEntryRole::SurfaceTemporalIndirectColor
                           : ShaderEntryRole::SurfaceIndirectColor)
               : (temporal ? ShaderEntryRole::SurfaceTemporalColor : ShaderEntryRole::SurfaceColor);
}
const char* shader_entry_role_name(ShaderEntryRole);
ShaderEntryRole shader_entry_role(std::string_view);
const char* surface_shader_entry(ShaderEntryRole);
// Versioned immutable entry set; definition validation remains with its owner.
std::span<const ShaderEntryRole> surface_shader_roles(const SurfaceShaderDefinition&,
                                                      unsigned wrapper_version);
struct ShaderStageKey {
    ShaderStage stage;
    ShaderEntryRole role;
    ShaderStageKey(ShaderStage stage_, ShaderEntryRole role_ = ShaderEntryRole::Program)
        : stage(stage_), role(role_) {}
    auto operator<=>(const ShaderStageKey&) const = default;
};
const char* shader_stage_name(ShaderStage);
ShaderStage shader_stage(std::string_view);
struct ShaderEntry {
    ShaderStage stage = ShaderStage::Vertex;
    std::string source, entry = "main";
    bool operator==(const ShaderEntry&) const = default;
};
struct ShaderProgramSource {
    std::vector<ShaderEntry> stages;
    std::map<std::string, std::string> defines;
    std::map<std::string, std::vector<std::string>> permutations;
    bool row_major = true;
    unsigned optimization = 2;
    std::optional<SurfaceShaderDefinition> surface;
    bool operator==(const ShaderProgramSource&) const = default;
};
// The original JSON remains the authored asset/draft so unknown fields survive
// save. This projection supplies only the supported compiler inputs.
ShaderProgramSource shader_program_source(const nlohmann::json& document);
void validate_shader_program(const ShaderProgramSource&);
std::map<std::string, std::string>
select_shader_permutation(const ShaderProgramSource&,
                          const std::map<std::string, std::string>& selection);
// Detached immutable compiler input. Keys are canonical virtual filenames;
// a native source factory may read only this set, never the host filesystem.
using ShaderSources = std::map<std::string, std::string>;
void validate_shader_sources(const ShaderSources&);
// Exact FORGE adapter sources/toolchain/configuration, independent of asset identity.
std::string shader_compiler_revision();
// Logical shader identity, current pathname and source timestamps are excluded
// from build identity. Exact compiler bytes/profile and all captured sources count.
AssetBuildInput shader_build_input(const ShaderProgramSource&, const ShaderSources&,
                                   const std::map<std::string, std::string>& permutation,
                                   std::string compiler_digest, bool compiler_debug);
struct ShaderStageData {
    ShaderStage stage = ShaderStage::Vertex;
    std::string entry;
    std::vector<std::byte> bytecode;
    // Copied native reflection; no pointers or compiler objects cross workers.
    nlohmann::json reflection;
    ShaderEntryRole role = ShaderEntryRole::Program;
};
struct ShaderData {
    std::string build_key, compiler_digest;
    bool row_major = true, compiler_debug = false;
    std::vector<ShaderStageData> stages;
    std::optional<SurfaceShaderDefinition> surface;
    // Engine-generated wrapper contract, independent of optimized resources.
    // 1 is legacy; 2 adds fog; 3 adds a separate color/motion entry.
    // This is engine wrapper capability, not authored HLSL intent.
    unsigned surface_wrapper_version = 1;
    std::string layout_digest() const;
    std::size_t resident_bytes() const;
};
void validate_shader_reflection(const nlohmann::json&);
void validate_shader(const ShaderData&);
std::vector<std::byte> encode_shader(const ShaderData&);
ShaderData decode_shader(std::span<const std::byte>);
} // namespace forge
