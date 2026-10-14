#pragma once
#include <forge/particle_effect.hpp>
#include <span>
namespace forge {
inline constexpr std::size_t particle_effect_byte_limit = 64 * 1024;
inline constexpr const char* particle_effect_importer = "forge.particles.cpu-mesh";
inline constexpr const char* particle_effect_legacy_recipe = "forge.particles.cpu-mesh.v1";
inline constexpr const char* particle_effect_recipe = "forge.particles.cpu-mesh.v2";
inline constexpr const char* particle_effect_cooked_format = "forge.particle-effect-cooked";
enum class ParticleLifetimeChannel { Size, Color };
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
    void enable_lifetime(ParticleLifetimeChannel, bool);
    AssetId insert_lifetime_stop(ParticleLifetimeChannel, double time);
    void remove_lifetime_stop(ParticleLifetimeChannel, AssetId);
};
ParticleEffectData decode_cooked_particle_effect(std::span<const std::byte>, AssetId);
} // namespace forge
