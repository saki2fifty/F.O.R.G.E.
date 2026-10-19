#include "mesh_render_host.hpp"
#include "asset_bytes.hpp"
#include "material_parameter_overrides.hpp"
#include "mesh_draw_shader.hpp"
#include "mesh_pick.hpp"
#include "pbr_material.hpp"
#include "render_projection.hpp"
#include "render_sort.hpp"
#include "sprite_draw_selection.hpp"
#include "sprite_render_values.hpp"
#include <forge/engine_assets.hpp>
#include <set>
namespace forge {
namespace {
void update_sprite_mesh(RenderMesh& mesh, const RenderSprite& sprite) {
    mesh.entity = sprite.entity;
    mesh.renderer.mesh = engine_primitive(8);
    // Reuse the one logical slot's storage across frames and numeric edits.
    mesh.renderer.materials.resize(1);
    mesh.renderer.materials[0].slot = "surface";
    mesh.renderer.materials[0].material = sprite.renderer.material;
    mesh.renderer.enabled = sprite.renderer.enabled;
    mesh.renderer.visible = sprite.renderer.enabled && sprite.renderer.visible;
    mesh.renderer.cast_shadows = mesh.renderer.receive_shadows = false;
    mesh.renderer.layers = sprite.renderer.layers;
    mesh.world = sprite.world;
    mesh.selectable = sprite.selectable;
}
std::uint64_t parameter_bytes(const std::vector<MaterialParameterOverride>& values) {
    std::uint64_t bytes = values.capacity() * sizeof(MaterialParameterOverride);
    for (const auto& value : values)
        bytes += value.slot.capacity() + value.key.capacity() + 2; // String terminators.
    return bytes;
}
std::span<const MaterialParameterOverride>
slot_parameters(std::span<const MaterialParameterOverride> values, std::string_view slot) {
    const auto first =
        std::lower_bound(values.begin(), values.end(), slot,
                         [](const auto& value, auto key) { return value.slot < key; });
    const auto last = std::upper_bound(
        first, values.end(), slot, [](auto key, const auto& value) { return key < value.slot; });
    return {first, last};
}
} // namespace
MeshResourceInspection MeshResourceHost::inspect() const {
    check_thread();
    MeshResourceInspection result;
    result.meshes = meshes_.statistics();
    result.materials = materials_.statistics();
    result.textures = textures_.statistics();
    result.particles = particles_.statistics();
    result.gpu_meshes = gpu_meshes_.statistics();
    result.gpu_textures = gpu_textures_.statistics();
    result.environments = environments_.statistics();
    auto append = [&](const auto& pool) {
        const auto revisions = pool.revisions();
        result.revisions.insert(result.revisions.end(), revisions.begin(), revisions.end());
        const auto requests = pool.pending_requests();
        result.requests.insert(result.requests.end(), requests.begin(), requests.end());
    };
    append(meshes_);
    append(materials_);
    append(textures_);
    append(particles_);
    return result;
}
std::optional<RenderBounds> MeshSceneRenderer::bounds() const {
    host_->check_thread();
    std::optional<RenderBounds> result;
    for (const auto& [id, entry] : entries_) {
        (void)id;
        if (!entry.ready)
            continue;
        const auto box = mesh_instance_bounds(entry.pose, {0, 0, 0});
        if (!result)
            result = box;
        else
            for (unsigned axis = 0; axis < 3; ++axis) {
                result->minimum[axis] = std::min(result->minimum[axis], box.minimum[axis]);
                result->maximum[axis] = std::max(result->maximum[axis], box.maximum[axis]);
            }
    }
    for (const auto& [id, box] : particle_bounds_) {
        (void)id;
        if (!result)
            result = box;
        else
            for (unsigned axis = 0; axis < 3; ++axis) {
                result->minimum[axis] = std::min(result->minimum[axis], box.minimum[axis]);
                result->maximum[axis] = std::max(result->maximum[axis], box.maximum[axis]);
            }
    }
    return result;
}
std::optional<RenderBounds> MeshSceneRenderer::bounds(const RenderScene& scene,
                                                      const std::set<EntityId>& selected,
                                                      Double3 origin) const {
    host_->check_thread();
    if (scene.scene != scene_)
        return {};
    std::optional<RenderBounds> result;
    for (const auto& mesh : scene.meshes) {
        if (selected.empty() ? !mesh.renderer.visible : !selected.contains(mesh.entity))
            continue;
        const auto found = entries_.find({mesh.entity, scope(mesh)});
        if (found == entries_.end() || !found->second.ready)
            return {};
        const auto box = mesh_instance_bounds(found->second.pose, origin);
        if (!result)
            result = box;
        else
            for (unsigned axis = 0; axis < 3; ++axis) {
                result->minimum[axis] = std::min(result->minimum[axis], box.minimum[axis]);
                result->maximum[axis] = std::max(result->maximum[axis], box.maximum[axis]);
            }
    }
    for (const auto& sprite : scene.sprites) {
        if (selected.empty() ? !sprite.renderer.visible : !selected.contains(sprite.entity))
            continue;
        const auto found = entries_.find({sprite.entity, DrawScope::Sprite});
        if (found == entries_.end() || !found->second.ready)
            return {};
        const auto box = mesh_instance_bounds(found->second.pose, origin);
        if (!result)
            result = box;
        else
            for (unsigned axis = 0; axis < 3; ++axis) {
                result->minimum[axis] = std::min(result->minimum[axis], box.minimum[axis]);
                result->maximum[axis] = std::max(result->maximum[axis], box.maximum[axis]);
            }
    }
    for (const auto& emitter : scene.particles) {
        if (selected.empty() ? !emitter.emitter.visible : !selected.contains(emitter.entity))
            continue;
        const auto found =
            std::find_if(particle_bounds_.begin(), particle_bounds_.end(),
                         [&](const auto& row) { return row.first == emitter.entity; });
        if (found == particle_bounds_.end())
            return {};
        const auto& box = found->second;
        if (!result)
            result = box;
        else
            for (unsigned axis = 0; axis < 3; ++axis) {
                result->minimum[axis] = std::min(result->minimum[axis], box.minimum[axis]);
                result->maximum[axis] = std::max(result->maximum[axis], box.maximum[axis]);
            }
    }
    return result;
}
namespace {
struct DrawCapacityError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
} // namespace
EntityId MeshSceneRenderer::pick(const RenderScene& scene, const CameraView& camera, double x,
                                 double y, std::uint32_t layers, double radius) const {
    const auto result = pick_depth(scene, camera, x, y, layers, radius);
    return result ? result->first : EntityId{};
}
std::optional<std::pair<EntityId, double>>
MeshSceneRenderer::pick_depth(const RenderScene& scene, const CameraView& camera, double x,
                              double y, std::uint32_t layers, double radius) const {
    host_->check_thread();
    MeshPickBudget budget;
    EntityId selected;
    double nearest = 2;
    std::int32_t selected_order = 0;
    auto visit = [&](const RenderMesh& mesh, DrawScope draw_scope) {
        if (!mesh.selectable || !(mesh.renderer.layers & layers))
            return;
        const auto found = entries_.find({mesh.entity, draw_scope});
        if (found == entries_.end() || !found->second.ready)
            return;
        const auto& entry = found->second;
        const auto order = entry.sprite && entry.sprite->ready ? entry.sprite->admitted.order : 0;
        const auto bounds = mesh_instance_bounds(entry.pose, camera.position);
        if (!bounds_visible(bounds, camera))
            return;
        // Conservative screen rectangle; crossing a clip plane disables this
        // shortcut. Precise tests below still clip every actual primitive.
        double left = INFINITY, right = -INFINITY, top = INFINITY, bottom = -INFINITY;
        bool rectangle = true;
        for (unsigned corner = 0; corner < 8; ++corner) {
            Double3 point;
            for (unsigned c = 0; c < 3; ++c)
                point[c] = corner & (1u << c) ? bounds.maximum[c] : bounds.minimum[c];
            const auto pixel = project_render_point(camera, point);
            if (!pixel) {
                rectangle = false;
                break;
            }
            left = std::min(left, (*pixel)[0]);
            right = std::max(right, (*pixel)[0]);
            top = std::min(top, (*pixel)[1]);
            bottom = std::max(bottom, (*pixel)[1]);
        }
        if (rectangle &&
            (x < left - radius || x > right + radius || y < top - radius || y > bottom + radius))
            return;
        const auto& data = entry.ready->prepared().mesh->mesh;
        const auto lod = select_mesh_lod(data, bounds_screen_coverage(bounds, camera));
        const auto& parts = data.lods.at(lod).parts;
        for (unsigned p = 0; p < parts.size(); ++p)
            if (const auto depth = pick_mesh_part(parts[p], entry.pose.lods.at(lod).at(p),
                                                  entry.pose, camera, x, y, budget, radius))
                if (*depth < nearest ||
                    (*depth == nearest && (order > selected_order ||
                                           (order == selected_order && mesh.entity < selected)))) {
                    nearest = *depth;
                    selected = mesh.entity;
                    selected_order = order;
                }
    };
    for (const auto& mesh : scene.meshes)
        visit(mesh, scope(mesh));
    for (const auto& mesh : sprites_)
        visit(mesh, DrawScope::Sprite);
    return selected ? std::optional(std::pair{selected, nearest}) : std::nullopt;
}
MeshResourceHost::MeshResourceHost(DiligentPresentation& presentation,
                                   Diligent::IDeviceContext* context, std::filesystem::path project,
                                   bool isolated_source_preview)
    : presentation_(presentation), context_(context), project_(std::move(project)),
      isolated_source_preview_(isolated_source_preview),
      environments_(presentation.device(), context, 512ull * 1024 * 1024, 16,
                    EnvironmentRealization{&presentation}),
      gpu_meshes_(presentation.device(), context, 512ull * 1024 * 1024),
      gpu_textures_(presentation.device(), context, 512ull * 1024 * 1024) {}
void MeshResourceHost::preview_material(AssetRef<MaterialAsset> ref, MaterialResourceData data) {
    check_thread();
    if (!isolated_source_preview_ || !ref.id || epoch_ == UINT64_MAX)
        throw std::runtime_error("Unsaved material requires an isolated preview host");
    validate_render_material(data);
    const auto revision = asset_build_digest(
        {{"values", material_values_document(data.values)},
         {"textures", data.textures},
         {"shader_revision", data.surface ? data.surface->revision : ""},
         {"shader_input", data.surface ? data.surface->program.build_key : ""},
         {"shader_layout", data.surface ? data.surface->program.layout_digest() : ""}});
    if (material_preview_ && material_preview_->asset == ref &&
        material_preview_->revision == revision)
        return;
    auto next = std::make_shared<const asset_detail::MaterialPreviewSelection>(
        asset_detail::MaterialPreviewSelection{ref, revision, epoch_ + 1, std::move(data)});
    material_preview_ = std::move(next);
    ++epoch_;
}
void MeshResourceHost::check_thread() const {
    if (thread_ != std::this_thread::get_id())
        throw std::runtime_error("Mesh presentation accessed from another thread");
}
void MeshResourceHost::preview_particle_effect(AssetRef<ParticleEffectAsset> ref,
                                               ParticleEffectData data) {
    check_thread();
    if (!isolated_source_preview_ || !ref.id || epoch_ == UINT64_MAX)
        throw std::runtime_error("Unsaved particles require an isolated preview host");
    validate_particle_effect(data);
    const auto found = particle_previews_.find(ref.id);
    if (found != particle_previews_.end() && *found->second == data)
        return;
    if (found == particle_previews_.end() && particle_previews_.size() == 1)
        throw std::runtime_error("Isolated particle preview accepts one source at a time");
    particle_previews_[ref.id] = std::make_shared<const ParticleEffectData>(std::move(data));
    ++epoch_;
}
void MeshResourceHost::preview_mesh(AssetRef<MeshAsset> ref, MeshData mesh) {
    check_thread();
    if (!isolated_source_preview_ || !ref.id || epoch_ == UINT64_MAX)
        throw std::runtime_error("Unsaved Mesh requires an isolated preview host");
    if (mesh_preview_ && mesh_preview_->asset != ref)
        throw std::runtime_error("Isolated Mesh preview accepts one source at a time");
    validate_mesh(mesh, {32 * 1024 * 1024});
    const auto revision = asset_detail::content_digest(encode_mesh(mesh));
    if (mesh_preview_ && mesh_preview_->revision == revision)
        return;
    auto data = std::make_shared<MeshResourceData>();
    data->mesh = std::move(mesh);
    data->materials = {{0, "default", {}}};
    validate_mesh_material_bindings(*data);
    auto next = std::make_shared<const asset_detail::MeshPreviewSelection>(
        asset_detail::MeshPreviewSelection{ref, revision, epoch_ + 1, std::move(data)});
    mesh_preview_ = std::move(next);
    ++epoch_;
}
void MeshResourceHost::catalog(std::shared_ptr<const AssetCatalog> catalog) {
    check_thread();
    if (!catalog)
        throw std::runtime_error("Mesh presentation requires a catalog snapshot");
    if (catalog == catalog_)
        return;
    if (epoch_ == UINT64_MAX)
        throw std::runtime_error("Mesh presentation catalog epoch exhausted");
    // No file reads here. Callers publish the snapshot already admitted by their
    // project/asset service, including removal and failed-import last-good rules.
    catalog_ = std::move(catalog);
    ++epoch_;
}
void MeshResourceHost::pump() {
    check_thread();
    meshes_.pump();
    materials_.pump();
    textures_.pump();
    particles_.pump();
    particles_.collect();
    environments_.collect();
    gpu_meshes_.collect();
    gpu_textures_.collect();
}
void MeshResourceHost::submit() {
    check_thread();
    environments_.submit();
    gpu_meshes_.submit();
    gpu_textures_.submit();
}
MeshSceneRenderer::MeshSceneRenderer(std::shared_ptr<MeshResourceHost> host,
                                     Diligent::TEXTURE_FORMAT color, std::uint64_t pose_budget,
                                     std::size_t draw_part_limit, std::uint64_t material_budget)
    : host_(std::move(host)), color_(color), pose_budget_(pose_budget),
      draw_part_limit_(draw_part_limit), material_budget_(material_budget) {
    if (!host_)
        throw std::runtime_error("Mesh scene requires a presentation resource host");
    shadows_ = std::make_unique<ShadowRenderer>(host_->presentation_);
    transmission_ = std::make_unique<TransmissionBackground>(host_->presentation_);
}
std::uint64_t MeshSceneRenderer::Entry::pose_bytes() const {
    // The inline empty pose already belongs to the entry, not an allocation.
    std::uint64_t bytes = ready ? mesh_pose_bytes(pose) : 0;
    if (ready)
        bytes += mesh_pose_bytes(ready->geometry());
    if (candidate_geometry && (!ready || candidate_geometry != ready->geometry_owner()))
        bytes += mesh_pose_bytes(*candidate_geometry);
    return bytes;
}
std::uint64_t MeshSceneRenderer::Entry::material_bytes() const {
    return parameter_bytes(requested_parameters) + parameter_bytes(parameters) +
           (sprite ? sizeof(SpriteValues) : 0);
}
std::uint64_t MeshSceneRenderer::material_payload_bytes() const {
    host_->check_thread();
    std::uint64_t bytes = 0;
    for (const auto& [id, entry] : entries_) {
        (void)id;
        const auto count = entry.material_bytes();
        if (count > material_budget_ - bytes)
            throw std::runtime_error("Retained object material values exceed the scene budget");
        bytes += count;
    }
    return bytes;
}
std::uint64_t MeshSceneRenderer::pose_payload_bytes() const {
    host_->check_thread();
    std::uint64_t bytes = 0;
    for (const auto& [id, entry] : entries_) {
        (void)id;
        const auto count = entry.pose_bytes();
        if (count > pose_budget_ - bytes)
            throw std::runtime_error("Retained mesh poses exceed the scene payload budget");
        bytes += count;
    }
    return bytes;
}
void MeshSceneRenderer::report(EntityId entity, const std::string& text, DrawScope draw_scope) {
    if (diagnostics_.size() >= 256) {
        ++omitted_;
        return;
    }
    Diagnostic value{Severity::Error,
                     draw_scope == DrawScope::Sprite     ? "render.sprite.resource"
                     : draw_scope == DrawScope::Particle ? "render.particles.resource"
                                                         : "render.mesh.resource",
                     text.substr(0, 4096),
                     {}};
    value.context.asset = scene_;
    value.context.entity = entity;
    value.context.property = draw_scope == DrawScope::Sprite     ? "forge.sprite_renderer"
                             : draw_scope == DrawScope::Particle ? "forge.particle_emitter"
                                                                 : "forge.mesh_renderer";
    value.context.source = "presentation";
    diagnostics_.push_back(std::move(value));
}
std::size_t MeshSceneRenderer::bundle_count() const {
    host_->check_thread();
    return std::count_if(bundles_.begin(), bundles_.end(),
                         [](const auto& row) { return !row.second.expired(); });
}
std::shared_ptr<MeshDrawBundle>
MeshSceneRenderer::prepare_bundle(const asset_detail::PreparedModelDraw& prepared, bool skinned,
                                  std::shared_ptr<const MeshPoseGeometry> geometry,
                                  MeshDrawProfile profile) {
    const auto key = asset_detail::prepared_model_draw_key(prepared, skinned, profile);
    if (auto found = bundles_.find(key); found != bundles_.end())
        if (auto existing = found->second.lock())
            return existing;
    // Bound native PSO/SRB/constant-buffer fan-out independently of vertex and
    // texture residency. Include retained old revisions while preparing replacements.
    std::size_t parts = 0;
    auto admit = [&](std::size_t count) {
        if (count > draw_part_limit_ - parts)
            throw DrawCapacityError("Native mesh draw-part budget exceeded (old draw retained)");
        parts += count;
    };
    for (const auto& lod : prepared.mesh->mesh.lods)
        admit(lod.parts.size());
    for (const auto& [old_key, weak] : bundles_) {
        (void)old_key;
        if (auto live = weak.lock())
            for (const auto& lod : live->prepared().mesh->mesh.lods)
                admit(lod.parts.size());
    }
    auto next = std::make_shared<MeshDrawBundle>(
        host_->presentation_, host_->context_, prepared, host_->gpu_meshes_, host_->gpu_textures_,
        color_, Diligent::TEX_FORMAT_D32_FLOAT, skinned, std::move(geometry), profile);
    next->environment(environment_ready_);
    bundles_[key] = next;
    return next;
}
bool MeshSceneRenderer::update(const RenderScene& scene) {
    host_->pump();
    bool changed = false;
    diagnostics_ = scene.diagnostics;
    omitted_ = scene.omitted_diagnostics;
    if (scene_ != scene.scene) {
        entries_.clear();
        sprites_.clear();
        particle_sources_.clear();
        particle_instances_.clear();
        particle_bounds_.clear();
        bundles_.clear();
        environment_ready_ = {};
        environment_candidate_.reset();
        environment_epoch_ = 0;
        environment_error_.clear();
        scene_ = scene.scene;
        changed = true;
    }
    changed |= update_environment(scene);
    const ModelSceneIndex model_index(scene);
    auto particle_sources = prepare_particle_sources(scene);
    sprites_.resize(scene.sprites.size());
    for (std::size_t i = 0; i < scene.sprites.size(); ++i)
        update_sprite_mesh(sprites_[i], scene.sprites[i]);
    struct Input {
        const RenderMesh* mesh;
        DrawScope scope;
        const SpriteRenderer* sprite = nullptr;
    };
    std::vector<Input> inputs;
    inputs.reserve(scene.meshes.size() + particle_sources.size() + sprites_.size());
    for (const auto& mesh : scene.meshes)
        inputs.push_back({&mesh, scope(mesh)});
    for (const auto& mesh : particle_sources)
        inputs.push_back({&mesh, DrawScope::Particle});
    for (std::size_t i = 0; i < sprites_.size(); ++i)
        inputs.push_back({&sprites_[i], DrawScope::Sprite, &scene.sprites[i].renderer});
    std::set<DrawOwner> used;
    for (const auto& input : inputs)
        used.insert({input.mesh->entity, input.scope});
    // Reclaim deleted instances before admitting this frame's replacements.
    std::erase_if(entries_, [&](const auto& pair) { return !used.contains(pair.first); });
    std::erase_if(bundles_, [](const auto& row) { return row.second.expired(); });
    auto resident = pose_payload_bytes();
    auto material_resident = material_payload_bytes();
    for (const auto& input : inputs) {
        const auto& mesh = *input.mesh;
        auto& entry = entries_[{mesh.entity, input.scope}];
        const auto previous_bytes = entry.pose_bytes();
        const auto previous_material_bytes = entry.material_bytes();
        if (input.sprite && !entry.sprite) {
            if (sizeof(SpriteValues) >
                material_budget_ - (material_resident - previous_material_bytes)) {
                report(mesh.entity, "Sprite values exceed the scene payload budget", input.scope);
                continue;
            }
            entry.sprite = std::make_unique<SpriteValues>();
        }
        const auto admit_parameters = [&](const auto& requested, const auto& admitted) {
            const auto other = material_resident - previous_material_bytes;
            const auto own = parameter_bytes(requested) + parameter_bytes(admitted) +
                             (entry.sprite ? sizeof(SpriteValues) : 0);
            if (own > material_budget_ - other)
                throw std::runtime_error("Object material values exceed the scene payload budget");
        };
        auto available = [&] {
            const auto other = resident - previous_bytes;
            const auto own = entry.pose_bytes();
            if (own > pose_budget_ - other)
                throw std::runtime_error("Retained mesh poses exceed the scene payload budget");
            return pose_budget_ - other - own;
        };
        bool adopted = false;
        const auto& renderer = mesh.renderer;
        const bool values_changed =
            !entry.parameters_checked ||
            !detail::same_material_parameter_values(entry.requested_parameters,
                                                    renderer.material_parameters) ||
            (input.sprite && (!entry.sprite->checked || entry.sprite->requested != *input.sprite));
        bool parameters_observed = true;
        if (values_changed)
            try {
                detail::validate_material_parameter_overrides(renderer.material_parameters);
                if (input.sprite)
                    detail::validate_sprite_renderer(*input.sprite);
                auto requested = renderer.material_parameters;
                admit_parameters(requested, entry.parameters);
                entry.requested_parameters = std::move(requested);
                entry.parameters_checked = true;
                if (input.sprite) {
                    entry.sprite->requested = *input.sprite;
                    entry.sprite->checked = true;
                }
                entry.parameter_error.clear();
            } catch (const std::exception& error) {
                parameters_observed = false;
                if (entry.sprite)
                    entry.sprite->checked = false;
                entry.parameter_error = error.what();
            }
        const bool resource_changed = entry.epoch != host_->epoch_ || entry.mesh != renderer.mesh ||
                                      entry.overrides != renderer.materials ||
                                      entry.variant != renderer.material_variant;
        if (resource_changed) {
            entry.resource_current = false;
            entry.candidate.reset();
            entry.candidate_geometry.reset();
            entry.failed_skin_mode.reset();
            entry.epoch = host_->epoch_;
            entry.mesh = renderer.mesh;
            entry.overrides = renderer.materials;
            entry.variant = renderer.material_variant;
            entry.error.clear();
            changed = true;
        }
        if (parameters_observed &&
            (resource_changed || (!entry.resource_current && !entry.candidate && values_changed))) {
            try {
                entry.candidate = std::make_unique<asset_detail::ModelDrawCandidate>(
                    host_->project_, host_->catalog_, host_->epoch_, renderer.mesh,
                    renderer.materials, host_->meshes_, host_->material_preview_,
                    renderer.material_variant,
                    input.scope != DrawScope::Sprite && (!entry.ready || entry.fallback),
                    host_->mesh_preview_);
            } catch (const std::exception& e) {
                entry.error = e.what();
            }
        }
        if (entry.candidate && parameters_observed) {
            entry.candidate->advance(host_->epoch_, host_->meshes_, host_->materials_,
                                     host_->textures_);
            if (const auto* candidate = entry.candidate->ready()) {
                bool pose_validated = false;
                bool parameters_validated = false;
                try {
                    auto parameters = asset_detail::prepare_model_material_parameters(
                        *candidate, entry.requested_parameters);
                    admit_parameters(entry.requested_parameters, parameters);
                    parameters_validated = true;
                    std::optional<SpriteDrawValues> sprite_values;
                    std::uint32_t texture_width = 0, texture_height = 0;
                    auto draw_mesh = mesh;
                    if (input.sprite) {
                        const auto& source = asset_detail::sprite_source_texture(*candidate);
                        texture_width = source.width;
                        texture_height = source.height;
                        sprite_values = sprite_draw_values(entry.sprite->requested, texture_width,
                                                           texture_height);
                        draw_mesh.world =
                            sprite_world_transform(entry.sprite->requested, mesh.world);
                    }
                    if (!entry.candidate_geometry)
                        entry.candidate_geometry = std::make_shared<const MeshPoseGeometry>(
                            prepare_mesh_pose_geometry(candidate->mesh->mesh, available()));
                    auto pose =
                        prepare_mesh_instance_pose(candidate->mesh.get(), *entry.candidate_geometry,
                                                   draw_mesh, model_index, available());
                    pose_validated = true;
                    auto native = prepare_bundle(*candidate, pose.skinned, entry.candidate_geometry,
                                                 input.sprite ? MeshDrawProfile::Sprite
                                                              : MeshDrawProfile::Mesh);
                    native->prepare_material_parameters(parameters);
                    std::vector<float> thresholds;
                    for (const auto& lod : candidate->mesh->mesh.lods)
                        thresholds.push_back(lod.screen_coverage);
                    native->environment(environment_ready_);
                    entry.ready = std::move(native);
                    entry.parameters = std::move(parameters);
                    entry.parameter_error.clear();
                    entry.resource_current = true;
                    entry.pose = std::move(pose);
                    if (sprite_values) {
                        entry.sprite->admitted = entry.sprite->requested;
                        entry.sprite->draw = *sprite_values;
                        entry.sprite->texture_width = texture_width;
                        entry.sprite->texture_height = texture_height;
                        entry.sprite->ready = true;
                    }
                    entry.thresholds = std::move(thresholds);
                    entry.error = entry.candidate->diagnostic();
                    entry.fallback = candidate->has_fallbacks();
                    entry.pose_error.clear();
                    adopted = true;
                    entry.candidate.reset();
                    entry.candidate_geometry.reset();
                    changed = true;
                } catch (const DrawCapacityError& e) {
                    entry.error = e.what(); // Retry when another bundle releases capacity.
                } catch (const std::exception& e) {
                    entry.error = e.what();
                    if (!parameters_validated) {
                        entry.parameter_error = entry.error;
                        entry.error.clear();
                        entry.candidate.reset();
                        entry.candidate_geometry.reset();
                    } else if (pose_validated) {
                        bool retry = false;
                        if (!input.sprite && (!entry.ready || entry.fallback))
                            try {
                                retry = entry.candidate->error_surface(e.what(), host_->materials_);
                            } catch (const std::exception& fallback_error) {
                                entry.error += std::string("; error surface unavailable: ") +
                                               fallback_error.what();
                            }
                        if (!retry) {
                            entry.candidate.reset();
                            entry.candidate_geometry.reset();
                        }
                    }
                }
                // A temporarily unavailable binding/revision remains retryable.
            } else if (entry.candidate->state() != ResourceState::Loading) {
                entry.error = entry.candidate->diagnostic();
                entry.candidate.reset();
            }
        }
        if (entry.ready && entry.resource_current && !adopted &&
            (values_changed || !entry.parameter_error.empty()) && parameters_observed) {
            try {
                auto parameters = asset_detail::prepare_model_material_parameters(
                    entry.ready->prepared(), entry.requested_parameters);
                admit_parameters(entry.requested_parameters, parameters);
                std::optional<SpriteDrawValues> sprite_values;
                std::optional<MeshInstancePose> sprite_pose;
                if (input.sprite) {
                    sprite_values =
                        sprite_draw_values(entry.sprite->requested, entry.sprite->texture_width,
                                           entry.sprite->texture_height);
                    auto draw_mesh = mesh;
                    draw_mesh.world = sprite_world_transform(entry.sprite->requested, mesh.world);
                    if (draw_mesh.world != entry.pose.world || !entry.pose_error.empty()) {
                        sprite_pose = prepare_mesh_instance_pose(entry.ready->prepared().mesh.get(),
                                                                 entry.ready->geometry(), draw_mesh,
                                                                 model_index, available());
                        if (sprite_pose->skinned)
                            throw std::runtime_error(
                                "Sprite pose must remain static Quad geometry");
                    }
                }
                entry.ready->prepare_material_parameters(parameters);
                changed |= !detail::same_material_parameter_values(entry.parameters, parameters);
                entry.parameters = std::move(parameters);
                if (sprite_values) {
                    changed |= entry.sprite->admitted != entry.sprite->requested ||
                               (sprite_pose && entry.pose != *sprite_pose);
                    entry.sprite->admitted = entry.sprite->requested;
                    entry.sprite->draw = *sprite_values;
                    entry.sprite->ready = true;
                    if (sprite_pose)
                        entry.pose = std::move(*sprite_pose);
                    entry.pose_error.clear();
                    adopted = true;
                }
                entry.parameter_error.clear();
            } catch (const std::exception& error) {
                entry.parameter_error = error.what();
            }
        }
        if (entry.ready && !adopted) {
            try {
                auto draw_mesh = mesh;
                if (input.sprite) {
                    if (!entry.sprite->ready)
                        throw std::runtime_error("Sprite has no admitted numeric values");
                    draw_mesh.world = sprite_world_transform(entry.sprite->admitted, mesh.world);
                }
                // Sprite always uses the admitted immutable, unskinned Quad. Its
                // pose can change only with this matrix; ordinary model binding
                // and morph/skin validation continue on every update below.
                if (!input.sprite || draw_mesh.world != entry.pose.world ||
                    !entry.pose_error.empty()) {
                    auto pose = prepare_mesh_instance_pose(entry.ready->prepared().mesh.get(),
                                                           entry.ready->geometry(), draw_mesh,
                                                           model_index, available());
                    if (pose.skinned != entry.ready->skinned()) {
                        if (entry.failed_skin_mode == pose.skinned)
                            throw std::runtime_error(entry.pose_error);
                        entry.failed_skin_mode = pose.skinned;
                        auto native = prepare_bundle(
                            entry.ready->prepared(), pose.skinned, entry.ready->geometry_owner(),
                            input.sprite ? MeshDrawProfile::Sprite : MeshDrawProfile::Mesh);
                        native->prepare_material_parameters(entry.parameters);
                        entry.ready = std::move(native);
                    }
                    entry.failed_skin_mode.reset();
                    changed |= pose != entry.pose;
                    entry.pose = std::move(pose);
                }
                entry.pose_error.clear();
            } catch (const DrawCapacityError& e) {
                entry.failed_skin_mode.reset();
                entry.pose_error = e.what();
            } catch (const std::exception& e) {
                entry.pose_error = e.what();
            }
        }
        resident = resident - previous_bytes + entry.pose_bytes();
        material_resident = material_resident - previous_material_bytes + entry.material_bytes();
        if (!entry.parameter_error.empty())
            report(mesh.entity,
                   entry.parameter_error + (input.sprite
                                                ? " (previous complete Sprite values retained)"
                                                : " (previous complete material values retained)"),
                   input.scope);
        if (!entry.pose_error.empty())
            report(mesh.entity, entry.pose_error + " (previous complete pose retained)",
                   input.scope);
        if (!entry.error.empty())
            report(mesh.entity,
                   entry.error + (entry.ready ? entry.fallback
                                                    ? " (fallback draw active)"
                                                    : " (previous complete draw retained)"
                                              : ""),
                   input.scope);
        if (entry.ready)
            for (const auto& key : entry.ready->unresolved_slots())
                report(mesh.entity, "Authored material slot is absent from this revision: " + key,
                       input.scope);
    }
    std::erase_if(entries_, [&](const auto& pair) { return !used.contains(pair.first); });
    update_particle_instances(scene);
    return changed;
}
bool MeshSceneRenderer::update_environment(const RenderScene& scene) {
    bool changed = false;
    const auto source = scene.settings.environment.texture;
    auto adopt = [&](EnvironmentLease next) {
        for (auto& [id, entry] : entries_) {
            (void)id;
            if (entry.ready)
                entry.ready->environment(next);
        }
        environment_ready_ = std::move(next);
        changed = true;
    };
    if (source != environment_source_ || environment_epoch_ != host_->epoch_) {
        environment_source_ = source;
        environment_epoch_ = host_->epoch_;
        environment_candidate_.reset();
        environment_error_.clear();
        changed = true;
        if (!source.id)
            adopt({});
        else
            try {
                environment_candidate_ = asset_detail::request_texture(
                    host_->textures_, host_->project_, host_->catalog_, source);
            } catch (const std::exception& e) {
                environment_error_ = e.what();
            }
    }
    if (environment_candidate_) {
        const auto info = environment_candidate_->inspect();
        if (info.state == ResourceState::Ready) {
            try {
                const auto cpu = host_->textures_.acquire(*environment_candidate_);
                if (!cpu)
                    throw std::runtime_error("Environment selection became stale before adoption");
                auto next = host_->environments_.acquire(cpu);
                adopt(std::move(next));
            } catch (const std::exception& e) {
                environment_error_ = e.what();
            }
            environment_candidate_.reset();
        } else if (info.state != ResourceState::Queued && info.state != ResourceState::Loading &&
                   info.state != ResourceState::DependencyPending &&
                   info.state != ResourceState::Replacing) {
            environment_error_ =
                info.diagnostic.empty() ? resource_state_name(info.state) : info.diagnostic;
            environment_candidate_.reset();
        }
    }
    if (!environment_error_.empty()) {
        if (diagnostics_.size() < 256) {
            Diagnostic error{Severity::Error,
                             "render.environment.resource",
                             environment_error_.substr(0, 4000) +
                                 (environment_ready_ ? " (previous environment retained)" : ""),
                             {}};
            error.context.asset = scene.scene;
            error.context.property = "rendering.environment.texture";
            error.context.source = "presentation";
            diagnostics_.push_back(std::move(error));
        } else
            ++omitted_;
    }
    return changed;
}
bool MeshSceneRenderer::pending() const {
    host_->check_thread();
    const bool particles =
        std::any_of(particle_sources_.begin(), particle_sources_.end(), [](const auto& row) {
            const auto& source = row.second;
            const auto awaiting = [](const ResourceTicket& ticket, bool acquired) {
                if (!ticket || acquired)
                    return false;
                const auto state = ticket.inspect().state;
                return state == ResourceState::Ready || !resource_detail::terminal(state);
            };
            return awaiting(source.effect_ticket, bool(source.effect_lease)) ||
                   awaiting(source.mesh_ticket, bool(source.mesh_lease));
        });
    return particles || environment_candidate_.has_value() ||
           std::any_of(entries_.begin(), entries_.end(), [](const auto& pair) {
               return pair.second.candidate &&
                      pair.second.candidate->state() == ResourceState::Loading;
           });
}
void MeshSceneRenderer::draw(const RenderScene& scene, const CameraView& camera,
                             std::uint32_t layers, Diligent::ITexture* color,
                             Diligent::ITextureView* depth) {
    host_->check_thread();
    if (scene.scene != scene_)
        throw std::runtime_error("Mesh draw scene differs from prepared scene");
    struct Object {
        const RenderMesh* mesh;
        MeshDrawBundle* bundle;
        const MeshInstancePose* pose;
        std::span<const MaterialParameterOverride> parameters;
        unsigned lod;
        std::vector<LightView> lights;
        std::vector<int> shadow_slots;
        const SpriteDrawValues* sprite = nullptr;
        DrawScope scope = DrawScope::Mesh;
    };
    struct Item {
        RenderSortKey key;
        std::size_t object;
        bool transmission;
    };
    std::vector<Object> objects;
    std::vector<Item> queue;
    auto append = [&](const RenderMesh& mesh, DrawScope draw_scope,
                      const MeshInstancePose* instance_pose = nullptr) {
        if (!mesh.renderer.visible || !(mesh.renderer.layers & layers))
            return;
        const auto found = entries_.find({mesh.entity, draw_scope});
        if (found == entries_.end() || !found->second.ready)
            return;
        auto& entry = found->second;
        const auto& pose = instance_pose ? *instance_pose : entry.pose;
        const auto queue_start = queue.size();
        try {
            const auto bounds = mesh_instance_bounds(pose, camera.position);
            if (!bounds_visible(bounds, camera))
                return;
            const float coverage = bounds_screen_coverage(bounds, camera);
            unsigned lod = 0;
            for (unsigned i = 0; i < entry.thresholds.size(); ++i)
                if (coverage <= entry.thresholds[i])
                    lod = i;
            Object object{&mesh, entry.ready.get(), &pose, entry.parameters, lod, {}, {}};
            object.scope = draw_scope;
            if (draw_scope == DrawScope::Sprite) {
                if (!entry.sprite || !entry.sprite->ready)
                    throw std::runtime_error("Sprite has no complete admitted draw values");
                object.sprite = &entry.sprite->draw;
            }
            for (const auto& light : scene.lights)
                if (!object.sprite && (light.light.layers & mesh.renderer.layers & layers)) {
                    if (object.lights.size() == mesh_draw_light_limit)
                        throw std::runtime_error(
                            "Visible light list exceeds the current draw profile");
                    object.lights.push_back(light.light);
                    object.shadow_slots.push_back(
                        mesh.renderer.receive_shadows ? shadows_->selection(light.entity) : -1);
                }
            const auto parts = entry.ready->parts(lod);
            for (unsigned i = 0; i < parts.size(); ++i) {
                const auto& part = parts[i];
                const auto part_bounds = mesh_part_bounds(pose, lod, i, camera.position);
                if (!bounds_visible(part_bounds, camera))
                    continue;
                RenderSortKey key{part.transmission ? MaterialAlpha::Blend : part.alpha,
                                  pose.skinned ? TransformParity::Positive
                                               : transform_parity(pose.world),
                                  part.material,
                                  entry.ready->mesh_identity().asset,
                                  bounds_camera_depth(part_bounds, camera),
                                  mesh.entity,
                                  i,
                                  mesh.particle_source,
                                  mesh.birth};
                if (object.sprite) {
                    key.order = entry.sprite->admitted.order;
                    key.sprite = true;
                }
                validate_render_key(key);
                queue.push_back({key, objects.size(), part.transmission});
            }
            objects.push_back(std::move(object));
        } catch (const std::exception& e) {
            queue.resize(queue_start);
            report(mesh.entity, e.what(), draw_scope);
        }
    };
    for (const auto& mesh : scene.meshes)
        append(mesh, scope(mesh));
    for (const auto& [mesh, pose] : particle_instances_)
        append(mesh, DrawScope::Particle, &pose);
    for (const auto& mesh : sprites_)
        append(mesh, DrawScope::Sprite);
    std::sort(queue.begin(), queue.end(),
              [](const auto& a, const auto& b) { return render_key_less(a.key, b.key); });
    EnvironmentLighting lighting;
    if (environment_ready_)
        lighting = {&environment_ready_.get(), scene.settings.environment.intensity,
                    scene.settings.environment.rotation};
    const bool needs_background =
        std::any_of(queue.begin(), queue.end(), [](const auto& item) { return item.transmission; });
    TransmissionLighting transmission;
    bool captured = false;
    auto clear_bindings = [&] {
        for (auto& [id, entry] : entries_) {
            (void)id;
            if (entry.ready)
                entry.ready->transmission(nullptr);
        }
    };
    // Release all old SRB references before reusing a previous camera's snapshot
    // as a copy destination. No sampling/write feedback and no invisible retainers.
    clear_bindings();
    if (!needs_background)
        transmission_->clear();
    draw_stats_ = {};
    for (std::size_t cursor = 0; cursor < queue.size();) {
        const auto& item = queue[cursor++];
        if (needs_background && !captured && item.key.alpha == MaterialAlpha::Blend) {
            captured = true;
            try {
                if (!color || !depth)
                    throw std::runtime_error("Transmission requires HDR color and depth targets");
                transmission = transmission_->capture(host_->context_, color, camera.viewport);
            } catch (const std::exception& error) {
                report({}, error.what());
            }
            if (color && depth) {
                auto* rtv = color->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
                host_->context_->SetRenderTargets(
                    1, &rtv, depth, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                const auto& v = camera.viewport;
                Diligent::Viewport area{float(v.x),      float(v.y), float(v.width),
                                        float(v.height), 0,          1};
                host_->context_->SetViewports(1, &area, color->GetDesc().Width,
                                              color->GetDesc().Height);
            }
        }
        const auto& object = objects[item.object];
        const auto batch_start = cursor - 1;
        try {
            std::array<MeshDraw::Instance, MeshDraw::instance_limit> batch;
            std::size_t count = 1;
            batch[0] = {object.pose->world, object.mesh->legacy_tint, object.sprite};
            const bool eligible = (item.key.alpha != MaterialAlpha::Blend || object.sprite) &&
                                  !item.transmission &&
                                  object.bundle->supports_instances(object.lod, item.key.part);
            while (eligible && cursor < queue.size() && count < batch.size()) {
                const auto& next_item = queue[cursor];
                const auto& next = objects[next_item.object];
                if (next.bundle != object.bundle || next.lod != object.lod ||
                    next_item.key.part != item.key.part ||
                    next_item.key.parity != item.key.parity ||
                    next_item.key.alpha != item.key.alpha || next_item.transmission ||
                    next.mesh->renderer.layers != object.mesh->renderer.layers ||
                    next.mesh->renderer.receive_shadows != object.mesh->renderer.receive_shadows ||
                    !detail::same_material_parameter_values(
                        slot_parameters(object.parameters,
                                        object.bundle->slot(object.lod, item.key.part)),
                        slot_parameters(next.parameters,
                                        next.bundle->slot(next.lod, next_item.key.part))))
                    break;
                batch[count++] = {next.pose->world, next.mesh->legacy_tint, next.sprite};
                ++cursor;
            }
            object.bundle->draw_part(
                host_->context_, object.mesh->world, camera, object.lights, object.lod,
                item.key.part, environment_ready_ ? &lighting : nullptr,
                object.mesh->legacy_tint ? &*object.mesh->legacy_tint : nullptr,
                &shadows_->lighting(), object.shadow_slots,
                transmission.background ? &transmission : nullptr, object.pose,
                count > 1 ? std::span<const MeshDraw::Instance>(batch.data(), count)
                          : std::span<const MeshDraw::Instance>{},
                slot_parameters(object.parameters, object.bundle->slot(object.lod, item.key.part)),
                object.sprite);
            ++draw_stats_.calls;
            draw_stats_.instances += count;
            draw_stats_.batched_calls += count > 1;
        } catch (const std::exception& e) {
            if (cursor == batch_start + 1) {
                report(object.mesh->entity, e.what(), object.scope);
                continue;
            }
            // A rejected batch must not hide its valid neighbors. Individual
            // submissions retain the same complete validation and entity errors.
            for (auto i = batch_start; i < cursor; ++i) {
                const auto& failed_item = queue[i];
                const auto& single = objects[failed_item.object];
                try {
                    single.bundle->draw_part(
                        host_->context_, single.pose->world, camera, single.lights, single.lod,
                        failed_item.key.part, environment_ready_ ? &lighting : nullptr,
                        single.mesh->legacy_tint ? &*single.mesh->legacy_tint : nullptr,
                        &shadows_->lighting(), single.shadow_slots, nullptr, single.pose, {},
                        slot_parameters(single.parameters,
                                        single.bundle->slot(single.lod, failed_item.key.part)),
                        single.sprite);
                    ++draw_stats_.calls;
                    ++draw_stats_.instances;
                } catch (const std::exception& error) {
                    report(single.mesh->entity, error.what(), single.scope);
                }
            }
        }
    }
}
void MeshSceneRenderer::shadows(const RenderScene& scene, const CameraView& camera,
                                std::uint32_t layers) {
    host_->check_thread();
    if (scene.scene != scene_)
        throw std::runtime_error("Shadow scene differs from prepared scene");
    struct Caster {
        const RenderMesh* source;
        MeshDrawBundle* bundle;
        const MeshInstancePose* pose;
        std::span<const MaterialParameterOverride> parameters;
    };
    std::vector<Caster> casters;
    std::vector<ShadowCasterBounds> bounds;
    for (const auto& mesh : scene.meshes) {
        if (!mesh.renderer.visible || !mesh.renderer.cast_shadows ||
            !(mesh.renderer.layers & layers))
            continue;
        const auto found = entries_.find({mesh.entity, scope(mesh)});
        if (found == entries_.end() || !found->second.ready)
            continue;
        try {
            auto world_bounds = mesh_instance_bounds(found->second.pose, camera.position);
            casters.push_back(
                {&mesh, found->second.ready.get(), &found->second.pose, found->second.parameters});
            bounds.push_back({world_bounds, mesh.renderer.layers});
        } catch (const std::exception& error) {
            report(mesh.entity, error.what());
        }
    }
    shadows_->render(
        host_->context_, scene, camera, layers, bounds,
        [&](const CameraView& view, std::uint32_t mask) {
            for (const auto& caster : casters)
                if (caster.source->renderer.layers & mask) {
                    // Skin arithmetic uses this shadow camera's origin too.
                    if (bounds_visible(mesh_instance_bounds(*caster.pose, view.position), view))
                        caster.bundle->draw_shadow(host_->context_, caster.pose->world, view, 0,
                                                   caster.pose, caster.parameters);
                }
        });
    for (const auto& error : shadows_->diagnostics())
        if (diagnostics_.size() < 256)
            diagnostics_.push_back(error);
        else
            ++omitted_;
    // Replace invisible/unused parity bindings too: a previous camera's maps must
    // not remain retained indefinitely by a mesh that leaves the view.
    for (auto& [entity, entry] : entries_) {
        (void)entity;
        if (entry.ready)
            entry.ready->shadows(&shadows_->lighting());
    }
}
} // namespace forge
