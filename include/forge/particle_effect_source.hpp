#pragma once
#include <forge/particle_effect.hpp>
#include <span>
namespace forge {
inline constexpr std::size_t particle_effect_byte_limit = 64 * 1024;
inline constexpr const char* particle_effect_importer = "forge.particles.cpu-mesh";
inline constexpr const char* particle_effect_recipe = "forge.particles.cpu-mesh.v1";
inline constexpr const char* particle_effect_cooked_format = "forge.particle-effect-cooked";
// Source/history retains opaque extensions; cooking projects only admitted values.
struct ParticleEffectSource {
    nlohmann::json document;
    static ParticleEffectSource create(AssetId);
    static ParticleEffectSource parse(std::span<const std::byte>);
    AssetId asset() const;
    ParticleEffectData compiled() const;
    void validate() const;
    std::vector<std::byte> cook() const;
};
ParticleEffectData decode_cooked_particle_effect(std::span<const std::byte>, AssetId);
} // namespace forge
