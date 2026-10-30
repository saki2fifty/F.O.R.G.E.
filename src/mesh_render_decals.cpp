#include "decal_shader.hpp"
#include "mesh_render_host.hpp"
#include "model_render_resource.hpp"
#include "texture_gpu.hpp"
#include <algorithm>
#include <set>
namespace forge {
void MeshSceneRenderer::clear_decal_bindings() {
    // Invisible parts can retain native references too. Clear all live bundles
    // before replacing the corresponding leases, as for environment revisions.
    for (const auto& [key, weak] : bundles_) {
        (void)key;
        if (const auto bundle = weak.lock())
            bundle->clear_decals(host_->context_);
    }
}
bool MeshSceneRenderer::update_decals(const RenderScene& scene) {
    bool changed = false, cleared = false;
    const auto before_release = [&] {
        if (!cleared) {
            clear_decal_bindings();
            cleared = true;
        }
        changed = true;
    };
    const auto diagnostic = [&](EntityId entity, std::string message) {
        Diagnostic value{Severity::Warning, "render.decal.resource", message.substr(0, 4000), {}};
        value.context.asset = scene.scene;
        value.context.entity = entity;
        value.context.property = "forge.decal";
        value.context.source = "presentation";
        report(std::move(value));
    };
    std::vector<const RenderDecal*> active;
    std::set<EntityId> ids;
    bool duplicate = false;
    for (const auto& decal : scene.decals)
        if (decal.projector.values.enabled && decal.projector.values.material.id &&
            decal.projector.values.opacity != 0) {
            active.push_back(&decal);
            duplicate = !ids.insert(decal.entity).second || duplicate;
        }
    if (duplicate || active.size() > decal_draw_limit) {
        if (!decal_entries_.empty()) {
            before_release();
            decal_entries_.clear();
        }
        decal_order_.clear();
        diagnostic({}, duplicate ? "Duplicate decal entities; color projection omitted."
                                 : "Scene exceeds the 8 active color projector limit; ordinary "
                                   "materials retained. Disable or remove projectors.");
        return changed;
    }
    for (auto it = decal_entries_.begin(); it != decal_entries_.end();)
        if (!ids.contains(it->first)) {
            before_release();
            it = decal_entries_.erase(it);
        } else
            ++it;
    for (const auto* decal : active) {
        auto& entry = decal_entries_[decal->entity];
        changed = changed || entry.projector.values != decal->projector.values ||
                  entry.projector.world != decal->projector.world;
        entry.projector = decal->projector;
        if (entry.requested != entry.projector.values.material || entry.epoch != host_->epoch_) {
            changed = true;
            entry.requested = entry.projector.values.material;
            entry.epoch = host_->epoch_;
            entry.material_ticket.reset();
            entry.texture_ticket.reset();
            entry.candidate.reset();
            entry.error.clear();
            try {
                entry.material_ticket = asset_detail::request_model_pbr_material(
                    host_->materials_, host_->project_, host_->catalog_, entry.requested,
                    host_->material_preview_);
            } catch (const std::exception& error) {
                entry.error = error.what();
            }
        }
        // A candidate is published only after its selected color texture and
        // sampler are complete. Failure retains the last admitted material.
        try {
            const auto ready = [](const ResourceTicket& ticket) {
                const auto info = ticket.inspect();
                if (info.state == ResourceState::Ready)
                    return true;
                if (!resource_detail::terminal(info.state))
                    return false;
                throw std::runtime_error(info.diagnostic.empty() ? resource_state_name(info.state)
                                                                 : info.diagnostic);
            };
            if (entry.material_ticket && ready(*entry.material_ticket)) {
                auto material = host_->materials_.acquire(*entry.material_ticket);
                if (!material)
                    throw std::runtime_error("Decal material became stale before adoption");
                if (material->surface)
                    throw std::runtime_error(
                        "Color projectors require a built-in PBR or unlit "
                        "Material; custom Surface Shader sources are unsupported");
                DecalResources candidate;
                candidate.values = prepare_decal_material(material->values, material->textures);
                candidate.material = std::move(material);
                if (candidate.values.texture.id)
                    entry.texture_ticket = asset_detail::request_texture(
                        host_->textures_, host_->project_, host_->catalog_,
                        candidate.values.texture, TextureSemantic::Color);
                entry.candidate = std::move(candidate);
                entry.material_ticket.reset();
            }
            if (entry.texture_ticket && ready(*entry.texture_ticket)) {
                auto texture = host_->textures_.acquire(*entry.texture_ticket);
                if (!texture || texture->dimension != TextureDimension::D2 ||
                    texture->semantic != TextureSemantic::Color)
                    throw std::runtime_error("Decal color requires a current 2D color texture");
                entry.candidate->gpu = host_->gpu_textures_.acquire(texture);
                entry.candidate->texture = std::move(texture);
                entry.candidate->sampler = upload_sampler(host_->presentation_.device(),
                                                          entry.candidate->values.slot.sampler);
                entry.texture_ticket.reset();
            }
            if (entry.candidate && !entry.texture_ticket) {
                before_release();
                entry.ready = std::move(entry.candidate);
                entry.candidate.reset();
                entry.error.clear();
            }
        } catch (const std::exception& error) {
            entry.material_ticket.reset();
            entry.texture_ticket.reset();
            entry.candidate.reset();
            entry.error = error.what();
        }
        if (!entry.error.empty())
            diagnostic(decal->entity,
                       entry.error + (entry.ready ? " (previous decal material retained)"
                                                  : " (projection omitted)"));
    }
    std::sort(active.begin(), active.end(), [](const auto* a, const auto* b) {
        const auto ap = a->projector.values.priority, bp = b->projector.values.priority;
        return ap == bp ? a->entity < b->entity : ap < bp;
    });
    decal_order_.clear();
    for (const auto* decal : active)
        if (decal_entries_.at(decal->entity).ready)
            decal_order_.push_back(decal->entity);
    return changed;
}
} // namespace forge
