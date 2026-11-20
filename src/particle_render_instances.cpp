#include "particle_render_instances.hpp"
#include "pbr_material.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace forge {
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
           particle_uses_collision(effect)) &&
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
    for (const auto& sample : samples) {
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
        result.push_back({sample.birth, world, sample.color, sample.opacity});
    }
    return result;
}
RenderBounds particle_effect_bounds(const ParticleEffectData& data, const MeshBounds& mesh,
                                    const AffineTransform& world, double multiplier) {
    validate_particle_effect(data);
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
