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
#include "tile_map_pick.hpp"
#include "tile_map_variants.hpp"
#include "tile_map_view.hpp"
#include <forge/asset_build.hpp>
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
    result.tile_sets = tile_sets_.statistics();
    result.tile_maps = tile_maps_.statistics();
    result.tile_chunks = tile_chunks_.statistics();
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
    append(tile_sets_);
    append(tile_maps_);
    append(tile_chunks_);
    return result;
}
std::optional<RenderBounds> MeshSceneRenderer::bounds() const {
    host_->check_thread();
    std::optional<RenderBounds> result;
    for (const auto& [id, entry] : entries_) {
        if (!entry.ready || id.second == DrawScope::TileMap)
            continue;
        const auto box = mesh_instance_bounds(*entry.pose, {0, 0, 0});
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
    for (const auto& [id, source] : tile_sources_) {
        (void)id;
        for (const auto& [layer, local] : source.bounds) {
            (void)layer;
            const auto box = transform_render_bounds(local, source.requested.world);
            if (!result)
                result = box;
            else
                for (unsigned axis = 0; axis < 3; ++axis) {
                    result->minimum[axis] = std::min(result->minimum[axis], box.minimum[axis]);
                    result->maximum[axis] = std::max(result->maximum[axis], box.maximum[axis]);
                }
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
        const auto box = mesh_instance_bounds(*found->second.pose, origin);
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
        const auto box = mesh_instance_bounds(*found->second.pose, origin);
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
    for (const auto& map : scene.tile_maps) {
        if (selected.empty() ? !map.renderer.visible : !selected.contains(map.entity))
            continue;
        const auto found = tile_sources_.find(map.entity);
        if (found == tile_sources_.end() || !found->second.cells)
            return {};
        const auto& source = found->second;
        for (const auto& layer : source.cells->layers()) {
            if (selected.empty() && !layer.visible)
                continue;
            const auto local = source.bounds.find(layer.id);
            if (local == source.bounds.end())
                continue;
            const auto box = transform_render_bounds(local->second, source.requested.world);
            if (!result)
                result = box;
            else
                for (unsigned axis = 0; axis < 3; ++axis) {
                    result->minimum[axis] = std::min(result->minimum[axis], box.minimum[axis]);
                    result->maximum[axis] = std::max(result->maximum[axis], box.maximum[axis]);
                }
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
    std::int64_t selected_order = 0;
    auto visit = [&](const RenderMesh& mesh, DrawScope draw_scope) {
        if (!mesh.selectable || !(mesh.renderer.layers & layers))
            return;
        const auto found = entries_.find({mesh.entity, draw_scope});
        if (found == entries_.end() || !found->second.ready)
            return;
        const auto& entry = found->second;
        const auto order = entry.sprite && entry.sprite->ready ? entry.sprite->admitted.order : 0;
        const auto bounds = mesh_instance_bounds(*entry.pose, camera.position);
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
            if (const auto depth = pick_mesh_part(parts[p], entry.pose->lods.at(lod).at(p),
                                                  *entry.pose, camera, x, y, budget, radius))
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
    for (const auto& map : scene.tile_maps) {
        if (!map.selectable || !map.renderer.enabled || !(map.renderer.layers & layers))
            continue;
        const auto found = tile_sources_.find(map.entity);
        if (found == tile_sources_.end() || !found->second.cells)
            continue;
        const auto& source = found->second;
        const auto hit =
            pick_tile_grid(source.cells->cell_size(), source.requested.world, camera, x, y);
        if (!hit)
            continue;
        for (const auto& layer : source.cells->layers()) {
            if (!layer.visible || !source.cells->cell(layer.id, hit->cell).tile)
                continue;
            const auto order = std::int64_t(map.renderer.order) + layer.order;
            if (hit->depth < nearest ||
                (hit->depth == nearest &&
                 (order > selected_order || (order == selected_order && map.entity < selected)))) {
                nearest = hit->depth;
                selected = map.entity;
                selected_order = order;
            }
        }
    }
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
MeshResourceHost::SpriteMipIdentity
MeshResourceHost::sprite_mip_identity(const ResourceIdentity& source,
                                      const SpriteFrameMipKey& frame) {
    const nlohmann::json key = {frame.x,
                                frame.y,
                                frame.width,
                                frame.height,
                                frame.canvas_width,
                                frame.canvas_height,
                                frame.trim_x,
                                frame.trim_y,
                                frame.rotated_clockwise};
    return {"sprite-frame:" + asset_build_digest(key),
            asset_build_digest({{"recipe", "forge.sprite-frame-mips.v1"},
                                {"source", source.revision},
                                {"variant", source.variant},
                                {"frame", key}})};
}
ResourceTicket MeshResourceHost::request_sprite_mip(const ResourceLease<TextureAsset>& source,
                                                    const SpriteFrameMipKey& frame,
                                                    std::uint64_t byte_limit) {
    check_thread();
    (void)sprite_frame_mip_bytes(frame, byte_limit);
    // Queue incrementally: one bank can have512 entries while this existing
    // shared pool admits64 pending requests. A full queue is pending, not failure.
    if (textures_.statistics().pending >= texture_limits_.requests)
        return {};
    std::erase_if(sprite_mip_inputs_, [](const auto& entry) { return entry.second.expired(); });
    const auto input_key = std::pair{source.identity(), frame};
    std::shared_ptr<const SpriteFrameMipInput> input;
    if (const auto found = sprite_mip_inputs_.find(input_key); found != sprite_mip_inputs_.end())
        input = found->second.lock();
    if (!input) {
        constexpr std::uint64_t input_limit = 128ull * 1024 * 1024;
        const auto wanted = std::uint64_t(frame.width) * frame.height * 4;
        if (wanted > input_limit)
            throw std::runtime_error("Sprite mip copied pixels exceed their input budget");
        std::uint64_t retained = 0;
        for (const auto& [key, weak] : sprite_mip_inputs_) {
            (void)key;
            if (const auto value = weak.lock())
                retained += value->pixels.byte_size();
        }
        if (retained > input_limit - wanted)
            return {};
        input = std::make_shared<const SpriteFrameMipInput>(
            copy_sprite_frame_mip_input(source.get(), frame, byte_limit));
        sprite_mip_inputs_[input_key] = input;
    }
    const auto identity = sprite_mip_identity(source.identity(), frame);
    const auto generation = source.identity().generation;
    return textures_.request(
        {source.identity().asset}, identity.revision, generation,
        [input = std::move(input), byte_limit](std::stop_token stop) {
            auto image = std::make_unique<TextureData>(
                prepare_sprite_frame_mips(input->pixels, input->frame, byte_limit, stop));
            const auto bytes = image->resident_bytes();
            return ResourceCandidate<TextureAsset>{std::move(image), ResourceMemory{bytes}};
        },
        {}, 0, identity.variant);
}
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
    tile_sets_.pump();
    tile_maps_.pump();
    pump_tile_chunks();
    tile_sets_.collect();
    tile_maps_.collect();
    environments_.collect();
    gpu_meshes_.collect();
    gpu_textures_.collect();
}
ResourceTicket MeshResourceHost::request_tile_map_values(AssetRef<TileMapAsset> asset) {
    check_thread();
    if (!catalog_)
        throw std::runtime_error("Tile Map inspection requires a project catalog");
    return request_tile_map(tile_maps_, project_, catalog_, asset);
}
ResourceLease<TileMapAsset> MeshResourceHost::tile_map_values(const ResourceTicket& ticket) {
    check_thread();
    return tile_maps_.acquire(ticket);
}
ResourceTicket MeshResourceHost::request_tile_set_values(AssetRef<TileSetAsset> asset) {
    check_thread();
    if (!catalog_)
        throw std::runtime_error("Tile Set inspection requires a project catalog");
    return request_tile_set(tile_sets_, project_, catalog_, asset);
}
ResourceLease<TileSetAsset> MeshResourceHost::tile_set_values(const ResourceTicket& ticket) {
    check_thread();
    return tile_sets_.acquire(ticket);
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
    weighted_ = std::make_unique<WeightedTransparency>(host_->presentation_);
}
std::shared_ptr<const MeshInstancePose>
MeshSceneRenderer::Entry::stage_pose(MeshInstancePose value) const {
    if (pose && *pose == value)
        return pose;
    if (previous_pose && *previous_pose == value)
        return previous_pose;
    return std::make_shared<const MeshInstancePose>(std::move(value));
}
void MeshSceneRenderer::commit_temporal_frame(bool enabled) {
    host_->check_thread();
    for (auto& [owner, entry] : entries_) {
        if (enabled && owner.second == DrawScope::Mesh && entry.ready) {
            entry.previous_mesh = entry.ready->mesh_identity();
            entry.previous_pose = entry.pose;
        } else {
            entry.previous_pose.reset();
            entry.previous_mesh.reset();
        }
    }
}
std::uint64_t MeshSceneRenderer::Entry::pose_bytes() const {
    std::uint64_t bytes = pose ? mesh_pose_bytes(*pose) : 0;
    if (previous_pose && previous_pose != pose)
        bytes += mesh_pose_bytes(*previous_pose);
    if (ready)
        bytes += mesh_pose_bytes(ready->geometry());
    if (candidate_geometry && (!ready || candidate_geometry != ready->geometry_owner()))
        bytes += mesh_pose_bytes(*candidate_geometry);
    return bytes;
}
std::uint64_t MeshSceneRenderer::Entry::material_bytes() const {
    return parameter_bytes(requested_parameters) + parameter_bytes(parameters) +
           (sprite ? sprite->metadata_bytes() : 0);
}
std::uint64_t MeshSceneRenderer::material_payload_bytes() const {
    host_->check_thread();
    std::uint64_t bytes = tile_material_bytes();
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
    std::uint64_t bytes = tile_worlds_.capacity() * sizeof(AffineTransform);
    if (bytes > pose_budget_)
        throw std::runtime_error("Tile draw transforms exceed the scene payload budget");
    for (const auto& [id, entry] : entries_) {
        (void)id;
        const auto count = entry.pose_bytes();
        if (count > pose_budget_ - bytes)
            throw std::runtime_error("Retained mesh poses exceed the scene payload budget");
        bytes += count;
    }
    return bytes;
}
void MeshSceneRenderer::report(Diagnostic value) {
    if (diagnostics_.size() >= 256) {
        ++omitted_;
        return;
    }
    diagnostics_.push_back(std::move(value));
}
void MeshSceneRenderer::report(EntityId entity, const std::string& text, DrawScope draw_scope) {
    Diagnostic value{Severity::Error,
                     draw_scope == DrawScope::Sprite     ? "render.sprite.resource"
                     : draw_scope == DrawScope::TileMap  ? "render.tile_map.resource"
                     : draw_scope == DrawScope::Particle ? "render.particles.resource"
                                                         : "render.mesh.resource",
                     text.substr(0, 4096),
                     {}};
    value.context.asset = scene_;
    value.context.entity = entity;
    value.context.property = draw_scope == DrawScope::Sprite     ? "forge.sprite_renderer"
                             : draw_scope == DrawScope::TileMap  ? "forge.tile_map_renderer"
                             : draw_scope == DrawScope::Particle ? "forge.particle_emitter"
                                                                 : "forge.mesh_renderer";
    value.context.source = "presentation";
    report(std::move(value));
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
            admit(live->draw_part_count());
    }
    auto next = std::make_shared<MeshDrawBundle>(
        host_->presentation_, host_->context_, prepared, host_->gpu_meshes_, host_->gpu_textures_,
        color_, Diligent::TEX_FORMAT_D32_FLOAT, skinned, std::move(geometry), profile);
    next->environment(environment_.ready);
    bundles_[key] = next;
    return next;
}
bool MeshSceneRenderer::update(const RenderScene& scene,
                               std::span<const PreparedCamera> tile_views) {
    host_->pump();
    bool changed = false;
    diagnostics_ = scene.diagnostics;
    omitted_ = scene.omitted_diagnostics;
    if (scene_ != scene.scene) {
        entries_.clear();
        sprites_.clear();
        tile_images_.clear();
        tile_sources_.clear();
        tile_variant_bytes_.reset();
        std::vector<AffineTransform>{}.swap(tile_worlds_);
        particle_sources_.clear();
        particle_instances_.clear();
        particle_bounds_.clear();
        bundles_.clear();
        decal_entries_.clear();
        decal_order_.clear();
        light_image_entries_.clear();
        weighted_->clear();
        environment_ = {};
        probe_entries_.clear();
        lighting_probes_.clear();
        scene_ = scene.scene;
        changed = true;
    }
    changed |= update_environment(scene);
    changed |= update_reflection_probes(scene);
    changed |= update_decals(scene);
    changed |= update_light_images(scene);
    const ModelSceneIndex model_index(scene);
    auto particle_sources = prepare_particle_sources(scene);
    tile_images_ = prepare_tile_sources(scene);
    changed |= prepare_tile_chunks(tile_views);
    sprites_.resize(scene.sprites.size());
    for (std::size_t i = 0; i < scene.sprites.size(); ++i)
        update_sprite_mesh(sprites_[i], scene.sprites[i]);
    struct Input {
        const RenderMesh* mesh;
        DrawScope scope;
        const SpriteRenderer* sprite = nullptr;
        std::uint64_t image = 0;
    };
    std::vector<Input> inputs;
    inputs.reserve(scene.meshes.size() + particle_sources.size() + sprites_.size() +
                   tile_images_.size());
    for (const auto& mesh : scene.meshes)
        inputs.push_back({&mesh, scope(mesh)});
    for (const auto& mesh : particle_sources)
        inputs.push_back({&mesh, DrawScope::Particle});
    for (std::size_t i = 0; i < sprites_.size(); ++i)
        inputs.push_back({&sprites_[i], DrawScope::Sprite, &scene.sprites[i].renderer});
    for (const auto& image : tile_images_)
        inputs.push_back({&image.mesh, DrawScope::TileMap, &image.sprite, image.image});
    std::set<DrawOwner> used;
    for (const auto& input : inputs)
        used.insert({input.mesh->entity, input.scope, input.image});
    // Reclaim deleted instances before admitting this frame's replacements.
    std::erase_if(entries_, [&](const auto& pair) { return !used.contains(pair.first); });
    std::erase_if(bundles_, [](const auto& row) { return row.second.expired(); });
    auto resident = pose_payload_bytes();
    auto material_resident = material_payload_bytes();
    for (const auto& input : inputs) {
        const auto& mesh = *input.mesh;
        auto& entry = entries_[{mesh.entity, input.scope, input.image}];
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
                             (entry.sprite ? entry.sprite->metadata_bytes() : 0);
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
        const bool tile_mips_changed = input.scope == DrawScope::TileMap &&
                                       input.sprite->mip_filter &&
                                       (!entry.sprite->mip_candidate ||
                                        entry.sprite->mip_candidate->definitions !=
                                            tile_sources_.at(mesh.entity).requested.definitions);
        const bool values_changed =
            tile_mips_changed || !entry.parameters_checked ||
            !detail::same_material_parameter_values(entry.requested_parameters,
                                                    renderer.material_parameters) ||
            (input.sprite && (!entry.sprite->checked || entry.sprite->requested != *input.sprite));
        bool parameters_observed = true;
        if (values_changed)
            try {
                if (entry.sprite)
                    entry.sprite->mip_pending = false;
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
            if (entry.sprite) {
                entry.sprite->mip_pending = false;
                entry.sprite->mip_candidate.reset();
            }
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
                    renderer.material_variant, !input.sprite && (!entry.ready || entry.fallback),
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
                    std::shared_ptr<SpriteMipBank> sprite_mips;
                    std::uint32_t texture_width = 0, texture_height = 0, frame_slot = 0;
                    auto draw_mesh = mesh;
                    if (input.sprite) {
                        const auto& source = asset_detail::sprite_source_texture(*candidate);
                        asset_detail::validate_sprite_frame_selection(*candidate,
                                                                      entry.sprite->requested);
                        texture_width = source.width;
                        texture_height = source.height;
                        frame_slot = sprite_texture_frame_slot(entry.sprite->requested, source);
                        sprite_values = sprite_draw_values(entry.sprite->requested, source);
                        sprite_mips = prepare_sprite_mips(entry, *candidate, mesh.entity,
                                                          input.scope, input.image);
                        if (sprite_mips) {
                            const auto& key = sprite_mips->plan.frames.at(frame_slot);
                            sprite_values = sprite_canvas_draw_values(
                                entry.sprite->requested, key.canvas_width, key.canvas_height);
                        }
                        admit_parameters(entry.requested_parameters, parameters);
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
                    auto native = prepare_bundle(
                        *candidate, pose.skinned, entry.candidate_geometry,
                        input.sprite ? sprite_draw_profile(entry.sprite->requested.mip_filter)
                                     : MeshDrawProfile::Mesh);
                    native->prepare_material_parameters(parameters);
                    std::vector<float> thresholds;
                    for (const auto& lod : candidate->mesh->mesh.lods)
                        thresholds.push_back(lod.screen_coverage);
                    native->environment(environment_.ready);
                    auto selected_pose = entry.stage_pose(std::move(pose));
                    entry.ready = std::move(native);
                    entry.parameters = std::move(parameters);
                    entry.parameter_error.clear();
                    entry.resource_current = true;
                    entry.pose = std::move(selected_pose);
                    if (sprite_values) {
                        entry.sprite->admitted = entry.sprite->requested;
                        entry.sprite->draw = *sprite_values;
                        entry.sprite->texture_width = texture_width;
                        entry.sprite->texture_height = texture_height;
                        entry.sprite->frame_slot = frame_slot;
                        entry.sprite->ready = true;
                        entry.sprite->mips = std::move(sprite_mips);
                    }
                    entry.thresholds = std::move(thresholds);
                    entry.error = entry.candidate->diagnostic();
                    entry.fallback = candidate->has_fallbacks();
                    entry.pose_error.clear();
                    adopted = true;
                    entry.candidate.reset();
                    entry.candidate_geometry.reset();
                    changed = true;
                } catch (const SpriteMipsPending&) {
                    // Keep the old complete draw while shared workers finish.
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
            (values_changed ||
             (input.sprite && (!entry.parameter_error.empty() || entry.sprite->mip_pending))) &&
            parameters_observed) {
            try {
                auto parameters = asset_detail::prepare_model_material_parameters(
                    entry.ready->prepared(), entry.requested_parameters);
                admit_parameters(entry.requested_parameters, parameters);
                std::optional<SpriteDrawValues> sprite_values;
                std::shared_ptr<const MeshInstancePose> sprite_pose;
                std::shared_ptr<SpriteMipBank> sprite_mips;
                auto native = entry.ready;
                std::uint32_t frame_slot = 0;
                if (input.sprite) {
                    asset_detail::validate_sprite_frame_selection(entry.ready->prepared(),
                                                                  entry.sprite->requested);
                    const auto& source =
                        asset_detail::sprite_source_texture(entry.ready->prepared());
                    frame_slot = sprite_texture_frame_slot(entry.sprite->requested, source);
                    sprite_values = sprite_draw_values(entry.sprite->requested, source);
                    sprite_mips = prepare_sprite_mips(entry, entry.ready->prepared(), mesh.entity,
                                                      input.scope, input.image);
                    if (sprite_mips) {
                        const auto& key = sprite_mips->plan.frames.at(frame_slot);
                        sprite_values = sprite_canvas_draw_values(
                            entry.sprite->requested, key.canvas_width, key.canvas_height);
                    }
                    admit_parameters(entry.requested_parameters, parameters);
                    const auto profile = sprite_draw_profile(entry.sprite->requested.mip_filter);
                    if (profile != native->profile())
                        native = prepare_bundle(entry.ready->prepared(), false,
                                                entry.ready->geometry_owner(), profile);
                    auto draw_mesh = mesh;
                    draw_mesh.world = sprite_world_transform(entry.sprite->requested, mesh.world);
                    if (draw_mesh.world != entry.pose->world || !entry.pose_error.empty()) {
                        sprite_pose = entry.stage_pose(prepare_mesh_instance_pose(
                            entry.ready->prepared().mesh.get(), entry.ready->geometry(), draw_mesh,
                            model_index, available()));
                        if (sprite_pose->skinned)
                            throw std::runtime_error(
                                "Sprite pose must remain static Quad geometry");
                    }
                }
                native->prepare_material_parameters(parameters);
                changed |= !detail::same_material_parameter_values(entry.parameters, parameters);
                entry.parameters = std::move(parameters);
                entry.ready = std::move(native);
                if (sprite_values) {
                    changed |= entry.sprite->admitted != entry.sprite->requested ||
                               (sprite_pose && entry.pose != sprite_pose);
                    entry.sprite->admitted = entry.sprite->requested;
                    entry.sprite->frame_slot = frame_slot;
                    entry.sprite->draw = *sprite_values;
                    entry.sprite->ready = true;
                    entry.sprite->mips = std::move(sprite_mips);
                    if (sprite_pose)
                        entry.pose = std::move(sprite_pose);
                    entry.pose_error.clear();
                    adopted = true;
                }
                entry.parameter_error.clear();
            } catch (const SpriteMipsPending&) {
                // Numeric intent is retained until its complete bank is ready.
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
                if (!input.sprite || draw_mesh.world != entry.pose->world ||
                    !entry.pose_error.empty()) {
                    auto pose = entry.stage_pose(prepare_mesh_instance_pose(
                        entry.ready->prepared().mesh.get(), entry.ready->geometry(), draw_mesh,
                        model_index, available()));
                    if (pose->skinned != entry.ready->skinned()) {
                        if (entry.failed_skin_mode == pose->skinned)
                            throw std::runtime_error(entry.pose_error);
                        entry.failed_skin_mode = pose->skinned;
                        auto native = prepare_bundle(
                            entry.ready->prepared(), pose->skinned, entry.ready->geometry_owner(),
                            input.sprite ? entry.ready->profile() : MeshDrawProfile::Mesh);
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
    changed |= adopt_tile_images();
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
    const bool tiles = std::any_of(tile_sources_.begin(), tile_sources_.end(),
                                   [](const auto& row) { return row.second.pending; });
    const bool probes =
        std::any_of(probe_entries_.begin(), probe_entries_.end(),
                    [](const auto& row) { return row.second.environment.candidate.has_value(); });
    const bool decals =
        std::any_of(decal_entries_.begin(), decal_entries_.end(), [](const auto& row) {
            return row.second.material_ticket ||
                   std::any_of(row.second.texture_tickets.begin(), row.second.texture_tickets.end(),
                               [](const auto& ticket) { return ticket.has_value(); });
        });
    const bool light_images =
        std::any_of(light_image_entries_.begin(), light_image_entries_.end(),
                    [](const auto& row) { return row.second.ticket.has_value(); });
    return particles || tiles || probes || decals || light_images ||
           environment_.candidate.has_value() ||
           std::any_of(entries_.begin(), entries_.end(), [](const auto& pair) {
               return (pair.second.sprite && pair.second.sprite->mip_pending) ||
                      (pair.second.candidate &&
                       pair.second.candidate->state() == ResourceState::Loading);
           });
}
void MeshSceneRenderer::draw(const RenderScene& scene, const CameraView& camera,
                             std::uint32_t layers, Diligent::ITexture* color,
                             Diligent::ITextureView* depth, TemporalPass* temporal,
                             PostMeshPass* post_mesh) {
    host_->check_thread();
    if (scene.scene != scene_)
        throw std::runtime_error("Mesh draw scene differs from prepared scene");
    validate_scene_distance_fog(scene.settings.distance_fog);
    const auto diagnostics_before = diagnostics_.size(), omitted_before = omitted_;
    std::vector<EntityId> view_decals;
    view_decals.reserve(decal_order_.size());
    for (const auto& id : decal_order_)
        try {
            const auto& projector = decal_entries_.at(id).projector;
            (void)decal_gpu_transform(projector, camera.position);
            if (temporal && temporal->current.position != camera.position)
                (void)decal_gpu_transform(projector, temporal->current.position);
            view_decals.push_back(id);
        } catch (const std::exception& error) {
            Diagnostic value{Severity::Warning,
                             "render.decal.view",
                             std::string(error.what()) +
                                 "; color projection omitted for this view.",
                             {}};
            value.context.asset = scene.scene;
            value.context.entity = id;
            value.context.property = "forge.decal";
            value.context.source = "presentation";
            report(std::move(value));
        }
    std::map<EntityId, MeshDraw::LightImage> view_images;
    for (const auto& light : scene.lights)
        if (light.image && (light.light.layers & layers)) {
            const auto found = light_image_entries_.find(light.entity);
            if (found == light_image_entries_.end() || !found->second.ready)
                continue;
            try {
                (void)light_image_gpu_frame(*light.image, camera.position);
                if (temporal)
                    (void)light_image_gpu_frame(*light.image, temporal->current.position);
                const auto& image = *found->second.ready;
                (void)image.source.get();
                view_images.emplace(
                    light.entity, MeshDraw::LightImage{&*light.image,
                                                       {image.gpu.get()->GetDefaultView(
                                                            Diligent::TEXTURE_VIEW_SHADER_RESOURCE),
                                                        image.sampler},
                                                       &image.source->sampler});
            } catch (const std::exception& error) {
                Diagnostic value{Severity::Warning,
                                 "render.light.image.view",
                                 std::string(error.what()) + " (ordinary light retained)",
                                 {}};
                value.context.asset = scene.scene;
                value.context.entity = light.entity;
                value.context.property = "forge.light.texture";
                value.context.source = "presentation";
                report(std::move(value));
            }
        }
    std::vector<EnvironmentProbe> view_irradiance;
    for (const auto& probe : lighting_probes_) {
        if (probe.diffuse_intensity == 0)
            continue;
        try {
            (void)reflection_probe_view_center(probe.volume, camera.position);
            if (temporal)
                (void)reflection_probe_view_center(probe.volume, temporal->current.position);
            view_irradiance.push_back(probe);
        } catch (const std::exception& error) {
            Diagnostic value{Severity::Warning,
                             "render.irradiance_probe.view",
                             std::string(error.what()) + " (Scene diffuse lighting retained)",
                             {}};
            value.context.asset = scene.scene;
            value.context.entity = probe.entity;
            value.context.property = "forge.reflection_probe.diffuse_intensity";
            value.context.source = "presentation";
            report(std::move(value));
        }
    }
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
        const AffineTransform* world = nullptr;
        const GpuLease<TextureAsset>* mip = nullptr;
        const MeshInstancePose* previous_pose = nullptr;
        bool area_lights = false;
        std::vector<EntityId> decals{};
        RenderBounds bounds{};
        std::vector<MeshDraw::LightImage> images{};
    };
    struct Item {
        RenderSortKey key;
        std::size_t object;
        bool transmission;
    };
    std::vector<Object> objects;
    std::vector<Item> queue;
    const auto require_specialization_capacity = [&] {
        std::size_t count = 0;
        for (const auto& [key, weak] : bundles_) {
            (void)key;
            if (auto bundle = weak.lock()) {
                const auto parts = bundle->draw_part_count();
                if (parts > draw_part_limit_ - count)
                    throw DrawCapacityError("Mesh specialization draw-part budget exceeded");
                count += parts;
            }
        }
        if (count == draw_part_limit_)
            throw DrawCapacityError("Mesh specialization draw-part budget exceeded");
    };
    auto append = [&](const RenderMesh& mesh, DrawScope draw_scope,
                      const MeshInstancePose* instance_pose = nullptr) {
        if (!mesh.renderer.visible || !(mesh.renderer.layers & layers))
            return;
        const auto found = entries_.find({mesh.entity, draw_scope});
        if (found == entries_.end() || !found->second.ready)
            return;
        auto& entry = found->second;
        const auto& pose = instance_pose ? *instance_pose : *entry.pose;
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
            object.bounds = bounds;
            object.world = &pose.world;
            object.scope = draw_scope;
            if (draw_scope == DrawScope::Mesh && entry.previous_pose && entry.previous_mesh &&
                *entry.previous_mesh == entry.ready->mesh_identity() &&
                entry.previous_pose->skinned == pose.skinned)
                object.previous_pose = entry.previous_pose.get();
            if (draw_scope == DrawScope::Sprite) {
                if (!entry.sprite || !entry.sprite->ready)
                    throw std::runtime_error("Sprite has no complete admitted draw values");
                object.sprite = &entry.sprite->draw;
                if (entry.sprite->mips) {
                    const auto& bank = *entry.sprite->mips;
                    object.mip =
                        &bank.frames.at(bank.plan.frames.at(entry.sprite->frame_slot)).image;
                }
            }
            for (const auto& light : scene.lights)
                if (!object.sprite && (light.light.layers & mesh.renderer.layers & layers)) {
                    if (object.lights.size() == mesh_draw_light_limit)
                        throw std::runtime_error(
                            "Visible light list exceeds the current draw profile");
                    object.lights.push_back(light.light);
                    const auto image = view_images.find(light.entity);
                    object.images.push_back(image == view_images.end() ? MeshDraw::LightImage{}
                                                                       : image->second);
                    object.area_lights =
                        object.area_lights || light.light.kind >= unsigned(LightKind::Rectangle);
                    object.shadow_slots.push_back(
                        mesh.renderer.receive_shadows ? shadows_->selection(light.entity) : -1);
                }
            if (!object.sprite)
                for (const auto& id : view_decals) {
                    const auto& projector = decal_entries_.at(id).projector;
                    if (!(projector.values.layers & mesh.renderer.layers & layers))
                        continue;
                    const auto local =
                        transform_render_bounds(bounds, projector.projector_from_world);
                    bool overlaps = true;
                    for (unsigned axis = 0; axis < 3; ++axis)
                        overlaps =
                            overlaps && local.minimum[axis] <= .5 && local.maximum[axis] >= -.5;
                    if (overlaps)
                        object.decals.push_back(id);
                }
            const auto parts = entry.ready->parts(lod);
            for (unsigned i = 0; i < parts.size(); ++i) {
                const auto& part = parts[i];
                const auto part_bounds = mesh_part_bounds(pose, lod, i, camera.position);
                if (!bounds_visible(part_bounds, camera))
                    continue;
                if (object.area_lights) {
                    if (!entry.ready->surface_prepared(lod, i, false, false, false, false, true))
                        require_specialization_capacity();
                    entry.ready->prepare_surface(host_->context_, lod, i, false, false,
                                                 object.parameters, false, false, true);
                }
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
    tile_worlds_.clear();
    const auto tile_visible = [&](const auto& source) {
        if (!source.cells || !source.definitions ||
            (source.streamed && !source.current_chunks_ready) ||
            !source.requested.renderer.visible || !(source.requested.renderer.layers & layers) ||
            !source.visible_cells)
            return false;
        try {
            for (const auto& layer : source.cells->layers())
                if (layer.visible)
                    if (const auto local = source.bounds.find(layer.id);
                        local != source.bounds.end() &&
                        bounds_visible(
                            transform_render_bounds(local->second, source.requested.world), camera))
                        return true;
        } catch (const std::exception& error) {
            report(source.requested.entity, error.what(), DrawScope::TileMap);
        }
        return false;
    };
    const auto visit_tile_cells = [&](auto& source, const auto& visit) {
        const auto visit_chunk = [&](const TileMapChunkView& chunk) {
            const auto& available_layers = source.cells->layers();
            const auto layer =
                std::find_if(available_layers.begin(), available_layers.end(),
                             [&](const auto& value) { return value.id == chunk.key.layer; });
            if (layer == available_layers.end())
                throw std::runtime_error("Tile draw chunk has no layer");
            auto overrides = source.variants ? source.variants->overrides(chunk.key)
                                             : std::span<const TileVariantOverride>{};
            for (std::uint32_t slot = 0; slot < tile_chunk_cells; ++slot) {
                const auto& cell = chunk.cells[slot];
                if (!cell.tile)
                    continue;
                const auto selected_tile = !overrides.empty() && overrides.front().slot == slot
                                               ? overrides.front().tile
                                               : cell.tile;
                if (!overrides.empty() && overrides.front().slot == slot)
                    overrides = overrides.subspan(1);
                const auto& definition = source.definitions->tile(selected_tile);
                auto& image = source.images.at(definition.image);
                const auto world =
                    tile_cell_world(tile_cell_address({chunk.key.address, slot}),
                                    source.cells->cell_size(), source.requested.world);
                const auto& part = image.bundle->parts(0)[0];
                const auto bounds = transform_bounds(part.bounds, world);
                if (!bounds_visible(bounds, camera))
                    continue;
                visit(*layer, cell, selected_tile, definition, image, world, part, bounds);
            }
        };
        if (source.streamed) {
            visit_visible_tile_chunk_info(
                *source.cells, source.requested.world, std::span(&camera, 1),
                [&](const TileMapChunkBounds& chunk) {
                    const auto found = source.current_chunks.find(chunk.key);
                    if (found == source.current_chunks.end() || !found->second.demand ||
                        !found->second.demand->lease)
                        throw std::runtime_error("Tile draw view has no prepared chunk demand");
                    const auto& page = found->second.demand->lease.get();
                    if (page.key != chunk.key || page.occupied != chunk.occupied)
                        throw std::runtime_error("Tile draw chunk does not match its logical map");
                    visit_chunk({page.key, page.cells, chunk.world_bounds});
                });
        } else {
            visit_visible_tile_chunk_values(*source.cells, source.requested.world,
                                            std::span(&camera, 1), visit_chunk);
        }
    };
    std::size_t tile_capacity = 0;
    for (auto& [id, source] : tile_sources_) {
        source.visible_this_view = tile_visible(source);
        if (!source.visible_this_view)
            continue;
        try {
            std::size_t cells = 0;
            // Use the same cell admission for counting and emission. Capacity
            // is fixed before queued objects retain pointers into tile_worlds_.
            visit_tile_cells(
                source, [&](const auto&...) { cells = std::min(tile_map_cell_limit, cells + 1); });
            tile_capacity = std::min(tile_map_cell_limit, tile_capacity + cells);
        } catch (const std::exception& error) {
            source.visible_this_view = false;
            report(id, error.what(), DrawScope::TileMap);
        }
    }
    bool tile_capacity_ready = true;
    try {
        if (tile_capacity > tile_worlds_.capacity()) {
            const auto extra = (tile_capacity - tile_worlds_.capacity()) * sizeof(AffineTransform);
            if (extra > pose_budget_ - pose_payload_bytes())
                throw std::runtime_error("Tile draw transforms exceed the scene payload budget");
            tile_worlds_.reserve(tile_capacity);
        }
    } catch (const std::exception& error) {
        report({}, error.what(), DrawScope::TileMap);
        tile_capacity_ready = false;
    }
    if (tile_capacity_ready)
        for (auto& [entity, source] : tile_sources_) {
            const auto& map = source.requested;
            if (!source.visible_this_view)
                continue;
            const auto queue_start = queue.size(), object_start = objects.size();
            const auto world_start = tile_worlds_.size();
            try {
                std::uint64_t ordinal = 0;
                visit_tile_cells(source, [&](const auto& layer, const auto& cell,
                                             auto selected_tile, const auto& definition,
                                             auto& image, const auto& world, const auto& part,
                                             const auto& bounds) {
                    if (tile_worlds_.size() == tile_map_cell_limit)
                        throw std::runtime_error(
                            "Visible Tile Map cells exceed the 262144-instance profile");
                    const auto key = std::pair{selected_tile, cell.flags};
                    auto values = source.values.find(key);
                    const bool missing = values == source.values.end();
                    const bool animated = definition.animation && definition.animation->enabled;
                    bool refresh = missing;
                    auto selected =
                        missing ? tile_detail::tile_frame_at(definition, source.animation_seconds)
                                : values->second.frame;
                    if (!missing && animated &&
                        values->second.animation_sample != source.animation_seconds) {
                        selected = tile_detail::tile_frame_at(definition, source.animation_seconds);
                        refresh = selected != values->second.frame;
                        if (!refresh)
                            values->second.animation_sample = source.animation_seconds;
                    }
                    if (refresh) {
                        if (missing) {
                            const auto bytes =
                                sizeof(typename decltype(source.values)::value_type) +
                                4 * sizeof(void*);
                            if (bytes > material_budget_ - material_payload_bytes())
                                throw std::runtime_error(
                                    "Tile image values exceed the scene payload budget");
                        }
                        auto numeric = tile_draw_frame_values(*source.definitions, selected_tile,
                                                              cell.flags, image.width, image.height,
                                                              selected, source.color);
                        TileDrawSource::Values prepared{std::move(numeric.sprite)};
                        prepared.frame = selected;
                        prepared.animation_sample = source.animation_seconds;
                        if (image.mips) {
                            const auto region = sprite_grid_region(
                                image.width, image.height,
                                source.definitions->image(definition.image).grid, selected);
                            const SpriteFrameMipKey frame{region.x,
                                                          region.y,
                                                          region.width,
                                                          region.height,
                                                          region.width,
                                                          region.height,
                                                          0,
                                                          0,
                                                          false};
                            prepared.mip = &image.mips->frames.at(frame).image;
                            prepared.sprite.region = sprite_region_mapping(
                                {0, 0, region.width, region.height}, region.width, region.height,
                                bool(cell.flags & 1), bool(cell.flags & 2));
                        }
                        if (missing)
                            values = source.values.emplace(key, std::move(prepared)).first;
                        else
                            values->second = std::move(prepared);
                    }
                    tile_worlds_.push_back(world); // Capacity reserved before storing any pointers.
                    image.mesh.renderer.layers = map.renderer.layers;
                    Object object{&image.mesh, image.bundle.get(), nullptr, {}, 0, {}, {}};
                    object.scope = DrawScope::TileMap;
                    object.world = &tile_worlds_.back();
                    object.sprite = &values->second.sprite;
                    object.mip = values->second.mip;
                    RenderSortKey sort{part.alpha,
                                       transform_parity(world),
                                       part.material,
                                       image.bundle->mesh_identity().asset,
                                       bounds_camera_depth(bounds, camera),
                                       entity,
                                       0};
                    sort.sprite = true;
                    sort.order = std::int64_t(map.renderer.order) + layer.order;
                    sort.birth = ordinal++;
                    validate_render_key(sort);
                    queue.push_back({sort, objects.size(), false});
                    objects.push_back(std::move(object));
                });
            } catch (const std::exception& error) {
                queue.resize(queue_start);
                objects.resize(object_start);
                tile_worlds_.resize(world_start);
                source.values.clear();
                report(entity, error.what(), DrawScope::TileMap);
            }
        }
    std::sort(queue.begin(), queue.end(),
              [](const auto& a, const auto& b) { return render_key_less(a.key, b.key); });
    const auto temporal_item = [&](const Item& item) {
        return objects[item.object].scope == DrawScope::Mesh && !item.transmission &&
               item.key.alpha != MaterialAlpha::Blend;
    };
    const bool geometry_ready =
        diagnostics_.size() == diagnostics_before && omitted_ == omitted_before;
    if (temporal)
        temporal->applied = temporal->successful = false;
    const auto unavailable = [&](const char* category, const char* label, const std::string& why,
                                 const char* property = "") {
        Diagnostic value{
            Severity::Warning, category, std::string(label) + " unavailable: " + why, {}};
        value.context.asset = scene_;
        value.context.source = "presentation";
        value.context.property = property;
        if (diagnostics_.size() < 256)
            diagnostics_.push_back(std::move(value));
        else
            ++omitted_;
    };
    if (post_mesh)
        post_mesh->applied = false;
    if (temporal || post_mesh) {
        bool current_layer_seen = false, ordered = true;
        for (const auto& item : queue) {
            ordered &= !(temporal_item(item) && current_layer_seen);
            current_layer_seen |= !temporal_item(item);
        }
        if (!ordered) {
            const char* why =
                "Opaque layers precede later Mesh draws; the view keeps ordinary draw ordering";
            if (temporal)
                unavailable("render.temporal.unavailable", "Temporal surface effects", why);
            if (post_mesh)
                unavailable("render.focus.unavailable", "Depth of field", why);
            temporal = nullptr;
            post_mesh = nullptr;
        }
    }
    if (post_mesh) {
        try {
            if (!color || !depth || !post_mesh->prepare || !post_mesh->resolve)
                throw std::invalid_argument("Post-Mesh view routing is incomplete");
            for (const auto& item : queue)
                if (temporal_item(item) &&
                    !objects[item.object]
                         .bundle->parts(objects[item.object].lod)[item.key.part]
                         .depth_write)
                    throw std::runtime_error(
                        "Mesh material must test and write depth for focus filtering");
            post_mesh->prepare();
        } catch (const std::exception& error) {
            unavailable("render.focus.unavailable", "Depth of field", error.what());
            post_mesh = nullptr;
        }
    }
    if (temporal) {
        temporal->applied = temporal->successful = false;
        try {
            if ((temporal->reflection_output && !temporal->indirect_output) ||
                (temporal->normal_only_output &&
                 (!temporal->indirect_output || temporal->reflection_output)) ||
                (!temporal->motion_output && !temporal->indirect_output) || !temporal->begin ||
                !temporal->resolve || !temporal->previous_parts || !temporal->current_parts ||
                temporal->current.viewport.width != camera.viewport.width ||
                temporal->current.viewport.height != camera.viewport.height)
                throw std::invalid_argument(
                    "Temporal view routing is incomplete or has wrong extent");
            const auto visible_parts =
                std::size_t(std::count_if(queue.begin(), queue.end(), temporal_item));
            if (visible_parts > temporal_mesh_part_limit)
                throw std::runtime_error(
                    "Temporal view exceeds the 65536 visible Mesh-part profile");
            temporal->current_parts->clear();
            if (temporal->motion_output)
                temporal->current_parts->reserve(visible_parts);
            for (const auto& item : queue)
                if (temporal_item(item)) {
                    const auto& object = objects[item.object];
                    if (!object.bundle->parts(object.lod)[item.key.part].depth_write)
                        throw std::runtime_error(
                            "Mesh material must test and write depth for temporal surface effects");
                    if (!object.bundle->surface_prepared(
                            object.lod, item.key.part, temporal->motion_output,
                            temporal->indirect_output, temporal->reflection_output,
                            temporal->normal_only_output, object.area_lights))
                        require_specialization_capacity();
                    object.bundle->prepare_surface(
                        host_->context_, object.lod, item.key.part, temporal->motion_output,
                        temporal->indirect_output, object.parameters, temporal->reflection_output,
                        temporal->normal_only_output, object.area_lights);
                    if (temporal->motion_output)
                        temporal->current_parts->push_back(
                            {object.mesh->entity, object.lod, item.key.part});
                }
            std::sort(temporal->current_parts->begin(), temporal->current_parts->end());
        } catch (const std::exception& error) {
            unavailable("render.temporal.unavailable", "Temporal surface effects", error.what());
            temporal = nullptr; // Entire view retains ordinary unjittered rendering.
        }
    }
    bool weighted = scene.settings.weighted_transparency &&
                    std::any_of(queue.begin(), queue.end(), [](const auto& item) {
                        return item.key.alpha == MaterialAlpha::Blend;
                    });
    if (weighted) {
        try {
            // Admit the complete view before any accumulation target is bound.
            // Incompatible layers retain the existing sorted transport together.
            for (const auto& item : queue) {
                const auto& object = objects[item.object];
                if (object.scope == DrawScope::Sprite || object.scope == DrawScope::TileMap)
                    throw std::runtime_error("visible 2D layers require sorted alpha");
                if (item.transmission)
                    throw std::runtime_error("transmission requires sorted background transport");
                if (item.key.alpha != MaterialAlpha::Blend)
                    continue;
                if (object.scope != DrawScope::Mesh || item.key.order != 0)
                    throw std::runtime_error("this transparent layer requires sorted alpha");
                if (!object.bundle->surface_prepared(object.lod, item.key.part, false, false, false,
                                                     false, object.area_lights, true))
                    require_specialization_capacity();
                object.bundle->prepare_surface(host_->context_, object.lod, item.key.part, false,
                                               false, object.parameters, false, false,
                                               object.area_lights, true);
            }
            weighted_->prepare(color, depth, camera.viewport);
        } catch (const std::exception& error) {
            unavailable("render.transparency.unavailable", "Weighted transparency",
                        std::string(error.what()) + "; sorted alpha retained",
                        "rendering.weighted_transparency");
            weighted = false;
        }
    } else {
        weighted_->clear();
    }
    bool weighted_started = false;
    const auto draw_diagnostics_start = diagnostics_.size(), draw_omitted_start = omitted_;
    if (temporal) {
        temporal->begin();
        temporal->applied = true;
    }
    bool temporal_resolved = !temporal;
    bool post_resolved = !post_mesh;
    const auto resolve_temporal = [&] {
        if (!temporal_resolved) {
            temporal->successful = geometry_ready &&
                                   diagnostics_.size() == draw_diagnostics_start &&
                                   omitted_ == draw_omitted_start;
            temporal->resolve(temporal->successful);
            temporal_resolved = true;
        }
        if (!post_resolved) {
            try {
                if (geometry_ready && diagnostics_.size() == draw_diagnostics_start &&
                    omitted_ == draw_omitted_start) {
                    post_mesh->resolve();
                    post_mesh->applied = true;
                }
            } catch (const std::exception& error) {
                unavailable("render.focus.unavailable", "Depth of field", error.what());
            }
            post_resolved = true;
            auto* target = color->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
            host_->context_->SetRenderTargets(1, &target, depth,
                                              Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            const auto& area = camera.viewport;
            const auto& desc = color->GetDesc();
            Diligent::Viewport viewport{
                float(area.x), float(area.y), float(area.width), float(area.height), 0, 1};
            host_->context_->SetViewports(1, &viewport, desc.Width, desc.Height);
        }
    };
    const auto has_previous_part = [&](const Object& object, unsigned part) {
        return object.previous_pose && temporal &&
               std::binary_search(temporal->previous_parts->begin(),
                                  temporal->previous_parts->end(),
                                  TemporalMeshPart{object.mesh->entity, object.lod, part});
    };
    const auto motion_for = [&](const Object& object, unsigned part) {
        MeshDraw::Motion motion;
        if (temporal) {
            motion.previous_view = &temporal->previous;
            motion.jitter = temporal->jitter;
            motion.previous_jitter = temporal->previous_jitter;
            motion.valid = temporal->valid;
            if (has_previous_part(object, part)) {
                motion.previous_world = &object.previous_pose->world;
                motion.previous_morph_weights = object.previous_pose->morph_weights;
                const auto& pose = object.previous_pose->lods.at(object.lod).at(part);
                motion.previous_skin = pose.skin ? &*pose.skin : nullptr;
            }
        }
        return motion;
    };
    EnvironmentLighting lighting;
    if (environment_.ready)
        lighting = {&environment_.ready.get(), scene.settings.environment.intensity,
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
    const auto local_irradiance = [&](const Object& object,
                                      unsigned part) -> std::span<const EnvironmentProbe> {
        if (view_irradiance.empty() || !object.bundle->uses_lighting(object.lod, part))
            return {};
        if (object.bundle->supports_local_irradiance(object.lod, part))
            return view_irradiance;
        Diagnostic value{Severity::Warning,
                         "render.irradiance_probe.profile",
                         "Mesh part cannot consume local diffuse probes. Scene diffuse lighting "
                         "retained; recook its Surface Shader.",
                         {}};
        value.context.asset = scene.scene;
        value.context.entity = object.mesh->entity;
        value.context.property = "forge.reflection_probe.diffuse_intensity";
        value.context.source = "presentation";
        report(std::move(value));
        return {};
    };
    const auto projected_surface = [&](const Object& object, unsigned part,
                                       std::span<const MeshDraw::LightImage>& images) {
        if (std::any_of(object.images.begin(), object.images.end(),
                        [](const auto& image) { return image.frame != nullptr; }))
            images = object.images;
        if (!images.empty() && !object.bundle->supports_light_images(object.lod, part)) {
            Diagnostic value{
                Severity::Warning,
                "render.light.image.profile",
                "Mesh part cannot consume light images. Ordinary lighting retained; use a lit "
                "material with available projected map slots and a current Surface Shader cook.",
                {}};
            value.context.asset = scene.scene;
            value.context.entity = object.mesh->entity;
            value.context.property = "forge.light.texture";
            value.context.source = "presentation";
            report(std::move(value));
            images = {};
        }
        std::vector<MeshDraw::Decal> result;
        result.reserve(object.decals.size());
        for (const auto& id : object.decals) {
            const auto& entry = decal_entries_.at(id);
            const auto& resources = *entry.ready;
            (void)resources.material.get();
            MeshDraw::Decal selected{&entry.projector, &resources.values};
            selected.channels = resources.channels;
            for (unsigned c = 0; c < decal_channel_count; ++c) {
                const auto& texture = resources.textures[c];
                selected.textures[c] = {texture.gpu ? texture.gpu.get()->GetDefaultView(
                                                          Diligent::TEXTURE_VIEW_SHADER_RESOURCE)
                                                    : nullptr,
                                        texture.sampler};
            }
            result.push_back(selected);
        }
        if (!object.bundle->accepts_projected(object.lod, part, result, images)) {
            Diagnostic value{
                Severity::Warning,
                "render.decal.capacity",
                "Mesh part cannot admit the complete decal/light image set within its " +
                    std::to_string(object.bundle->decal_capacity(object.lod, part)) +
                    " map slots or its Shader projector capacity; ordinary material retained. "
                    "Reduce unique maps/samplers. Lists above the small packet capacity and "
                    "normal/roughness projection require a current receiver Surface Shader cook.",
                {}};
            value.context.asset = scene.scene;
            value.context.entity = object.mesh->entity;
            value.context.property = "forge.decal";
            value.context.source = "presentation";
            report(std::move(value));
            result.clear();
            images = {};
        }
        return result;
    };
    for (std::size_t cursor = 0; cursor < queue.size();) {
        const auto& item = queue[cursor++];
        const bool accumulated = temporal && temporal_item(item);
        if (!temporal_item(item))
            resolve_temporal();
        const auto& draw_view = accumulated ? temporal->current : camera;
        if (weighted && !weighted_started && item.key.alpha == MaterialAlpha::Blend) {
            weighted_->begin(host_->context_, depth);
            weighted_started = true;
        }
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
            auto batch_bounds = object.bounds;
            std::size_t count = 1;
            batch[0] = {*object.world, object.mesh->legacy_tint, object.sprite,
                        has_previous_part(object, item.key.part) ? &object.previous_pose->world
                                                                 : nullptr};
            const bool eligible = (item.key.alpha != MaterialAlpha::Blend || object.sprite) &&
                                  !item.transmission &&
                                  object.bundle->supports_instances(object.lod, item.key.part);
            while (eligible && cursor < queue.size() && count < batch.size()) {
                const auto& next_item = queue[cursor];
                const auto& next = objects[next_item.object];
                if (((temporal || post_mesh) && temporal_item(next_item) != temporal_item(item)) ||
                    next.bundle != object.bundle || next.lod != object.lod ||
                    bool(next.mip) != bool(object.mip) ||
                    (object.mip && next.mip->source() != object.mip->source()) ||
                    next_item.key.part != item.key.part ||
                    next_item.key.parity != item.key.parity ||
                    next_item.key.alpha != item.key.alpha || next_item.transmission ||
                    next.mesh->renderer.layers != object.mesh->renderer.layers ||
                    next.decals != object.decals ||
                    next.mesh->renderer.receive_shadows != object.mesh->renderer.receive_shadows ||
                    !detail::same_material_parameter_values(
                        slot_parameters(object.parameters,
                                        object.bundle->slot(object.lod, item.key.part)),
                        slot_parameters(next.parameters,
                                        next.bundle->slot(next.lod, next_item.key.part))))
                    break;
                batch[count++] = {*next.world, next.mesh->legacy_tint, next.sprite,
                                  has_previous_part(next, next_item.key.part)
                                      ? &next.previous_pose->world
                                      : nullptr};
                for (unsigned axis = 0; axis < 3; ++axis) {
                    batch_bounds.minimum[axis] =
                        std::min(batch_bounds.minimum[axis], next.bounds.minimum[axis]);
                    batch_bounds.maximum[axis] =
                        std::max(batch_bounds.maximum[axis], next.bounds.maximum[axis]);
                }
                ++cursor;
            }
            const auto motion = motion_for(object, item.key.part);
            std::span<const MeshDraw::LightImage> images;
            const auto decals = projected_surface(object, item.key.part, images);
            object.bundle->draw_part(
                host_->context_, *object.world, draw_view, object.lights, object.lod, item.key.part,
                environment_.ready ? &lighting : nullptr,
                object.mesh->legacy_tint ? &*object.mesh->legacy_tint : nullptr,
                &shadows_->lighting(), object.shadow_slots,
                transmission.background ? &transmission : nullptr, object.pose,
                count > 1 ? std::span<const MeshDraw::Instance>(batch.data(), count)
                          : std::span<const MeshDraw::Instance>{},
                slot_parameters(object.parameters, object.bundle->slot(object.lod, item.key.part)),
                object.sprite, &scene.settings.distance_fog, object.mip,
                accumulated && temporal->motion_output ? &motion : nullptr,
                accumulated && temporal->indirect_output,
                accumulated && temporal->reflection_output,
                accumulated && temporal->normal_only_output, decals,
                weighted && item.key.alpha == MaterialAlpha::Blend, &batch_bounds, images,
                local_irradiance(object, item.key.part));
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
                    const auto motion = motion_for(single, failed_item.key.part);
                    std::span<const MeshDraw::LightImage> images;
                    const auto decals = projected_surface(single, failed_item.key.part, images);
                    single.bundle->draw_part(
                        host_->context_, *single.world, draw_view, single.lights, single.lod,
                        failed_item.key.part, environment_.ready ? &lighting : nullptr,
                        single.mesh->legacy_tint ? &*single.mesh->legacy_tint : nullptr,
                        &shadows_->lighting(), single.shadow_slots, nullptr, single.pose, {},
                        slot_parameters(single.parameters,
                                        single.bundle->slot(single.lod, failed_item.key.part)),
                        single.sprite, &scene.settings.distance_fog, single.mip,
                        accumulated && temporal->motion_output ? &motion : nullptr,
                        accumulated && temporal->indirect_output,
                        accumulated && temporal->reflection_output,
                        accumulated && temporal->normal_only_output, decals,
                        weighted && failed_item.key.alpha == MaterialAlpha::Blend, &single.bounds,
                        images, local_irradiance(single, failed_item.key.part));
                    ++draw_stats_.calls;
                    ++draw_stats_.instances;
                } catch (const std::exception& error) {
                    report(single.mesh->entity, error.what(), single.scope);
                }
            }
        }
    }
    resolve_temporal();
    if (weighted_started) {
        try {
            weighted_->composite(host_->context_, color, depth);
        } catch (const std::exception& error) {
            unavailable("render.transparency.resolve", "Weighted transparency", error.what(),
                        "rendering.weighted_transparency");
        }
    }
    if (temporal)
        temporal->successful &= geometry_ready && diagnostics_.size() == draw_diagnostics_start &&
                                omitted_ == draw_omitted_start;
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
            auto world_bounds = mesh_instance_bounds(*found->second.pose, camera.position);
            casters.push_back({&mesh, found->second.ready.get(), found->second.pose.get(),
                               found->second.parameters});
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
