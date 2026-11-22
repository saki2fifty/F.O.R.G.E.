#pragma once
#include <array>
#include <cstdint>
#include <forge/asset_ref.hpp>
#include <optional>
#include <span>
#include <string_view>
#include <vector>
namespace forge {
inline constexpr std::size_t particle_emitter_limit = 64, particle_scene_capacity = 8192;
inline constexpr std::size_t particle_scene_collision_capacity = 1024;
inline constexpr std::size_t particle_scene_trace_limit = 2048, particle_scene_query_limit = 8192;
inline constexpr unsigned particle_scene_birth_query_limit = 256;
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
struct ParticleSceneCollision {
    double radius = .05, restitution = .5, friction = .1;
    unsigned bounce_limit = 4;
    std::uint32_t mask = UINT32_MAX;
    bool operator==(const ParticleSceneCollision&) const = default;
};
void validate_particle_scene_collision(const ParticleSceneCollision&);
void validate_particle_scene_collision_2d(const ParticleSceneCollision&);
nlohmann::json encode_particle_scene_collision(const ParticleSceneCollision&);
ParticleSceneCollision decode_particle_scene_collision(const nlohmann::json&);
struct ParticleEffectData {
    std::uint32_t version = 1; // CPU1/2, GPU3/4, alpha5/6, camera7/8/9, trails10, collision11,
                               // weighted GPU12–15, GPU opacity16–19, GPU collision20–22,
                               // GPU trails23–25, ordered GPU26–37, Scene CPU38/39.
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
    std::optional<ParticleSceneCollision> scene_collision;
    bool operator==(const ParticleEffectData&) const = default;
};
namespace particle_detail {
enum ProfileFlag : unsigned {
    Gpu = 1,
    Graph = 2,
    CpuAlpha = 4,
    Opacity = 8,
    Billboard = 16,
    Trail = 32,
    Collision = 64,
    Weighted = 128,
    Sorted = 256,
    SceneCollision = 512,
    MaterialPalette = 1024,
    SceneCollision2D = 2048,
    SceneTrail = 4096,
    SceneImpact = 8192,
    SceneRest = 16384
};
struct ValueProfile {
    unsigned version;
    const char* name;
    unsigned flags;
};
// Immutable serialized value versions. Append new profiles; never reorder old ones.
inline constexpr std::array value_profiles{
    ValueProfile{1, "cpu_mesh1", 0},
    ValueProfile{2, "cpu_mesh2", 0},
    ValueProfile{3, "gpu_mesh1", Gpu},
    ValueProfile{4, "gpu_graph1", Gpu | Graph},
    ValueProfile{5, "cpu_alpha1", CpuAlpha},
    ValueProfile{6, "cpu_alpha2", CpuAlpha | Opacity},
    ValueProfile{7, "cpu_billboard1", CpuAlpha | Opacity | Billboard},
    ValueProfile{8, "gpu_billboard1", Gpu | Billboard},
    ValueProfile{9, "gpu_graph_billboard1", Gpu | Graph | Billboard},
    ValueProfile{10, "cpu_trail1", CpuAlpha | Opacity | Trail},
    ValueProfile{11, "cpu_collision1", CpuAlpha | Opacity | Collision},
    ValueProfile{12, "gpu_weighted1", Gpu | Weighted},
    ValueProfile{13, "gpu_graph_weighted1", Gpu | Graph | Weighted},
    ValueProfile{14, "gpu_weighted_billboard1", Gpu | Weighted | Billboard},
    ValueProfile{15, "gpu_graph_weighted_billboard1", Gpu | Graph | Weighted | Billboard},
    ValueProfile{16, "gpu_weighted_opacity1", Gpu | Weighted | Opacity},
    ValueProfile{17, "gpu_graph_weighted_opacity1", Gpu | Graph | Weighted | Opacity},
    ValueProfile{18, "gpu_weighted_opacity_billboard1", Gpu | Weighted | Opacity | Billboard},
    ValueProfile{19, "gpu_graph_weighted_opacity_billboard1",
                 Gpu | Graph | Weighted | Opacity | Billboard},
    ValueProfile{20, "gpu_collision1", Gpu | Collision},
    ValueProfile{21, "gpu_weighted_collision1", Gpu | Weighted | Collision},
    ValueProfile{22, "gpu_weighted_collision_opacity1", Gpu | Weighted | Collision | Opacity},
    ValueProfile{23, "gpu_trail1", Gpu | Trail},
    ValueProfile{24, "gpu_weighted_trail1", Gpu | Weighted | Trail},
    ValueProfile{25, "gpu_weighted_trail_opacity1", Gpu | Weighted | Trail | Opacity},
    ValueProfile{26, "gpu_sorted1", Gpu | Sorted},
    ValueProfile{27, "gpu_graph_sorted1", Gpu | Graph | Sorted},
    ValueProfile{28, "gpu_sorted_billboard1", Gpu | Sorted | Billboard},
    ValueProfile{29, "gpu_graph_sorted_billboard1", Gpu | Graph | Sorted | Billboard},
    ValueProfile{30, "gpu_sorted_collision1", Gpu | Sorted | Collision},
    ValueProfile{31, "gpu_sorted_trail1", Gpu | Sorted | Trail},
    ValueProfile{32, "gpu_sorted_opacity1", Gpu | Sorted | Opacity},
    ValueProfile{33, "gpu_graph_sorted_opacity1", Gpu | Graph | Sorted | Opacity},
    ValueProfile{34, "gpu_sorted_opacity_billboard1", Gpu | Sorted | Opacity | Billboard},
    ValueProfile{35, "gpu_graph_sorted_opacity_billboard1",
                 Gpu | Graph | Sorted | Opacity | Billboard},
    ValueProfile{36, "gpu_sorted_collision_opacity1", Gpu | Sorted | Collision | Opacity},
    ValueProfile{37, "gpu_sorted_trail_opacity1", Gpu | Sorted | Trail | Opacity},
    ValueProfile{38, "cpu_scene_collision1", CpuAlpha | Opacity | SceneCollision},
    ValueProfile{39, "cpu_scene_collision_billboard1",
                 CpuAlpha | Opacity | SceneCollision | Billboard},
    ValueProfile{40, "gpu_palette_sorted1", Gpu | Sorted | MaterialPalette},
    ValueProfile{41, "gpu_graph_palette_sorted1", Gpu | Graph | Sorted | MaterialPalette},
    ValueProfile{42, "gpu_palette_sorted_billboard1", Gpu | Sorted | MaterialPalette | Billboard},
    ValueProfile{43, "gpu_graph_palette_sorted_billboard1",
                 Gpu | Graph | Sorted | MaterialPalette | Billboard},
    ValueProfile{44, "gpu_palette_sorted_collision1", Gpu | Sorted | MaterialPalette | Collision},
    ValueProfile{45, "gpu_palette_sorted_trail1", Gpu | Sorted | MaterialPalette | Trail},
    ValueProfile{46, "gpu_palette_sorted_opacity1", Gpu | Sorted | MaterialPalette | Opacity},
    ValueProfile{47, "gpu_graph_palette_sorted_opacity1",
                 Gpu | Graph | Sorted | MaterialPalette | Opacity},
    ValueProfile{48, "gpu_palette_sorted_opacity_billboard1",
                 Gpu | Sorted | MaterialPalette | Opacity | Billboard},
    ValueProfile{49, "gpu_graph_palette_sorted_opacity_billboard1",
                 Gpu | Graph | Sorted | MaterialPalette | Opacity | Billboard},
    ValueProfile{50, "gpu_palette_sorted_collision_opacity1",
                 Gpu | Sorted | MaterialPalette | Collision | Opacity},
    ValueProfile{51, "gpu_palette_sorted_trail_opacity1",
                 Gpu | Sorted | MaterialPalette | Trail | Opacity},
    ValueProfile{52, "cpu_scene_collision_2d1",
                 CpuAlpha | Opacity | SceneCollision | SceneCollision2D},
    ValueProfile{53, "cpu_scene_collision_2d_billboard1",
                 CpuAlpha | Opacity | SceneCollision | SceneCollision2D | Billboard},
    ValueProfile{54, "cpu_scene_trail1", CpuAlpha | Opacity | SceneCollision | Trail | SceneTrail},
    ValueProfile{55, "cpu_scene_trail_2d1",
                 CpuAlpha | Opacity | SceneCollision | SceneCollision2D | Trail | SceneTrail},
    ValueProfile{56, "cpu_scene_impact1", CpuAlpha | Opacity | SceneCollision | SceneImpact},
    ValueProfile{57, "cpu_scene_impact_billboard1",
                 CpuAlpha | Opacity | SceneCollision | Billboard | SceneImpact},
    ValueProfile{58, "cpu_scene_impact_2d1",
                 CpuAlpha | Opacity | SceneCollision | SceneCollision2D | SceneImpact},
    ValueProfile{59, "cpu_scene_impact_2d_billboard1",
                 CpuAlpha | Opacity | SceneCollision | SceneCollision2D | Billboard | SceneImpact},
    ValueProfile{60, "cpu_scene_impact_trail1",
                 CpuAlpha | Opacity | SceneCollision | Trail | SceneTrail | SceneImpact},
    ValueProfile{61, "cpu_scene_impact_trail_2d1",
                 CpuAlpha | Opacity | SceneCollision | SceneCollision2D | Trail | SceneTrail |
                     SceneImpact},
    ValueProfile{62, "cpu_scene_rest1", CpuAlpha | Opacity | SceneCollision | SceneRest},
    ValueProfile{63, "cpu_scene_rest_billboard1",
                 CpuAlpha | Opacity | SceneCollision | SceneRest | Billboard},
    ValueProfile{64, "cpu_scene_rest_2d1",
                 CpuAlpha | Opacity | SceneCollision | SceneRest | SceneCollision2D},
    ValueProfile{65, "cpu_scene_rest_2d_billboard1",
                 CpuAlpha | Opacity | SceneCollision | SceneRest | SceneCollision2D | Billboard},
    ValueProfile{66, "cpu_scene_rest_trail1",
                 CpuAlpha | Opacity | SceneCollision | SceneRest | Trail | SceneTrail},
    ValueProfile{67, "cpu_scene_rest_trail_2d1",
                 CpuAlpha | Opacity | SceneCollision | SceneRest | SceneCollision2D | Trail |
                     SceneTrail},
    ValueProfile{68, "cpu_scene_rest_impact1",
                 CpuAlpha | Opacity | SceneCollision | SceneRest | SceneImpact},
    ValueProfile{69, "cpu_scene_rest_impact_billboard1",
                 CpuAlpha | Opacity | SceneCollision | SceneRest | Billboard | SceneImpact},
    ValueProfile{70, "cpu_scene_rest_impact_2d1",
                 CpuAlpha | Opacity | SceneCollision | SceneRest | SceneCollision2D | SceneImpact},
    ValueProfile{71, "cpu_scene_rest_impact_2d_billboard1",
                 CpuAlpha | Opacity | SceneCollision | SceneRest | SceneCollision2D | Billboard |
                     SceneImpact},
    ValueProfile{72, "cpu_scene_rest_impact_trail1",
                 CpuAlpha | Opacity | SceneCollision | SceneRest | Trail | SceneTrail |
                     SceneImpact},
    ValueProfile{73, "cpu_scene_rest_impact_trail_2d1",
                 CpuAlpha | Opacity | SceneCollision | SceneRest | SceneCollision2D | Trail |
                     SceneTrail | SceneImpact}};
static_assert([] {
    for (unsigned i = 0; i < value_profiles.size(); ++i)
        if (value_profiles[i].version != i + 1)
            return false;
    return true;
}());
constexpr const ValueProfile* value_profile(std::uint64_t version) {
    return version && version <= value_profiles.size() ? &value_profiles[version - 1] : nullptr;
}
constexpr const ValueProfile* value_profile(std::string_view name) {
    for (const auto& profile : value_profiles)
        if (name == profile.name)
            return &profile;
    return nullptr;
}
constexpr bool has(const ParticleEffectData& data, unsigned flags) {
    const auto* profile = value_profile(data.version);
    return profile && (profile->flags & flags) == flags;
}
} // namespace particle_detail
constexpr bool particle_uses_gpu(const ParticleEffectData& data) {
    return particle_detail::has(data, particle_detail::Gpu);
}
constexpr bool particle_uses_graph(const ParticleEffectData& data) {
    return particle_detail::has(data, particle_detail::Graph);
}
constexpr bool particle_uses_weighted_alpha(const ParticleEffectData& data) {
    return particle_detail::has(data, particle_detail::Weighted);
}
constexpr bool particle_uses_sorted_alpha(const ParticleEffectData& data) {
    return particle_detail::has(data, particle_detail::CpuAlpha);
}
constexpr bool particle_uses_gpu_sorted_alpha(const ParticleEffectData& data) {
    return particle_detail::has(data, particle_detail::Gpu | particle_detail::Sorted);
}
constexpr bool particle_uses_gpu_material_palette(const ParticleEffectData& data) {
    return particle_detail::has(data, particle_detail::Gpu | particle_detail::Sorted |
                                          particle_detail::MaterialPalette);
}
constexpr bool particle_uses_gpu_opacity(const ParticleEffectData& data) {
    return particle_detail::has(data, particle_detail::Gpu | particle_detail::Opacity);
}
constexpr bool particle_uses_opacity(const ParticleEffectData& data) {
    return particle_detail::has(data, particle_detail::Opacity);
}
constexpr bool particle_uses_gpu_trails(const ParticleEffectData& data) {
    return particle_detail::has(data, particle_detail::Gpu | particle_detail::Trail);
}
constexpr bool particle_uses_trails(const ParticleEffectData& data) {
    return particle_detail::has(data, particle_detail::Trail);
}
constexpr bool particle_uses_collision(const ParticleEffectData& data) {
    return particle_detail::has(data, particle_detail::Collision);
}
constexpr bool particle_uses_scene_collision(const ParticleEffectData& data) {
    return particle_detail::has(data, particle_detail::SceneCollision);
}
constexpr bool particle_uses_scene_collision_2d(const ParticleEffectData& data) {
    return particle_detail::has(data, particle_detail::SceneCollision2D);
}
constexpr bool particle_uses_scene_trails(const ParticleEffectData& data) {
    return particle_detail::has(data, particle_detail::SceneTrail);
}
constexpr bool particle_uses_scene_resting(const ParticleEffectData& data) {
    return particle_detail::has(data, particle_detail::SceneRest);
}
constexpr bool particle_uses_scene_impacts(const ParticleEffectData& data) {
    return particle_detail::has(data, particle_detail::SceneImpact);
}
constexpr bool particle_uses_billboards(const ParticleEffectData& data) {
    return particle_detail::has(data, particle_detail::Billboard);
}
// Reserved draw segments, including tails after a head reaches its lifetime.
std::size_t particle_instance_capacity(const ParticleEffectData&);
struct ParticleSample {
    // Emitter-scoped transient ordinal; never an entity UUID or saved handle.
    std::uint64_t birth = 0;
    std::array<double, 3> position{}, velocity{};
    std::array<float, 3> color{};
    double age = 0, size = 0;
    double opacity = 1;
    bool operator==(const ParticleSample&) const = default;
};
struct ParticlePathRange {
    std::uint64_t birth;
    std::size_t first, count;
    bool operator==(const ParticlePathRange&) const = default;
};
// Flat detached ribbon geometry; the runtime owns any historical retention.
struct ParticlePathSamples {
    std::vector<ParticlePathRange> paths;
    std::vector<ParticleSample> points;
    std::size_t segment_count() const { return points.size() - paths.size(); }
    bool operator==(const ParticlePathSamples&) const = default;
};
struct ParticleWorldPaths {
    ParticlePathSamples paths;
    // Frozen birth X-column magnitudes, one per path. Point velocities are zero:
    // these are visual polyline samples, not instantaneous physics observations.
    std::vector<double> width_scales;
    bool operator==(const ParticleWorldPaths&) const = default;
};
// Copied transient World observation; neither an authored value nor saved body.
// Position/velocity are world-space. The row-major birth basis has no translation.
struct ParticleWorldSample {
    ParticleSample sample;
    std::array<double, 9> basis{1, 0, 0, 0, 1, 0, 0, 0, 1};
    bool operator==(const ParticleWorldSample&) const = default;
};
void validate_particle_world_samples(std::span<const ParticleWorldSample>);
void validate_particle_world_samples(const ParticleEffectData&, double time,
                                     std::span<const ParticleWorldSample>);
nlohmann::json encode_particle_world_samples(std::span<const ParticleWorldSample>);
std::vector<ParticleWorldSample> decode_particle_world_samples(const nlohmann::json&);
void validate_particle_world_paths(const ParticleWorldPaths&);
void validate_particle_world_paths(const ParticleEffectData&, double time,
                                   const ParticleWorldPaths&,
                                   std::span<const ParticleWorldSample> heads = {});
nlohmann::json encode_particle_world_paths(const ParticleWorldPaths&);
ParticleWorldPaths decode_particle_world_paths(const nlohmann::json&);
std::size_t particle_world_capture_birth_count(std::span<const ParticleWorldSample>,
                                               const ParticleWorldPaths* = nullptr);
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
