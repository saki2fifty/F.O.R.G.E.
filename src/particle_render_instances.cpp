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
            "CPU mesh particles require one static triangle part, one LOD and one material slot");
    const auto& part = mesh.lods.front().parts.front();
    if (part.topology != MeshTopology::Triangles || !part.morph_targets.empty() ||
        !part.joint_palette.empty())
        throw std::runtime_error(
            "CPU mesh particles do not support skinning, morphs, points or lines");
    for (const auto& stream : part.streams)
        if (stream.semantic.starts_with("JOINTS") || stream.semantic.starts_with("WEIGHTS"))
            throw std::runtime_error("CPU mesh particles do not support skinned vertex channels");
    if (resource.model)
        for (const auto& node : resource.model->nodes)
            if (node.skin)
                throw std::runtime_error("CPU mesh particles cannot bind an animated model skin");
}
void validate_particle_material_profile(const MaterialResourceData& material) {
    validate_render_material(material);
    if (material.surface || material.values.alpha == MaterialAlpha::Blend ||
        material_transmits(prepare_pbr_material(material.values)))
        throw std::runtime_error(
            "CPU mesh particles require built-in opaque/masked material models; Blend, "
            "transmission and custom Shader surfaces are unsupported");
}
std::vector<ParticleRenderInstance> particle_render_instances(const ParticleEffectData& data,
                                                              double time, std::uint32_t seed,
                                                              const AffineTransform& emitter,
                                                              std::size_t budget) {
    const auto samples = sample_particle_effect(data, time, seed, budget);
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
        result.push_back({sample.birth, world, sample.color});
    }
    return result;
}
RenderBounds particle_effect_bounds(const ParticleEffectData& data, const MeshBounds& mesh,
                                    const AffineTransform& world) {
    validate_particle_effect(data);
    MeshBounds envelope;
    const auto size = std::max(data.size_start, data.size_end);
    for (unsigned axis = 0; axis < 3; ++axis) {
        const auto travel = data.speed_max * data.lifetime +
                            .5 * std::abs(data.acceleration[axis]) * data.lifetime * data.lifetime;
        const auto reach = travel + size * std::max(std::abs(double(mesh.minimum[axis])),
                                                    std::abs(double(mesh.maximum[axis])));
        if (!std::isfinite(reach) || reach > std::numeric_limits<float>::max())
            throw std::runtime_error("Particle bounds exceed the supported profile");
        // Round outward before converting to the renderer's admitted float local AABB.
        envelope.minimum[axis] =
            std::nextafter(-float(reach), -std::numeric_limits<float>::infinity());
        envelope.maximum[axis] =
            std::nextafter(float(reach), std::numeric_limits<float>::infinity());
    }
    return transform_bounds(envelope, world);
}
} // namespace forge
