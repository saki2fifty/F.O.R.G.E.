#pragma once
#include <array>
#include <forge/asset_ref.hpp>
#include <optional>
#include <span>
#include <vector>
namespace forge {
inline constexpr std::size_t particle_emitter_limit = 64, particle_scene_capacity = 8192;
// Source/admission values, not an installed gameplay implementation API.
// Runtime emitter components will reference this logical asset through AssetRef.
enum class ParticleEmission : std::uint32_t { Continuous, Burst };
enum class ParticleSpawnShape : std::uint32_t { Point, Box, Sphere, Disc };
struct ParticleSizeStop {
    double time = 0, value = .1; // Normalized lifetime, meters.
    bool hold = false;           // Outgoing segment: Step or Linear.
    bool operator==(const ParticleSizeStop&) const = default;
};
struct ParticleColorStop {
    double time = 0;
    std::array<float, 3> value{1, .6f, .1f}; // Linear RGB.
    bool hold = false;
    bool operator==(const ParticleColorStop&) const = default;
};
struct ParticleOpacityStop {
    double time = 0, value = 1; // Normalized lifetime and opacity multiplier.
    bool hold = false;
    bool operator==(const ParticleOpacityStop&) const = default;
};
inline constexpr unsigned particle_graph_profile_version = 1;
struct ParticleGraphProgram {
    AssetRef<ShaderAsset> shader;
    std::array<double, 3> extent{1, 1, 1}; // Symmetric emitter-local bounds, meters.
    double maximum_size = 1;               // Before per-emitter size modulation.
    bool operator==(const ParticleGraphProgram&) const = default;
};
void validate_particle_graph_bounds(const std::array<double, 3>& extent, double maximum_size);
void validate_particle_graph_program(const ParticleGraphProgram&);
ParticleGraphProgram decode_particle_graph_program(const nlohmann::json&);
nlohmann::json encode_particle_graph_program(const ParticleGraphProgram&);
struct ParticleTrailSettings {
    double duration = .5; // Seconds of the emitter-local analytic path.
    unsigned segments = 8;
    bool operator==(const ParticleTrailSettings&) const = default;
};
void validate_particle_trail_settings(const ParticleTrailSettings&);
ParticleTrailSettings decode_particle_trail_settings(const nlohmann::json&);
nlohmann::json encode_particle_trail_settings(const ParticleTrailSettings&);
struct ParticleCollisionPlane {
    std::array<double, 3> normal{0, 1, 0};
    double offset = -1; // Signed meters along the normalized emitter-local normal.
    double radius = 0, restitution = .5, friction = .1;
    unsigned bounce_limit = 4; // The next incoming contact kills; zero kills at first contact.
    bool operator==(const ParticleCollisionPlane&) const = default;
};
void validate_particle_collision_plane(const ParticleCollisionPlane&);
ParticleCollisionPlane decode_particle_collision_plane(const nlohmann::json&);
nlohmann::json encode_particle_collision_plane(const ParticleCollisionPlane&);
struct ParticleEffectData {
    std::uint32_t version = 1; // CPU1/2, GPU3/4, alpha5/6, camera7/8/9, trails10, collision11,
                               // weighted GPU12–15, GPU opacity16–19, GPU collision20–22,
                               // GPU trails23–25.
    AssetRef<MeshAsset> mesh;
    AssetRef<MaterialAsset> material;
    ParticleEmission emission = ParticleEmission::Continuous;
    std::uint32_t capacity = 256, burst_count = 32;
    double spawn_rate = 32, lifetime = 2;
    std::array<double, 3> direction{0, 1, 0}, acceleration{0, -2, 0};
    double cone_angle = 15, speed_min = 1, speed_max = 2;
    double size_start = .1, size_end = .02;
    std::array<float, 3> color_start{1, .6f, .1f}, color_end{.3f, .1f, .02f};
    ParticleSpawnShape shape = ParticleSpawnShape::Point;
    std::array<double, 3> shape_size{1, 1, 1}; // Box full XYZ dimensions.
    double shape_radius = 1, drag = 0;         // Sphere/Disc meters, reciprocal seconds.
    std::vector<ParticleSizeStop> size_curve;
    std::vector<ParticleColorStop> color_gradient;
    std::optional<ParticleGraphProgram> program;
    double opacity_start = 1, opacity_end = 1;
    std::vector<ParticleOpacityStop> opacity_curve;
    std::optional<ParticleTrailSettings> trail;
    std::optional<ParticleCollisionPlane> collision;
    bool operator==(const ParticleEffectData&) const = default;
};
constexpr bool particle_uses_gpu(const ParticleEffectData& data) {
    return data.version == 3 || data.version == 4 || data.version == 8 || data.version == 9 ||
           (data.version >= 12 && data.version <= 25);
}
constexpr bool particle_uses_graph(const ParticleEffectData& data) {
    return data.version == 4 || data.version == 9 || data.version == 13 || data.version == 15 ||
           data.version == 17 || data.version == 19;
}
constexpr bool particle_uses_weighted_alpha(const ParticleEffectData& data) {
    return (data.version >= 12 && data.version <= 19) || data.version == 21 || data.version == 22 ||
           data.version == 24 || data.version == 25;
}
constexpr bool particle_uses_sorted_alpha(const ParticleEffectData& data) {
    return data.version == 5 || data.version == 6 || data.version == 7 || data.version == 10 ||
           data.version == 11;
}
constexpr bool particle_uses_gpu_opacity(const ParticleEffectData& data) {
    return (data.version >= 16 && data.version <= 19) || data.version == 22 || data.version == 25;
}
constexpr bool particle_uses_opacity(const ParticleEffectData& data) {
    return data.version == 6 || data.version == 7 || data.version == 10 || data.version == 11 ||
           particle_uses_gpu_opacity(data);
}
constexpr bool particle_uses_gpu_trails(const ParticleEffectData& data) {
    return data.version >= 23 && data.version <= 25;
}
constexpr bool particle_uses_trails(const ParticleEffectData& data) {
    return data.version == 10 || particle_uses_gpu_trails(data);
}
constexpr bool particle_uses_collision(const ParticleEffectData& data) {
    return data.version == 11 || (data.version >= 20 && data.version <= 22);
}
// Reserved draw segments, including tails after a head reaches its lifetime.
std::size_t particle_instance_capacity(const ParticleEffectData&);
constexpr bool particle_uses_billboards(const ParticleEffectData& data) {
    return data.version == 7 || data.version == 8 || data.version == 9 || data.version == 14 ||
           data.version == 15 || data.version == 18 || data.version == 19;
}
struct ParticleSample {
    // Emitter-scoped transient ordinal; never an entity UUID or saved handle.
    std::uint64_t birth = 0;
    std::array<double, 3> position{}, velocity{};
    std::array<float, 3> color{};
    double age = 0, size = 0;
    double opacity = 1;
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
// Per-emitter appearance, with no effect copy, clock or trajectory mutation.
void validate_particle_instance_parameters(double size, std::array<float, 3> tint);
void modulate_particle_samples(std::span<ParticleSample>, double size, std::array<float, 3> tint);
} // namespace forge
