#pragma once
#include <array>
#include <forge/asset_ref.hpp>
#include <vector>
namespace forge {
inline constexpr std::size_t particle_emitter_limit = 64, particle_scene_capacity = 8192;
// CPU source/admission values, not an installed gameplay implementation API.
// Runtime emitter components will reference this logical asset through AssetRef.
enum class ParticleEmission : std::uint32_t { Continuous, Burst };
struct ParticleEffectData {
    AssetRef<MeshAsset> mesh;
    AssetRef<MaterialAsset> material;
    ParticleEmission emission = ParticleEmission::Continuous;
    std::uint32_t capacity = 256, burst_count = 32;
    double spawn_rate = 32, lifetime = 2;
    std::array<double, 3> direction{0, 1, 0}, acceleration{0, -2, 0};
    double cone_angle = 15, speed_min = 1, speed_max = 2;
    double size_start = .1, size_end = .02;
    std::array<float, 3> color_start{1, .6f, .1f}, color_end{.3f, .1f, .02f};
    bool operator==(const ParticleEffectData&) const = default;
};
struct ParticleSample {
    // Emitter-scoped transient ordinal; never an entity UUID or saved handle.
    std::uint64_t birth = 0;
    std::array<double, 3> position{}, velocity{};
    std::array<float, 3> color{};
    double age = 0, size = 0;
    bool operator==(const ParticleSample&) const = default;
};
void validate_particle_effect(const ParticleEffectData&);
ParticleEffectData decode_particle_effect(const nlohmann::json&);
nlohmann::json encode_particle_effect(const ParticleEffectData&);
// Pure local-space visual evaluation. No clock, world, file, device or callback.
// Budget rejection is explicit; successful results are never silently truncated.
// Continuous births occur at integer emission phases 1,2,...; lifetime uses the
// same phase units to keep boundary inclusion independent of rounded birth times.
std::vector<ParticleSample> sample_particle_effect(const ParticleEffectData&, double seconds,
                                                   std::uint32_t seed, std::size_t budget = 1024);
} // namespace forge
