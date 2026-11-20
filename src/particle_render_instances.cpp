#include "particle_render_instances.hpp"
#include "particle_birth.hpp"
#include "pbr_material.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace forge {
namespace {
ParticleRenderInstance instance(const ParticleSample& sample, const AffineTransform& emitter) {
    AffineTransform local;
    for (unsigned row = 0; row < 3; ++row) {
        for (unsigned column = 0; column < 3; ++column)
            local.m[row * 4 + column] = row == column ? sample.size : 0;
        local.m[row * 4 + 3] = sample.position[row];
    }
    auto world = emitter * local;
    for (const auto value : world.m)
        if (!std::isfinite(value))
            throw std::runtime_error("Particle instance has a nonfinite world transform");
    return {sample.birth, world, sample.color, sample.opacity};
}
} // namespace
void validate_particle_mesh_profile(const MeshResourceData& resource) {
    const auto& mesh = resource.mesh;
    if (mesh.lods.size() != 1 || mesh.lods.front().parts.size() != 1 || mesh.material_slots != 1 ||
        resource.materials.size() != 1 || !mesh.morph_names.empty())
        throw std::runtime_error(
            "Mesh particles require one static triangle part, one LOD and one material slot");
    const auto& part = mesh.lods.front().parts.front();
    if (part.topology != MeshTopology::Triangles || !part.morph_targets.empty() ||
        !part.joint_palette.empty())
        throw std::runtime_error("Mesh particles do not support skinning, morphs, points or lines");
    for (const auto& stream : part.streams)
        if (stream.semantic.starts_with("JOINTS") || stream.semantic.starts_with("WEIGHTS"))
            throw std::runtime_error("Mesh particles do not support skinned vertex channels");
    if (resource.model)
        for (const auto& node : resource.model->nodes)
            if (node.skin)
                throw std::runtime_error("Mesh particles cannot bind an animated model skin");
}
void validate_particle_material_profile(const MaterialResourceData& material,
                                        const ParticleEffectData& effect) {
    validate_render_material(material);
    if (!material.render_textures.empty())
        throw std::runtime_error("Mesh particles require source textures; camera render "
                                 "textures are supported by MeshRenderer materials");
    if (material.surface || material_transmits(prepare_pbr_material(material.values)))
        throw std::runtime_error(
            "Mesh particles require built-in material models without transmission or custom "
            "Shader surfaces");
    if (!particle_uses_sorted_alpha(effect) && !particle_uses_weighted_alpha(effect) &&
        !particle_uses_gpu_sorted_alpha(effect) && material.values.alpha == MaterialAlpha::Blend)
        throw std::runtime_error(
            "Blend particles require CPU sorted alpha or an explicit GPU alpha transport");
    if ((particle_uses_weighted_alpha(effect) || particle_uses_gpu_sorted_alpha(effect)) &&
        material.values.alpha != MaterialAlpha::Blend)
        throw std::runtime_error("GPU alpha transport requires a Blend Material");
    if (particle_uses_trails(effect) && !material.values.double_sided)
        throw std::runtime_error("Particle trails require a double-sided Material");
    const bool identity_opacity =
        effect.opacity_curve.empty()
            ? effect.opacity_start == 1 && effect.opacity_end == 1
            : std::all_of(effect.opacity_curve.begin(), effect.opacity_curve.end(),
                          [](const auto& stop) { return stop.value == 1; });
    if ((effect.version == 6 ||
         ((particle_uses_billboards(effect) || particle_uses_trails(effect) ||
           particle_uses_collision(effect) || particle_uses_scene_collision(effect)) &&
          !identity_opacity)) &&
        material.values.alpha != MaterialAlpha::Blend)
        throw std::runtime_error("Particle lifetime opacity requires a Blend Material");
    if (material.values.alpha == MaterialAlpha::Blend &&
        (!material.values.depth_test || material.values.depth_write))
        throw std::runtime_error(
            "Transparent particles require depth testing without depth writes");
}
std::vector<ParticleRenderInstance> particle_render_instances(const ParticleEffectData& data,
                                                              double time, std::uint32_t seed,
                                                              const AffineTransform& emitter,
                                                              std::size_t budget, double size,
                                                              std::array<float, 3> tint) {
    validate_particle_instance_parameters(size, tint);
    auto samples = sample_particle_effect(data, time, seed, budget);
    modulate_particle_samples(samples, size, tint);
    std::vector<ParticleRenderInstance> result;
    result.reserve(samples.size());
    for (const auto& sample : samples)
        result.push_back(instance(sample, emitter));
    return result;
}
std::vector<ParticleRenderInstance>
particle_world_render_instances(const ParticleEffectData& effect,
                                std::span<const ParticleWorldSample> samples, double time,
                                double size, std::array<float, 3> tint) {
    validate_particle_effect(effect);
    validate_particle_world_samples(samples);
    validate_particle_instance_parameters(size, tint);
    if (!particle_uses_scene_collision(effect) || samples.size() > effect.capacity ||
        !std::isfinite(time) || time < 0 || time > 1e9)
        throw std::runtime_error(
            "Particle World capture does not match its effect or time profile");
    const auto window = particle_birth_window(effect, time);
    std::vector<ParticleSample> values;
    values.reserve(samples.size());
    for (const auto& value : samples) {
        const auto ordinal = value.sample.birth;
        if (effect.emission == ParticleEmission::Burst && ordinal >= effect.burst_count)
            throw std::runtime_error("Particle World capture has a foreign burst ordinal");
        const auto age = particle_birth_age(effect, window, time, ordinal);
        if (!age || (effect.emission == ParticleEmission::Burst && !window.last) ||
            *age != value.sample.age)
            throw std::runtime_error("Particle World capture is outside its live emission phase");
        values.push_back(value.sample);
    }
    modulate_particle_samples(values, size, tint);
    std::vector<ParticleRenderInstance> result;
    result.reserve(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        AffineTransform frame;
        for (unsigned row = 0; row < 3; ++row) {
            for (unsigned column = 0; column < 3; ++column)
                frame.m[row * 4 + column] = samples[i].basis[row * 3 + column];
            frame.m[row * 4 + 3] = values[i].position[row];
        }
        values[i].position = {};
        result.push_back(instance(values[i], frame));
    }
    return result;
}
std::optional<RenderBounds> particle_world_bounds(const MeshBounds& mesh,
                                                  std::span<const ParticleRenderInstance> values,
                                                  bool billboards) {
    std::optional<RenderBounds> result;
    for (const auto& value : values) {
        auto bounds = transform_bounds(mesh, value.world);
        if (billboards) {
            const auto& m = value.world.m;
            const auto width = std::hypot(m[0], m[4], m[8]);
            const auto height = std::hypot(m[1], m[5], m[9]);
            const auto radius = .5 * std::hypot(width, height);
            for (unsigned axis = 0; axis < 3; ++axis) {
                bounds.minimum[axis] = std::nextafter(m[axis * 4 + 3] - radius,
                                                      -std::numeric_limits<double>::infinity());
                bounds.maximum[axis] = std::nextafter(m[axis * 4 + 3] + radius,
                                                      std::numeric_limits<double>::infinity());
                if (!std::isfinite(bounds.minimum[axis]) || !std::isfinite(bounds.maximum[axis]))
                    throw std::runtime_error("Particle World bounds exceed finite range");
            }
        }
        if (!result)
            result = bounds;
        else
            for (unsigned axis = 0; axis < 3; ++axis) {
                result->minimum[axis] = std::min(result->minimum[axis], bounds.minimum[axis]);
                result->maximum[axis] = std::max(result->maximum[axis], bounds.maximum[axis]);
            }
    }
    return result;
}
RenderBounds particle_effect_bounds(const ParticleEffectData& data, const MeshBounds& mesh,
                                    const AffineTransform& world, double multiplier) {
    validate_particle_effect(data);
    if (particle_uses_scene_collision(data))
        throw std::runtime_error("Scene collision bounds require copied live World samples");
    validate_particle_instance_parameters(multiplier, {1, 1, 1});
    MeshBounds envelope;
    auto size = std::max(data.size_start, data.size_end);
    for (const auto& stop : data.size_curve)
        size = std::max(size, stop.value);
    if (data.program)
        size = data.program->maximum_size;
    size *= multiplier;
    for (unsigned axis = 0; axis < 3; ++axis) {
        const auto acceleration =
            particle_uses_collision(data)
                ? std::hypot(data.acceleration[0], data.acceleration[1], data.acceleration[2])
                : std::abs(data.acceleration[axis]);
        const auto travel =
            data.speed_max * data.lifetime + .5 * acceleration * data.lifetime * data.lifetime;
        const auto origin = data.shape == ParticleSpawnShape::Box     ? .5 * data.shape_size[axis]
                            : data.shape == ParticleSpawnShape::Point ? 0.
                                                                      : data.shape_radius;
        const auto position_reach = data.program ? data.program->extent[axis] : origin + travel;
        const auto reach =
            position_reach + ((particle_uses_billboards(data) || particle_uses_trails(data))
                                  ? 0
                                  : size * std::max(std::abs(double(mesh.minimum[axis])),
                                                    std::abs(double(mesh.maximum[axis]))));
        if (!std::isfinite(reach) || reach > std::numeric_limits<float>::max())
            throw std::runtime_error("Particle bounds exceed the supported profile");
        // Round outward before converting to the renderer's admitted float local AABB.
        envelope.minimum[axis] =
            std::nextafter(-float(reach), -std::numeric_limits<float>::infinity());
        envelope.maximum[axis] =
            std::nextafter(float(reach), std::numeric_limits<float>::infinity());
    }
    auto result = transform_bounds(envelope, world);
    if (particle_uses_billboards(data) || particle_uses_trails(data)) {
        const double width = std::hypot(world.m[0], world.m[4], world.m[8]);
        const double height = std::hypot(world.m[1], world.m[5], world.m[9]);
        const double radius =
            .5 * size * (particle_uses_trails(data) ? width : std::hypot(width, height));
        if (!std::isfinite(radius))
            throw std::runtime_error("Camera-aligned particle bounds exceed finite world range");
        for (unsigned axis = 0; axis < 3; ++axis) {
            result.minimum[axis] = std::nextafter(result.minimum[axis] - radius,
                                                  -std::numeric_limits<double>::infinity());
            result.maximum[axis] = std::nextafter(result.maximum[axis] + radius,
                                                  std::numeric_limits<double>::infinity());
            if (!std::isfinite(result.minimum[axis]) || !std::isfinite(result.maximum[axis]))
                throw std::runtime_error(
                    "Camera-aligned particle bounds exceed finite world range");
        }
    }
    return result;
}
AffineTransform particle_billboard_world(const AffineTransform& source, const CameraView& camera) {
    const double width = std::hypot(source.m[0], source.m[4], source.m[8]);
    const double height = std::hypot(source.m[1], source.m[5], source.m[9]);
    const auto normal_scale = std::max(width, height);
    auto result = source;
    for (unsigned row = 0; row < 3; ++row) {
        result.m[4 * row] = camera.right[row] * width;
        result.m[4 * row + 1] = camera.up[row] * height;
        result.m[4 * row + 2] = -camera.forward[row] * normal_scale;
    }
    for (const auto value : result.m)
        if (!std::isfinite(value))
            throw std::runtime_error("Camera-aligned particle transform is nonfinite");
    return result;
}
} // namespace forge
