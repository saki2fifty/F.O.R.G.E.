#pragma once
#include <forge/particle_effect.hpp>
#include <span>
namespace forge {
inline constexpr std::size_t particle_effect_byte_limit = 64 * 1024;
inline constexpr unsigned particle_effect_cooked_version = 3;
inline constexpr unsigned particle_gpu_profile_version = 1;
// This importer ID is persisted in source settings; backend selection never changes it.
inline constexpr const char* particle_effect_importer = "forge.particles.cpu-mesh";
inline constexpr const char* particle_effect_legacy_recipe = "forge.particles.cpu-mesh.v1";
inline constexpr const char* particle_effect_extended_recipe = "forge.particles.cpu-mesh.v2";
inline constexpr const char* particle_effect_recipe = "forge.particles.mesh.v3";
inline constexpr const char* particle_effect_cooked_format = "forge.particle-effect-cooked";
enum class ParticleLifetimeChannel { Size, Color };
enum class ParticleEvaluation { Cpu, Gpu, Graph };
// Source/history retains opaque extensions; cooking projects only admitted values.
struct ParticleEffectSource {
    nlohmann::json document;
    static ParticleEffectSource create(AssetId);
    static ParticleEffectSource parse(std::span<const std::byte>);
    AssetId asset() const;
    ParticleEffectData compiled() const;
    void validate() const;
    std::vector<std::byte> cook() const;
    // Each operation commits a validated candidate. Local stop UUIDs are source-only,
    // scoped to this asset, and retain their identity through document history/copy.
    void promote();
    void set_evaluation(ParticleEvaluation);
    void set_graph_program(const ParticleGraphProgram&, bool activate = true);
    std::optional<ParticleGraphProgram> graph_program() const;
    void enable_lifetime(ParticleLifetimeChannel, bool);
    AssetId insert_lifetime_stop(ParticleLifetimeChannel, double time);
    void remove_lifetime_stop(ParticleLifetimeChannel, AssetId);
};
ParticleEffectData decode_cooked_particle_effect(std::span<const std::byte>, AssetId,
                                                 unsigned expected_envelope = 0);
} // namespace forge
