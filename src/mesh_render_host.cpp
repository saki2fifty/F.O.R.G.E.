#include "mesh_render_host.hpp"
#include "mesh_draw_shader.hpp"
#include "render_sort.hpp"
#include <set>
namespace forge {
MeshResourceHost::MeshResourceHost(DiligentPresentation& presentation,
                                   Diligent::IDeviceContext* context, std::filesystem::path project)
    : presentation_(presentation), context_(context), project_(std::move(project)),
      gpu_meshes_(presentation.device(), context, 512ull * 1024 * 1024),
      gpu_textures_(presentation.device(), context, 512ull * 1024 * 1024) {}
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
    gpu_meshes_.collect();
    gpu_textures_.collect();
}
void MeshResourceHost::submit() {
    check_thread();
    gpu_meshes_.submit();
    gpu_textures_.submit();
}
MeshSceneRenderer::MeshSceneRenderer(std::shared_ptr<MeshResourceHost> host,
                                     Diligent::TEXTURE_FORMAT color)
    : host_(std::move(host)), color_(color) {
    if (!host_)
        throw std::runtime_error("Mesh scene requires a presentation resource host");
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
bool MeshSceneRenderer::update(const RenderScene& scene) {
    host_->pump();
    bool changed = false;
    diagnostics_ = scene.diagnostics;
    omitted_ = scene.omitted_diagnostics;
    if (scene_ != scene.scene) {
        entries_.clear();
        scene_ = scene.scene;
        changed = true;
    }
    std::set<EntityId> used;
    for (const auto& mesh : scene.meshes) {
        used.insert(mesh.entity);
        auto& entry = entries_[mesh.entity];
        const auto& renderer = mesh.renderer;
        if (entry.epoch != host_->epoch_ || entry.mesh != renderer.mesh ||
            entry.overrides != renderer.materials) {
            entry.candidate.reset();
            entry.epoch = host_->epoch_;
            entry.mesh = renderer.mesh;
            entry.overrides = renderer.materials;
            entry.error.clear();
            changed = true;
            try {
                entry.candidate = std::make_unique<asset_detail::ModelDrawCandidate>(
                    host_->project_, host_->catalog_, host_->epoch_, renderer.mesh,
                    renderer.materials, host_->meshes_);
            } catch (const std::exception& e) {
                entry.error = e.what();
            }
        }
        if (entry.candidate) {
            entry.candidate->advance(host_->epoch_, host_->meshes_, host_->materials_,
                                     host_->textures_);
            if (const auto* candidate = entry.candidate->ready()) {
                try {
                    MeshBounds bounds;
                    bool first = true;
                    std::vector<float> thresholds;
                    for (const auto& lod : candidate->mesh->mesh.lods) {
                        thresholds.push_back(lod.screen_coverage);
                        for (const auto& part : lod.parts) {
                            for (unsigned axis = 0; axis < 3; ++axis) {
                                bounds.minimum[axis] = first ? part.bounds.minimum[axis]
                                                             : std::min(bounds.minimum[axis],
                                                                        part.bounds.minimum[axis]);
                                bounds.maximum[axis] = first ? part.bounds.maximum[axis]
                                                             : std::max(bounds.maximum[axis],
                                                                        part.bounds.maximum[axis]);
                            }
                            first = false;
                        }
                    }
                    auto native = std::make_unique<MeshDrawBundle>(
                        host_->presentation_, host_->context_, *candidate, host_->gpu_meshes_,
                        host_->gpu_textures_, color_, Diligent::TEX_FORMAT_D32_FLOAT);
                    entry.ready = std::move(native);
                    entry.bounds = bounds;
                    entry.thresholds = std::move(thresholds);
                    entry.error.clear();
                    changed = true;
                } catch (const std::exception& e) {
                    entry.error = e.what();
                }
                entry.candidate.reset();
            } else if (entry.candidate->state() != ResourceState::Loading) {
                entry.error = entry.candidate->diagnostic();
                entry.candidate.reset();
            }
        }
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
bool MeshSceneRenderer::pending() const {
    host_->check_thread();
    return std::any_of(entries_.begin(), entries_.end(),
                       [](const auto& pair) { return bool(pair.second.candidate); });
}
void MeshSceneRenderer::draw(const RenderScene& scene, const CameraView& camera,
                             std::uint32_t layers) {
    host_->check_thread();
    if (scene.scene != scene_)
        throw std::runtime_error("Mesh draw scene differs from prepared scene");
    struct Object {
        const RenderMesh* mesh;
        MeshDrawBundle* bundle;
        unsigned lod;
        std::vector<LightView> lights;
    };
    struct Item {
        RenderSortKey key;
        std::size_t object;
    };
    std::vector<Object> objects;
    std::vector<Item> queue;
    for (const auto& mesh : scene.meshes) {
        if (!(mesh.renderer.layers & layers))
            continue;
        const auto found = entries_.find(mesh.entity);
        if (found == entries_.end() || !found->second.ready)
            continue;
        auto& entry = found->second;
        const auto queue_start = queue.size();
        try {
            const auto bounds = transform_bounds(entry.bounds, mesh.world);
            if (!bounds_visible(bounds, camera))
                continue;
            const float coverage = bounds_screen_coverage(bounds, camera);
            unsigned lod = 0;
            for (unsigned i = 0; i < entry.thresholds.size(); ++i)
                if (coverage <= entry.thresholds[i])
                    lod = i;
            Object object{&mesh, entry.ready.get(), lod, {}};
            for (const auto& light : scene.lights)
                if (light.light.layers & mesh.renderer.layers & layers) {
                    if (object.lights.size() == mesh_draw_light_limit)
                        throw std::runtime_error(
                            "Visible light list exceeds the current draw profile");
                    object.lights.push_back(light.light);
                }
            const auto parts = entry.ready->parts(lod);
            for (unsigned i = 0; i < parts.size(); ++i) {
                const auto& part = parts[i];
                const auto part_bounds = transform_bounds(part.bounds, mesh.world);
                if (!bounds_visible(part_bounds, camera))
                    continue;
                RenderSortKey key{part.alpha,
                                  transform_parity(mesh.world),
                                  part.material,
                                  entry.ready->mesh_identity().asset,
                                  bounds_camera_depth(part_bounds, camera),
                                  mesh.entity,
                                  i};
                validate_render_key(key);
                queue.push_back({key, objects.size()});
            }
            objects.push_back(std::move(object));
        } catch (const std::exception& e) {
            queue.resize(queue_start);
            report(mesh.entity, e.what());
        }
    }
    std::sort(queue.begin(), queue.end(),
              [](const auto& a, const auto& b) { return render_key_less(a.key, b.key); });
    for (const auto& item : queue) {
        const auto& object = objects[item.object];
        try {
            object.bundle->draw_part(host_->context_, object.mesh->world, camera, object.lights,
                                     object.lod, item.key.part);
        } catch (const std::exception& e) {
            report(object.mesh->entity, e.what());
        }
    }
}
} // namespace forge
