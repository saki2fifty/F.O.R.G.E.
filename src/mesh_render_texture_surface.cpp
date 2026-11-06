#include "mesh_render_host.hpp"

namespace forge {
std::optional<MeshTextureSurfaceHit>
MeshSceneRenderer::pick_texture_surface(const RenderScene& scene, const CameraView& camera,
                                        EntityId selected, const MeshTextureSurfaceTarget& target,
                                        double x, double y, std::uint32_t layers) const {
    host_->check_thread();
    if (scene.scene != scene_ || !selected)
        return {};
    MeshPickBudget budget;
    const auto front =
        pick_depth(scene, camera, x, y, layers, 0, MeshPickMode::VisibleGeometry, &budget);
    if (!front || front->first != selected)
        return {};
    const auto mesh = std::find_if(scene.meshes.begin(), scene.meshes.end(),
                                   [&](const auto& value) { return value.entity == selected; });
    if (mesh == scene.meshes.end() || mesh->particle_source || !mesh->selectable ||
        !mesh->renderer.enabled || !mesh->renderer.visible || !(mesh->renderer.layers & layers))
        return {};
    const auto entry = entries_.find({selected, DrawScope::Mesh});
    if (entry == entries_.end() || !entry->second.ready || !entry->second.pose)
        return {};
    const auto& draw = entry->second.ready->prepared();
    if (draw.has_fallbacks())
        throw std::runtime_error(
            "Surface painting requires a complete published Mesh/Material/image selection");
    const auto& pose = *entry->second.pose;
    const auto lod = select_mesh_lod(
        draw.mesh->mesh,
        bounds_screen_coverage(mesh_instance_bounds(pose, camera.position), camera));
    const auto& parts = draw.mesh->mesh.lods.at(lod).parts;
    std::optional<MeshPickHit> nearest;
    unsigned selected_part = 0;
    for (unsigned p = 0; p < parts.size(); ++p)
        if (const auto hit = pick_mesh_part_hit(parts[p], pose.lods.at(lod).at(p), pose, camera, x,
                                                y, budget, 0);
            hit && (!nearest || hit->depth < nearest->depth)) {
            nearest = hit;
            selected_part = p;
        }
    if (!nearest || !nearest->triangle)
        return {};
    const auto material_id = draw.selection.parts.at(lod).at(selected_part).id;
    const auto material = draw.materials.find(material_id);
    if (material == draw.materials.end())
        throw std::runtime_error("Surface painting Material is unavailable");
    const auto& mesh_id = draw.mesh.identity();
    const auto& material_identity = material->second.identity();
    return MeshTextureSurfaceHit{selected,
                                 mesh_id.asset,
                                 material_identity.asset,
                                 mesh_id.revision,
                                 mesh_id.variant,
                                 material_identity.revision,
                                 material_identity.variant,
                                 unsigned(lod),
                                 selected_part,
                                 mesh_texture_surface_point(parts[selected_part],
                                                            material->second.get(), target,
                                                            *nearest->triangle, budget)};
}
} // namespace forge
