#include "mesh_render_host.hpp"
#include "render_projection.hpp"

namespace forge {
std::optional<MeshSceneRenderer::UiSurfaceSelection>
MeshSceneRenderer::ui_surface_selection(const RenderScene& scene, EntityId entity,
                                        std::uint32_t layers) const {
    if (scene.scene != scene_ || scene.ui_surfaces.size() > ui_surface_detail::max_surfaces)
        return {};
    const auto surface = std::find_if(scene.ui_surfaces.begin(), scene.ui_surfaces.end(),
                                      [&](const auto& value) { return value.entity == entity; });
    if (surface == scene.ui_surfaces.end() || !surface->input.enabled ||
        !surface->input.render_texture.id || surface->mesh_index >= scene.meshes.size())
        return {};
    const auto& mesh = scene.meshes[surface->mesh_index];
    if (mesh.entity != entity || mesh.particle_source || !mesh.renderer.enabled ||
        !mesh.renderer.visible || !(mesh.renderer.layers & layers))
        return {};
    const auto entry = entries_.find({entity, DrawScope::Mesh});
    if (entry == entries_.end() || !entry->second.ready || !entry->second.pose)
        return {};
    return UiSurfaceSelection{&*surface, &entry->second,
                              ui_surface_image_mapping(entry->second.ready->prepared(),
                                                       *entry->second.pose,
                                                       surface->input.render_texture)};
}
std::vector<Diagnostic> MeshSceneRenderer::ui_surface_diagnostics(const RenderScene& scene) const {
    host_->check_thread();
    std::vector<Diagnostic> result;
    if (scene.ui_surfaces.size() > ui_surface_detail::max_surfaces) {
        Diagnostic value{Severity::Error,
                         "render.ui_surface.profile",
                         "UI Surface Input exceeds the Scene surface limit",
                         {}};
        value.context.asset = scene.scene;
        value.context.property = "forge.ui_surface_input";
        value.context.source = "presentation";
        result.push_back(std::move(value));
        return result;
    }
    for (const auto& surface : scene.ui_surfaces)
        try {
            (void)ui_surface_selection(scene, surface.entity, UINT32_MAX);
        } catch (const std::exception& error) {
            Diagnostic value{Severity::Error, "render.ui_surface.profile", error.what(), {}};
            value.context.asset = scene.scene;
            value.context.entity = surface.entity;
            value.context.property = "forge.ui_surface_input";
            value.context.source = "presentation";
            result.push_back(std::move(value));
        }
    return result;
}
std::optional<UiSurfaceHit>
MeshSceneRenderer::pick_ui_surface(const RenderScene& scene, const PreparedCamera& camera, double x,
                                   double y, std::optional<UiSurfaceHit> retained) const {
    host_->check_thread();
    if (scene.ui_surfaces.size() > ui_surface_detail::max_surfaces ||
        (retained && retained->view != camera.entity))
        return {};
    std::optional<UiSurfaceHit> result;
    double nearest = 2;
    for (const auto& surface : scene.ui_surfaces) {
        if (retained && (retained->surface != surface.entity ||
                         retained->output != surface.input.render_texture))
            continue;
        try {
            const auto selection =
                ui_surface_selection(scene, surface.entity, camera.settings.layers);
            if (!selection ||
                !render_texture_inputs_.produced.contains(surface.input.render_texture.id) ||
                !selection->entry->ready->render_textures_ready(render_texture_inputs_))
                continue;
            const auto hit = pick_render_plane(selection->entry->pose->world, camera.view, x, y);
            if (!hit ||
                (!retained && (std::abs(hit->local[0]) > .5 || std::abs(hit->local[1]) > .5)))
                continue;
            if (hit->depth < nearest ||
                (hit->depth == nearest && result && surface.entity < result->surface)) {
                nearest = hit->depth;
                result = UiSurfaceHit{surface.entity, surface.input.render_texture,
                                      selection->mapping.image(hit->local), camera.entity};
            }
        } catch (const std::exception&) {
            // The same retained selection is diagnosed by the Game host. Invalid
            // surfaces do not disable unrelated valid views.
        }
    }
    if (result && !retained) {
        const auto visible = pick_depth(scene, camera.view, x, y, camera.settings.layers, 0,
                                        MeshPickMode::VisibleGeometry);
        if (!visible || visible->first != result->surface)
            return {};
    }
    return result;
}
std::optional<Double2> MeshSceneRenderer::project_ui_surface(const RenderScene& scene,
                                                             const PreparedCamera& camera,
                                                             const UiSurfaceHit& hit) const {
    host_->check_thread();
    if (hit.view != camera.entity || !std::isfinite(hit.uv[0]) || !std::isfinite(hit.uv[1]))
        return {};
    try {
        const auto selection = ui_surface_selection(scene, hit.surface, camera.settings.layers);
        if (!selection || selection->surface->input.render_texture != hit.output ||
            !render_texture_inputs_.produced.contains(hit.output.id) ||
            !selection->entry->ready->render_textures_ready(render_texture_inputs_))
            return {};
        const auto local = selection->mapping.local(hit.uv);
        const auto& world = selection->entry->pose->world;
        Double3 relative;
        for (unsigned axis = 0; axis < 3; ++axis)
            relative[axis] = world.m[axis * 4] * local[0] + world.m[axis * 4 + 1] * local[1] +
                             (world.m[axis * 4 + 3] - camera.view.position[axis]);
        auto view = camera.view;
        view.position = {};
        return project_render_point(view, relative);
    } catch (const std::exception&) {
        return {};
    }
}
} // namespace forge
