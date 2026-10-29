#include "mesh_render_host.hpp"
#include "model_render_resource.hpp"
#include <algorithm>
#include <set>
namespace forge {
bool MeshSceneRenderer::resolve_environment(EnvironmentSelection& selection,
                                            AssetRef<TextureAsset> source) {
    bool changed = false;
    if (source != selection.source || selection.epoch != host_->epoch_) {
        selection.source = source;
        selection.epoch = host_->epoch_;
        selection.candidate.reset();
        selection.error.clear();
        changed = true;
        if (!source.id)
            selection.ready = {};
        else
            try {
                selection.candidate = asset_detail::request_texture(
                    host_->textures_, host_->project_, host_->catalog_, source);
            } catch (const std::exception& error) {
                selection.error = error.what();
            }
    }
    if (selection.candidate) {
        const auto info = selection.candidate->inspect();
        if (info.state == ResourceState::Ready) {
            try {
                const auto cpu = host_->textures_.acquire(*selection.candidate);
                if (!cpu)
                    throw std::runtime_error("Environment selection became stale before adoption");
                auto next = host_->environments_.acquire(cpu);
                selection.ready = std::move(next);
                changed = true;
            } catch (const std::exception& error) {
                selection.error = error.what();
            }
            selection.candidate.reset();
        } else if (info.state != ResourceState::Queued && info.state != ResourceState::Loading &&
                   info.state != ResourceState::DependencyPending &&
                   info.state != ResourceState::Replacing) {
            selection.error =
                info.diagnostic.empty() ? resource_state_name(info.state) : info.diagnostic;
            selection.candidate.reset();
        }
    }
    return changed;
}
bool MeshSceneRenderer::update_environment(const RenderScene& scene) {
    const bool changed = resolve_environment(environment_, scene.settings.environment.texture);
    if (changed)
        for (auto& [id, entry] : entries_) {
            (void)id;
            if (entry.ready)
                entry.ready->environment(environment_.ready);
        }
    if (!environment_.error.empty()) {
        Diagnostic error{Severity::Error,
                         "render.environment.resource",
                         environment_.error.substr(0, 4000) +
                             (environment_.ready ? " (previous environment retained)" : ""),
                         {}};
        error.context.asset = scene.scene;
        error.context.property = "rendering.environment.texture";
        error.context.source = "presentation";
        report(std::move(error));
    }
    return changed;
}
bool MeshSceneRenderer::update_reflection_probes(const RenderScene& scene) {
    const auto diagnostic = [&](EntityId entity, const std::string& message, bool warning = false) {
        Diagnostic value{warning ? Severity::Warning : Severity::Error,
                         "render.reflection_probe.resource",
                         message.substr(0, 4000),
                         {}};
        value.context.asset = scene.scene;
        value.context.entity = entity;
        value.context.property = "forge.reflection_probe";
        value.context.source = "presentation";
        report(std::move(value));
    };
    std::vector<const RenderReflectionProbe*> active;
    for (const auto& probe : scene.reflection_probes)
        if (probe.probe.enabled && probe.probe.texture.id && probe.probe.intensity != 0)
            active.push_back(&probe);
    if (active.size() > 8) {
        const bool changed = !probe_entries_.empty();
        probe_entries_.clear();
        reflection_probes_.clear();
        diagnostic({},
                   "Scene exceeds the 8 active reflection probe limit; global lighting retained. "
                   "Disable or remove probes.",
                   true);
        return changed;
    }
    std::set<EntityId> ids;
    for (const auto* probe : active)
        if (!ids.insert(probe->entity).second) {
            const bool changed = !probe_entries_.empty();
            probe_entries_.clear();
            reflection_probes_.clear();
            diagnostic(probe->entity,
                       "Duplicate reflection probe identity; global lighting retained.");
            return changed;
        }
    bool changed = false;
    std::vector<std::pair<EntityId, ProbeEntry*>> ready;
    for (const auto* probe : active) {
        try {
            const auto volume = reflection_probe_volume(probe->probe, probe->world);
            auto [found, added] = probe_entries_.try_emplace(probe->entity);
            auto& entry = found->second;
            if (added || entry.value != probe->probe || entry.world != probe->world)
                changed = true;
            entry.value = probe->probe;
            entry.world = probe->world;
            entry.volume = volume;
            changed |= resolve_environment(entry.environment, probe->probe.texture);
            if (!entry.environment.error.empty())
                diagnostic(probe->entity,
                           entry.environment.error +
                               (entry.environment.ready ? " (previous probe map retained)" : ""));
            if (entry.environment.ready)
                ready.emplace_back(probe->entity, &entry);
        } catch (const std::exception& error) {
            changed |= probe_entries_.erase(probe->entity) != 0;
            ids.erase(probe->entity);
            diagnostic(probe->entity, std::string(error.what()) + " (probe skipped)");
        }
    }
    changed |= std::erase_if(probe_entries_,
                             [&](const auto& row) { return !ids.contains(row.first); }) != 0;
    std::sort(ready.begin(), ready.end(), [](const auto& first, const auto& second) {
        if (first.second->value.priority != second.second->value.priority)
            return first.second->value.priority > second.second->value.priority;
        return first.first < second.first;
    });
    reflection_probes_.clear();
    for (const auto& [id, entry] : ready) {
        const auto& value = entry->value;
        reflection_probes_.push_back({entry->volume,
                                      entry->environment.ready.get().specular->GetDefaultView(
                                          Diligent::TEXTURE_VIEW_SHADER_RESOURCE),
                                      value.intensity, value.blend_distance, value.box_projection,
                                      id});
    }
    return changed;
}
std::vector<SurfaceReflections::Probe>
MeshSceneRenderer::reflection_probes(const CameraView& view) {
    host_->check_thread();
    std::vector<SurfaceReflections::Probe> result;
    result.reserve(reflection_probes_.size());
    for (const auto& probe : reflection_probes_) {
        try {
            (void)reflection_probe_view_center(probe.volume, view.position);
            result.push_back(probe);
        } catch (const std::exception& error) {
            Diagnostic value{Severity::Warning,
                             "render.reflection_probe.view",
                             std::string(error.what()) + " (probe skipped for this camera)",
                             {}};
            value.context.asset = scene_;
            value.context.entity = probe.entity;
            value.context.property = "forge.reflection_probe";
            value.context.source = "presentation";
            report(std::move(value));
        }
    }
    return result;
}
} // namespace forge
