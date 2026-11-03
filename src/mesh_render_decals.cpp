#include "decal_shader.hpp"
#include "mesh_render_host.hpp"
#include "model_render_resource.hpp"
#include "texture_gpu.hpp"
#include <algorithm>
#include <set>
namespace forge {
namespace {
bool projected_resource_ready(const ResourceTicket& ticket) {
    const auto info = ticket.inspect();
    if (info.state == ResourceState::Ready)
        return true;
    if (!resource_detail::terminal(info.state))
        return false;
    throw std::runtime_error(info.diagnostic.empty() ? resource_state_name(info.state)
                                                     : info.diagnostic);
}
} // namespace
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
    if (duplicate || active.size() > decal_scene_limit) {
        if (!decal_entries_.empty()) {
            before_release();
            decal_entries_.clear();
        }
        decal_order_.clear();
        diagnostic({}, duplicate ? "Duplicate decal entities; projection omitted."
                                 : "Scene exceeds the 128 active projector limit; ordinary "
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
        const unsigned channels =
            (1u << decal_color_channel) |
            (entry.projector.values.normal_influence > 0 ? 1u << decal_normal_channel : 0u) |
            (entry.projector.values.roughness_influence > 0 ? 1u << decal_roughness_channel : 0u);
        if (entry.requested != entry.projector.values.material || entry.epoch != host_->epoch_ ||
            entry.channels != channels) {
            changed = true;
            entry.requested = entry.projector.values.material;
            entry.epoch = host_->epoch_;
            entry.channels = channels;
            entry.material_ticket.reset();
            for (auto& ticket : entry.texture_tickets)
                ticket.reset();
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
        // Publish only a complete set of required channel resources. Pending or failed
        // additions keep the admitted channel mask and last usable material.
        try {
            constexpr std::array semantics{TextureSemantic::Color, TextureSemantic::Normal,
                                           TextureSemantic::Data};
            if (entry.material_ticket && projected_resource_ready(*entry.material_ticket)) {
                auto material = host_->materials_.acquire(*entry.material_ticket);
                if (!material)
                    throw std::runtime_error("Decal material became stale before adoption");
                if (material->surface)
                    throw std::runtime_error(
                        "Projectors require a built-in PBR or unlit "
                        "Material; custom Surface Shader sources are unsupported");
                if (!material->render_textures.empty())
                    throw std::runtime_error("Projectors require source textures; camera render "
                                             "textures are supported by MeshRenderer materials");
                DecalResources candidate;
                candidate.values = prepare_decal_material(material->values, material->textures);
                candidate.material = std::move(material);
                candidate.channels = channels;
                for (unsigned c = 0; c < decal_channel_count; ++c)
                    if ((channels & (1u << c)) && candidate.values.textures[c].asset.id)
                        entry.texture_tickets[c] = asset_detail::request_texture(
                            host_->textures_, host_->project_, host_->catalog_,
                            candidate.values.textures[c].asset, semantics[c]);
                entry.candidate = std::move(candidate);
                entry.material_ticket.reset();
            }
            for (unsigned c = 0; c < decal_channel_count; ++c)
                if (entry.texture_tickets[c] &&
                    projected_resource_ready(*entry.texture_tickets[c])) {
                    auto texture = host_->textures_.acquire(*entry.texture_tickets[c]);
                    if (!texture || texture->dimension != TextureDimension::D2 ||
                        texture->semantic != semantics[c])
                        throw std::runtime_error("Decal channel requires a current 2D texture with "
                                                 "its matching semantic");
                    auto& selected = entry.candidate->textures[c];
                    selected.gpu = host_->gpu_textures_.acquire(texture);
                    selected.source = std::move(texture);
                    selected.sampler =
                        upload_sampler(host_->presentation_.device(),
                                       entry.candidate->values.textures[c].slot.sampler);
                    entry.texture_tickets[c].reset();
                }
            if (entry.candidate &&
                std::none_of(entry.texture_tickets.begin(), entry.texture_tickets.end(),
                             [](const auto& ticket) { return ticket.has_value(); })) {
                before_release();
                entry.ready = std::move(entry.candidate);
                entry.candidate.reset();
                entry.error.clear();
            }
        } catch (const std::exception& error) {
            entry.material_ticket.reset();
            for (auto& ticket : entry.texture_tickets)
                ticket.reset();
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
bool MeshSceneRenderer::update_light_images(const RenderScene& scene) {
    bool changed = false, cleared = false;
    const auto before_release = [&] {
        if (!cleared) {
            clear_decal_bindings();
            cleared = true;
        }
        changed = true;
    };
    std::set<EntityId> active;
    for (const auto& light : scene.lights)
        if (light.texture.id && light.image)
            active.insert(light.entity);
    for (auto it = light_image_entries_.begin(); it != light_image_entries_.end();)
        if (!active.contains(it->first)) {
            before_release();
            it = light_image_entries_.erase(it);
        } else
            ++it;
    for (const auto& light : scene.lights) {
        if (!light.texture.id || !light.image)
            continue;
        auto& entry = light_image_entries_[light.entity];
        const auto dimension = light.light.kind == unsigned(LightKind::Point)
                                   ? TextureDimension::Cube
                                   : TextureDimension::D2;
        if (entry.requested != light.texture || entry.dimension != dimension ||
            entry.epoch != host_->epoch_) {
            changed = true;
            if (entry.dimension != dimension && entry.ready) {
                before_release();
                entry.ready.reset();
            }
            entry.requested = light.texture;
            entry.dimension = dimension;
            entry.epoch = host_->epoch_;
            entry.ticket.reset();
            entry.error.clear();
            try {
                // Existing selection chooses HDR color when present, otherwise Color.
                entry.ticket = asset_detail::request_texture(host_->textures_, host_->project_,
                                                             host_->catalog_, entry.requested);
            } catch (const std::exception& error) {
                entry.error = error.what();
            }
        }
        try {
            if (entry.ticket && projected_resource_ready(*entry.ticket)) {
                ProjectedTextureResource candidate;
                candidate.source = host_->textures_.acquire(*entry.ticket);
                if (!candidate.source || candidate.source->dimension != dimension ||
                    (candidate.source->semantic != TextureSemantic::Color &&
                     candidate.source->semantic != TextureSemantic::HdrColor))
                    throw std::runtime_error(
                        dimension == TextureDimension::Cube
                            ? "Point light image requires a current Color/HDR Cube Texture"
                            : "Directional/spot light image requires a current Color/HDR 2D "
                              "Texture");
                if (candidate.source->sampler.compare != TextureCompare::None)
                    throw std::runtime_error(
                        "Light images require a non-comparison Texture sampler");
                candidate.gpu = host_->gpu_textures_.acquire(candidate.source);
                candidate.sampler =
                    upload_sampler(host_->presentation_.device(), candidate.source->sampler);
                before_release();
                entry.ready = std::move(candidate);
                entry.ticket.reset();
                entry.error.clear();
            }
        } catch (const std::exception& error) {
            entry.ticket.reset();
            entry.error = error.what();
        }
        if (!entry.error.empty()) {
            Diagnostic diagnostic{Severity::Warning,
                                  "render.light.image.resource",
                                  (entry.error + (entry.ready ? " (previous light image retained)"
                                                              : " (ordinary light retained)"))
                                      .substr(0, 4000),
                                  {}};
            diagnostic.context.asset = scene.scene;
            diagnostic.context.entity = light.entity;
            diagnostic.context.property = "forge.light.texture";
            diagnostic.context.source = "presentation";
            report(std::move(diagnostic));
        }
    }
    return changed;
}
} // namespace forge
