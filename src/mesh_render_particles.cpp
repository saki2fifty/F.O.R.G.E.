#include "mesh_render_host.hpp"
#include "particle_render_instances.hpp"
#include <algorithm>
#include <set>
namespace forge {
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
RenderMesh proxy(const RenderParticleEmitter& emitter, const ParticleEffectData& effect,
                 const MeshResourceData& mesh) {
    RenderMesh result;
    result.entity = emitter.entity;
    result.world = emitter.world;
    result.renderer.mesh = effect.mesh;
    result.renderer.visible = emitter.emitter.visible;
    result.renderer.cast_shadows = false;
    result.renderer.receive_shadows = false;
    result.renderer.layers = emitter.emitter.layers;
    if (effect.material.id)
        result.renderer.materials.push_back({mesh.materials.front().key, effect.material});
    result.selectable = false;
    result.particle_source = true;
    return result;
}
} // namespace
std::vector<RenderMesh> MeshSceneRenderer::prepare_particle_sources(const RenderScene& scene) {
    require(scene.particles.size() <= particle_emitter_limit,
            "Scene exceeds the 64 particle-emitter budget");
    std::set<EntityId> used;
    for (const auto& emitter : scene.particles) {
        require(used.insert(emitter.entity).second,
                "Presentation repeats a particle emitter identity");
        require(emitter.emitter.enabled && emitter.emitter.effect.id,
                "Particle presentation contains an inactive/unassigned emitter");
    }
    std::erase_if(particle_sources_, [&](const auto& row) { return !used.contains(row.first); });
    std::vector<RenderMesh> result;
    std::size_t capacity = 0;
    for (const auto& emitter : scene.particles) {
        auto& source = particle_sources_[emitter.entity];
        if (source.epoch != host_->epoch_ || source.effect != emitter.emitter.effect) {
            source = {};
            source.effect = emitter.emitter.effect;
            source.epoch = host_->epoch_;
            try {
                const auto preview = host_->particle_previews_.find(source.effect.id);
                if (preview != host_->particle_previews_.end())
                    source.preview = preview->second;
                else
                    source.effect_ticket = request_particle_effect(
                        host_->particles_, host_->project_, host_->catalog_, source.effect);
            } catch (const std::exception& error) {
                source.error = error.what();
            }
        }
        try {
            if (!source.error.empty())
                throw std::runtime_error(source.error);
            if (!source.data() && source.effect_ticket) {
                source.effect_lease = host_->particles_.acquire(source.effect_ticket);
                if (!source.effect_lease) {
                    const auto state = source.effect_ticket.inspect();
                    if (resource_detail::terminal(state.state))
                        throw std::runtime_error("Particle Effect load failed: " +
                                                 state.diagnostic);
                    continue;
                }
            }
            const auto* data = source.data();
            require(bool(data), "Particle Effect data is unavailable");
            if (!emitter.revision.empty()) {
                require(!source.preview &&
                            source.effect_lease.identity().revision == emitter.revision &&
                            source.effect_ticket.inspect().source_generation == emitter.generation,
                        "Particle runtime and presentation selections differ; restart Play");
            }
            require(data->capacity <= particle_scene_capacity - capacity,
                    "Scene exceeds the 8192 reserved particle-instance budget");
            capacity += data->capacity;
            if (!source.mesh_ticket)
                source.mesh_ticket = asset_detail::request_model_mesh(
                    host_->meshes_, host_->project_, host_->catalog_, data->mesh);
            if (!source.mesh_lease)
                source.mesh_lease = host_->meshes_.acquire(source.mesh_ticket);
            if (!source.mesh_lease) {
                const auto state = source.mesh_ticket.inspect();
                if (resource_detail::terminal(state.state))
                    throw std::runtime_error("Particle Mesh load failed: " + state.diagnostic);
                continue;
            }
            validate_particle_mesh_profile(source.mesh_lease.get());
            result.push_back(proxy(emitter, *data, source.mesh_lease.get()));
        } catch (const std::exception& error) {
            report(emitter.entity, error.what(), true);
        }
    }
    return result;
}
void MeshSceneRenderer::update_particle_instances(const RenderScene& scene) {
    particle_instances_.clear();
    particle_bounds_.clear();
    constexpr std::uint64_t byte_limit = 8 * 1024 * 1024;
    std::uint64_t bytes = 0;
    for (const auto& emitter : scene.particles) {
        const auto source = particle_sources_.find(emitter.entity);
        const auto entry = entries_.find({emitter.entity, true});
        if (source == particle_sources_.end() || !source->second.data() ||
            entry == entries_.end() || !entry->second.ready)
            continue;
        try {
            const auto& prepared = entry->second.ready->prepared();
            require(!prepared.has_fallbacks() && !entry->second.fallback,
                    "Particle Effect requires complete admitted mesh/material resources");
            validate_particle_mesh_profile(prepared.mesh.get());
            const auto part = entry->second.ready->parts(0).front();
            require(part.alpha != MaterialAlpha::Blend && !part.transmission &&
                        !entry->second.pose.skinned &&
                        entry->second.ready->supports_instances(0, 0),
                    "CPU mesh particles require opaque/masked static instancing; "
                    "Blend/transmission is unsupported");
            for (const auto& [id, material] : prepared.materials)
                validate_particle_material_profile(material.get());
            const auto& data = *source->second.data();
            const auto bounds =
                particle_effect_bounds(data, prepared.mesh->mesh.lods.front().parts.front().bounds,
                                       emitter.world, emitter.emitter.size_multiplier);
            particle_bounds_.emplace_back(emitter.entity, bounds);
            if (!emitter.emitter.visible)
                continue;
            auto draw = proxy(emitter, data, prepared.mesh.get());
            std::uint64_t each = sizeof(decltype(particle_instances_)::value_type) +
                                 mesh_pose_bytes(entry->second.pose);
            each += draw.renderer.materials.capacity() * sizeof(MaterialSlotOverride);
            for (const auto& slot : draw.renderer.materials)
                each += slot.slot.capacity();
            const auto values = particle_render_instances(
                data, emitter.time, emitter.emitter.seed, emitter.world,
                particle_scene_capacity - particle_instances_.size(),
                emitter.emitter.size_multiplier,
                {emitter.emitter.tint_r, emitter.emitter.tint_g, emitter.emitter.tint_b});
            require(values.size() <= (byte_limit - bytes) / each,
                    "Particle instance CPU payload exceeds the 8 MiB frame budget");
            bytes += values.size() * each;
            for (const auto& instance : values) {
                auto pose = entry->second.pose;
                pose.world = instance.world;
                draw.world = instance.world;
                draw.legacy_tint = instance.color;
                draw.birth = instance.birth;
                particle_instances_.emplace_back(draw, std::move(pose));
            }
        } catch (const std::exception& error) {
            report(emitter.entity, error.what(), true);
        }
    }
}
} // namespace forge
