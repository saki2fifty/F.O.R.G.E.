#include "mesh_render_host.hpp"
#include "mesh_draw_shader.hpp"
#include "mesh_pick.hpp"
#include "pbr_material.hpp"
#include "render_projection.hpp"
#include "render_sort.hpp"
#include <set>
namespace forge {
MeshResourceInspection MeshResourceHost::inspect() const {
    check_thread();
    MeshResourceInspection result;
    result.meshes = meshes_.statistics();
    result.materials = materials_.statistics();
    result.textures = textures_.statistics();
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
        const auto found = entries_.find(mesh.entity);
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
    return result;
}
namespace {
struct DrawCapacityError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
} // namespace
EntityId MeshSceneRenderer::pick(const RenderScene& scene, const CameraView& camera, double x,
                                 double y, std::uint32_t layers, double radius) const {
    host_->check_thread();
    MeshPickBudget budget;
    EntityId selected;
    double nearest = 2;
    for (const auto& mesh : scene.meshes) {
        if (!mesh.selectable || !(mesh.renderer.layers & layers))
            continue;
        const auto found = entries_.find(mesh.entity);
        if (found == entries_.end() || !found->second.ready)
            continue;
        const auto& entry = found->second;
        const auto bounds = mesh_instance_bounds(entry.pose, camera.position);
        if (!bounds_visible(bounds, camera))
            continue;
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
            continue;
        const auto& data = entry.ready->prepared().mesh->mesh;
        const auto lod = select_mesh_lod(data, bounds_screen_coverage(bounds, camera));
        const auto& parts = data.lods.at(lod).parts;
        for (unsigned p = 0; p < parts.size(); ++p)
            if (const auto depth = pick_mesh_part(parts[p], entry.pose.lods.at(lod).at(p),
                                                  entry.pose, camera, x, y, budget, radius))
                if (*depth < nearest || (*depth == nearest && mesh.entity < selected)) {
                    nearest = *depth;
                    selected = mesh.entity;
                }
    }
    return selected;
}
MeshResourceHost::MeshResourceHost(DiligentPresentation& presentation,
                                   Diligent::IDeviceContext* context, std::filesystem::path project,
                                   bool isolated_material_preview)
    : presentation_(presentation), context_(context), project_(std::move(project)),
      isolated_material_preview_(isolated_material_preview),
      environments_(presentation.device(), context, 512ull * 1024 * 1024, 16,
                    EnvironmentRealization{&presentation}),
      gpu_meshes_(presentation.device(), context, 512ull * 1024 * 1024),
      gpu_textures_(presentation.device(), context, 512ull * 1024 * 1024) {}
void MeshResourceHost::preview_material(AssetRef<MaterialAsset> ref, MaterialResourceData data) {
    check_thread();
    if (!isolated_material_preview_ || !ref.id || epoch_ == UINT64_MAX)
        throw std::runtime_error("Unsaved material requires an isolated preview host");
    validate_material_bindings(data.values, data.textures);
    (void)prepare_pbr_material(data.values);
    const auto revision = asset_build_digest(
        {{"values", material_values_document(data.values)}, {"textures", data.textures}});
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
                                     std::size_t draw_part_limit)
    : host_(std::move(host)), color_(color), pose_budget_(pose_budget),
      draw_part_limit_(draw_part_limit) {
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
void MeshSceneRenderer::report(EntityId entity, const std::string& text) {
    if (diagnostics_.size() >= 256) {
        ++omitted_;
        return;
    }
    Diagnostic value{Severity::Error, "render.mesh.resource", text.substr(0, 4096), {}};
    value.context.asset = scene_;
    value.context.entity = entity;
    value.context.property = "forge.mesh_renderer";
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
                                  std::shared_ptr<const MeshPoseGeometry> geometry) {
    const auto key = asset_detail::prepared_model_draw_key(prepared, skinned);
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
        color_, Diligent::TEX_FORMAT_D32_FLOAT, skinned, std::move(geometry));
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
    std::set<EntityId> used;
    for (const auto& mesh : scene.meshes)
        used.insert(mesh.entity);
    // Reclaim deleted instances before admitting this frame's replacements.
    std::erase_if(entries_, [&](const auto& pair) { return !used.contains(pair.first); });
    std::erase_if(bundles_, [](const auto& row) { return row.second.expired(); });
    auto resident = pose_payload_bytes();
    for (const auto& mesh : scene.meshes) {
        auto& entry = entries_[mesh.entity];
        const auto previous_bytes = entry.pose_bytes();
        auto available = [&] {
            const auto other = resident - previous_bytes;
            const auto own = entry.pose_bytes();
            if (own > pose_budget_ - other)
                throw std::runtime_error("Retained mesh poses exceed the scene payload budget");
            return pose_budget_ - other - own;
        };
        bool adopted = false;
        const auto& renderer = mesh.renderer;
        if (entry.epoch != host_->epoch_ || entry.mesh != renderer.mesh ||
            entry.overrides != renderer.materials) {
            entry.candidate.reset();
            entry.candidate_geometry.reset();
            entry.failed_skin_mode.reset();
            entry.epoch = host_->epoch_;
            entry.mesh = renderer.mesh;
            entry.overrides = renderer.materials;
            entry.error.clear();
            changed = true;
            try {
                entry.candidate = std::make_unique<asset_detail::ModelDrawCandidate>(
                    host_->project_, host_->catalog_, host_->epoch_, renderer.mesh,
                    renderer.materials, host_->meshes_, host_->material_preview_);
            } catch (const std::exception& e) {
                entry.error = e.what();
            }
        }
        if (entry.candidate) {
            entry.candidate->advance(host_->epoch_, host_->meshes_, host_->materials_,
                                     host_->textures_);
            if (const auto* candidate = entry.candidate->ready()) {
                bool pose_validated = false;
                try {
                    if (!entry.candidate_geometry)
                        entry.candidate_geometry = std::make_shared<const MeshPoseGeometry>(
                            prepare_mesh_pose_geometry(candidate->mesh->mesh, available()));
                    auto pose =
                        prepare_mesh_instance_pose(candidate->mesh.get(), *entry.candidate_geometry,
                                                   mesh, model_index, available());
                    pose_validated = true;
                    auto native =
                        prepare_bundle(*candidate, pose.skinned, entry.candidate_geometry);
                    std::vector<float> thresholds;
                    for (const auto& lod : candidate->mesh->mesh.lods)
                        thresholds.push_back(lod.screen_coverage);
                    native->environment(environment_ready_);
                    entry.ready = std::move(native);
                    entry.pose = std::move(pose);
                    entry.thresholds = std::move(thresholds);
                    entry.error.clear();
                    entry.pose_error.clear();
                    adopted = true;
                    entry.candidate.reset();
                    entry.candidate_geometry.reset();
                    changed = true;
                } catch (const DrawCapacityError& e) {
                    entry.error = e.what(); // Retry when another bundle releases capacity.
                } catch (const std::exception& e) {
                    entry.error = e.what();
                    if (pose_validated) {
                        entry.candidate.reset();
                        entry.candidate_geometry.reset();
                    }
                }
                // A temporarily unavailable binding/revision remains retryable.
            } else if (entry.candidate->state() != ResourceState::Loading) {
                entry.error = entry.candidate->diagnostic();
                entry.candidate.reset();
            }
        }
        if (entry.ready && !adopted) {
            try {
                auto pose = prepare_mesh_instance_pose(entry.ready->prepared().mesh.get(),
                                                       entry.ready->geometry(), mesh, model_index,
                                                       available());
                if (pose.skinned != entry.ready->skinned()) {
                    if (entry.failed_skin_mode == pose.skinned)
                        throw std::runtime_error(entry.pose_error);
                    entry.failed_skin_mode = pose.skinned;
                    auto native = prepare_bundle(entry.ready->prepared(), pose.skinned,
                                                 entry.ready->geometry_owner());
                    entry.ready = std::move(native);
                }
                entry.failed_skin_mode.reset();
                changed |= pose != entry.pose;
                entry.pose = std::move(pose);
                entry.pose_error.clear();
            } catch (const DrawCapacityError& e) {
                entry.failed_skin_mode.reset();
                entry.pose_error = e.what();
            } catch (const std::exception& e) {
                entry.pose_error = e.what();
            }
        }
        resident = resident - previous_bytes + entry.pose_bytes();
        if (!entry.pose_error.empty())
            report(mesh.entity, entry.pose_error + " (previous complete pose retained)");
        if (!entry.error.empty())
            report(mesh.entity,
                   entry.error + (entry.ready ? " (previous complete draw retained)" : ""));
        if (entry.ready)
            for (const auto& key : entry.ready->unresolved_slots())
                report(mesh.entity, "Authored material slot is absent from this revision: " + key);
    }
    std::erase_if(entries_, [&](const auto& pair) { return !used.contains(pair.first); });
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
    return environment_candidate_.has_value() ||
           std::any_of(entries_.begin(), entries_.end(),
                       [](const auto& pair) { return bool(pair.second.candidate); });
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
        unsigned lod;
        std::vector<LightView> lights;
        std::vector<int> shadow_slots;
    };
    struct Item {
        RenderSortKey key;
        std::size_t object;
        bool transmission;
    };
    std::vector<Object> objects;
    std::vector<Item> queue;
    for (const auto& mesh : scene.meshes) {
        if (!mesh.renderer.visible || !(mesh.renderer.layers & layers))
            continue;
        const auto found = entries_.find(mesh.entity);
        if (found == entries_.end() || !found->second.ready)
            continue;
        auto& entry = found->second;
        const auto queue_start = queue.size();
        try {
            const auto bounds = mesh_instance_bounds(entry.pose, camera.position);
            if (!bounds_visible(bounds, camera))
                continue;
            const float coverage = bounds_screen_coverage(bounds, camera);
            unsigned lod = 0;
            for (unsigned i = 0; i < entry.thresholds.size(); ++i)
                if (coverage <= entry.thresholds[i])
                    lod = i;
            Object object{&mesh, entry.ready.get(), &entry.pose, lod, {}, {}};
            for (const auto& light : scene.lights)
                if (light.light.layers & mesh.renderer.layers & layers) {
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
                const auto part_bounds = mesh_part_bounds(entry.pose, lod, i, camera.position);
                if (!bounds_visible(part_bounds, camera))
                    continue;
                RenderSortKey key{part.transmission ? MaterialAlpha::Blend : part.alpha,
                                  entry.pose.skinned ? TransformParity::Positive
                                                     : transform_parity(entry.pose.world),
                                  part.material,
                                  entry.ready->mesh_identity().asset,
                                  bounds_camera_depth(part_bounds, camera),
                                  mesh.entity,
                                  i};
                validate_render_key(key);
                queue.push_back({key, objects.size(), part.transmission});
            }
            objects.push_back(std::move(object));
        } catch (const std::exception& e) {
            queue.resize(queue_start);
            report(mesh.entity, e.what());
        }
    }
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
            batch[0] = {object.pose->world, object.mesh->legacy_tint};
            const bool eligible = item.key.alpha != MaterialAlpha::Blend && !item.transmission &&
                                  object.bundle->supports_instances(object.lod, item.key.part);
            while (eligible && cursor < queue.size() && count < batch.size()) {
                const auto& next_item = queue[cursor];
                const auto& next = objects[next_item.object];
                if (next.bundle != object.bundle || next.lod != object.lod ||
                    next_item.key.part != item.key.part ||
                    next_item.key.parity != item.key.parity ||
                    next_item.key.alpha != item.key.alpha || next_item.transmission ||
                    next.mesh->renderer.layers != object.mesh->renderer.layers ||
                    next.mesh->renderer.receive_shadows != object.mesh->renderer.receive_shadows)
                    break;
                batch[count++] = {next.pose->world, next.mesh->legacy_tint};
                ++cursor;
            }
            object.bundle->draw_part(
                host_->context_, object.mesh->world, camera, object.lights, object.lod,
                item.key.part, environment_ready_ ? &lighting : nullptr,
                object.mesh->legacy_tint ? &*object.mesh->legacy_tint : nullptr,
                &shadows_->lighting(), object.shadow_slots,
                transmission.background ? &transmission : nullptr, object.pose,
                count > 1 ? std::span<const MeshDraw::Instance>(batch.data(), count)
                          : std::span<const MeshDraw::Instance>{});
            ++draw_stats_.calls;
            draw_stats_.instances += count;
            draw_stats_.batched_calls += count > 1;
        } catch (const std::exception& e) {
            if (cursor == batch_start + 1) {
                report(object.mesh->entity, e.what());
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
                        &shadows_->lighting(), single.shadow_slots, nullptr, single.pose);
                    ++draw_stats_.calls;
                    ++draw_stats_.instances;
                } catch (const std::exception& error) {
                    report(single.mesh->entity, error.what());
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
    };
    std::vector<Caster> casters;
    std::vector<ShadowCasterBounds> bounds;
    for (const auto& mesh : scene.meshes) {
        if (!mesh.renderer.visible || !mesh.renderer.cast_shadows ||
            !(mesh.renderer.layers & layers))
            continue;
        const auto found = entries_.find(mesh.entity);
        if (found == entries_.end() || !found->second.ready)
            continue;
        try {
            auto world_bounds = mesh_instance_bounds(found->second.pose, camera.position);
            casters.push_back({&mesh, found->second.ready.get(), &found->second.pose});
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
                                                   caster.pose);
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
