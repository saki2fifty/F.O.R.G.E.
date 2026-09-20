#pragma once
#include <forge/asset_build.hpp>
namespace forge {
struct ShaderAsset {
    static constexpr const char* type = "shader";
};
// Stages admitted by the initial D3D12 / FXC shader-model5.1 compiler profile.
// Admission is not a claim that every renderer pass uses each stage.
enum class ShaderStage { Vertex, Pixel, Compute, Geometry, Hull, Domain };
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
};
struct ShaderData {
    std::string build_key, compiler_digest;
    bool row_major = true, compiler_debug = false;
    std::vector<ShaderStageData> stages;
    std::string layout_digest() const;
    std::size_t resident_bytes() const;
};
void validate_shader_reflection(const nlohmann::json&);
void validate_shader(const ShaderData&);
std::vector<std::byte> encode_shader(const ShaderData&);
ShaderData decode_shader(std::span<const std::byte>);
} // namespace forge
