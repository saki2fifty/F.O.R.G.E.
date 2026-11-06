#include "mesh_render_host.hpp"
#include "mesh_uv_surface.hpp"
#include "render_projection.hpp"

namespace forge {
namespace {
struct MeshSurfaceView {
    const asset_detail::PreparedModelDraw& draw;
    const MeshInstancePose& pose;
    const PreparedCamera& camera;
    AssetRef<RenderTextureAsset> output;
    unsigned lod;
    MeshSurfaceView(const asset_detail::PreparedModelDraw& d, const MeshInstancePose& p,
                    const PreparedCamera& c, AssetRef<RenderTextureAsset> o)
        : draw(d), pose(p), camera(c), output(o),
          lod(select_mesh_lod(
              d.mesh->mesh,
              bounds_screen_coverage(mesh_instance_bounds(p, c.view.position), c.view))) {}
    const auto& parts() const { return draw.mesh->mesh.lods.at(lod).parts; }
    UiSurfaceMeshHit anchor(unsigned part, std::size_t primitive) const {
        const auto& id = draw.mesh.identity();
        return {id.asset, id.revision, id.variant, lod, part, primitive};
    }
    bool current(const UiSurfaceMeshHit& hit) const {
        const auto& id = draw.mesh.identity();
        if (hit.mesh != id.asset || hit.revision != id.revision || hit.variant != id.variant ||
            hit.lod != lod || hit.part >= parts().size())
            return false;
        const auto& part = parts()[hit.part];
        const auto corners = part.indices.empty() ? part.vertices : part.indices.size();
        return corners % 3 == 0 && hit.primitive < corners / 3;
    }
    std::optional<Double3> point(const UiSurfaceHit& hit, MeshPickBudget& budget) const {
        if (!hit.mesh || !current(*hit.mesh))
            return {};
        std::optional<Double3> result;
        for (unsigned p = 0; p < parts().size(); ++p) {
            const auto& part = parts()[p];
            const auto image = ui_surface_mesh_material(draw, lod, p, output).mesh_uv(hit.uv);
            const MeshPickVertices vertices(part, pose.lods.at(lod).at(p), pose);
            const auto corners = part.indices.empty() ? part.vertices : part.indices.size();
            for (std::size_t primitive = 0; primitive < corners / 3; ++primitive) {
                const MeshUvTriangle triangle(part, primitive, budget);
                const auto coordinates = triangle.coordinates(image);
                if (!coordinates || (*coordinates)[0] < -1e-10 || (*coordinates)[1] < -1e-10 ||
                    (*coordinates)[2] < -1e-10)
                    continue;
                const auto next = mesh_triangle_point(
                    triangle.relative(vertices, camera.view.position, budget), *coordinates);
                if (result)
                    for (unsigned axis = 0; axis < 3; ++axis) {
                        const double tolerance =
                            1e-8 + 64 * std::numeric_limits<double>::epsilon() *
                                       std::max(std::abs((*result)[axis]), std::abs(next[axis]));
                        if (std::abs((*result)[axis] - next[axis]) > tolerance)
                            return {}; // Overlapping UV islands identify different world points.
                    }
                result = next;
            }
        }
        if (result)
            return result;
        // A captured pointer may leave the image. Its original triangle supplies
        // an unbounded continuation plane; UV holes inside the image have no target.
        if (hit.uv[0] >= 0 && hit.uv[0] <= 1 && hit.uv[1] >= 0 && hit.uv[1] <= 1)
            return {};
        const auto& part = parts().at(hit.mesh->part);
        const MeshUvTriangle triangle(part, hit.mesh->primitive, budget);
        const auto image =
            ui_surface_mesh_material(draw, lod, hit.mesh->part, output).mesh_uv(hit.uv);
        const auto coordinates = triangle.coordinates(image);
        if (!coordinates)
            return {};
        const MeshPickVertices vertices(part, pose.lods.at(lod).at(hit.mesh->part), pose);
        return mesh_triangle_point(triangle.relative(vertices, camera.view.position, budget),
                                   *coordinates);
    }
    struct Hit {
        double depth;
        Double2 uv;
        UiSurfaceMeshHit mesh;
    };
    std::optional<Hit> pick(double x, double y, MeshPickBudget& budget,
                            const std::optional<UiSurfaceMeshHit>& retained) const {
        if (retained && !current(*retained))
            return {};
        std::optional<MeshPickHit> best;
        unsigned selected = 0;
        for (unsigned p = 0; p < parts().size(); ++p) {
            const auto hit = pick_mesh_part_hit(parts()[p], pose.lods.at(lod).at(p), pose,
                                                camera.view, x, y, budget, 0);
            if (hit && (!best || hit->depth < best->depth)) {
                best = hit;
                selected = p;
            }
        }
        if (best && best->triangle) {
            const auto& hit = *best->triangle;
            const MeshUvTriangle triangle(parts()[selected], hit.primitive, budget);
            const auto uv = triangle.image(hit.barycentric);
            if (!triangle.coordinates(uv))
                return {};
            return Hit{best->depth,
                       ui_surface_mesh_material(draw, lod, selected, output).image_uv(uv),
                       anchor(selected, hit.primitive)};
        }
        if (!retained)
            return {};
        const auto& part = parts().at(retained->part);
        const MeshUvTriangle triangle(part, retained->primitive, budget);
        const MeshPickVertices vertices(part, pose.lods.at(lod).at(retained->part), pose);
        const auto plane =
            mesh_triangle_plane(triangle.relative(vertices, camera.view.position, budget));
        auto view = camera.view;
        view.position = {};
        const auto hit = pick_render_plane(plane, view, x, y);
        if (!hit)
            return {};
        const Double3 weights{1 - hit->local[0] - hit->local[1], hit->local[0], hit->local[1]};
        return Hit{hit->depth,
                   ui_surface_mesh_material(draw, lod, retained->part, output)
                       .image_uv(triangle.image(weights)),
                   *retained};
    }
};
} // namespace

std::optional<MeshSceneRenderer::UiSurfaceSelection>
MeshSceneRenderer::ui_surface_selection(const RenderScene& scene, EntityId entity,
                                        std::uint32_t layers, MeshPickBudget& budget) const {
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
    UiSurfaceImageMapping mapping{};
    if (surface->input.geometry == std::uint32_t(UiSurfaceGeometry::Mesh))
        validate_ui_surface_mesh(entry->second.ready->prepared(), surface->input.render_texture,
                                 budget);
    else if (surface->input.geometry == std::uint32_t(UiSurfaceGeometry::Quad))
        mapping = ui_surface_image_mapping(entry->second.ready->prepared(), *entry->second.pose,
                                           surface->input.render_texture);
    else
        throw std::runtime_error("UI Surface Input geometry must be 0 Quad or 1 Mesh");
    return UiSurfaceSelection{&*surface, &entry->second, mapping};
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
    MeshPickBudget budget;
    for (const auto& surface : scene.ui_surfaces)
        try {
            (void)ui_surface_selection(scene, surface.entity, UINT32_MAX, budget);
        } catch (const std::exception& error) {
            Diagnostic value{Severity::Error, "render.ui_surface.profile", error.what(), {}};
            value.context.asset = scene.scene;
            value.context.entity = surface.entity;
            value.context.property = "forge.ui_surface_input";
            value.context.source = "presentation";
            result.push_back(std::move(value));
            if (budget.remaining == 0)
                break;
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
    MeshPickBudget budget;
    std::optional<UiSurfaceHit> result;
    double nearest = 2;
    for (const auto& surface : scene.ui_surfaces) {
        if (retained && (retained->surface != surface.entity ||
                         retained->output != surface.input.render_texture))
            continue;
        try {
            const auto selection =
                ui_surface_selection(scene, surface.entity, camera.settings.layers, budget);
            if (!selection ||
                !render_texture_inputs_.produced.contains(surface.input.render_texture.id) ||
                !selection->entry->ready->render_textures_ready(render_texture_inputs_))
                continue;
            UiSurfaceHit candidate{surface.entity, surface.input.render_texture, {}, camera.entity};
            double depth;
            if (surface.input.geometry == std::uint32_t(UiSurfaceGeometry::Mesh)) {
                if (retained && !retained->mesh)
                    continue;
                const MeshSurfaceView mesh(selection->entry->ready->prepared(),
                                           *selection->entry->pose, camera,
                                           surface.input.render_texture);
                const auto hit = mesh.pick(x, y, budget, retained ? retained->mesh : std::nullopt);
                if (!hit)
                    continue;
                depth = hit->depth;
                candidate.uv = hit->uv;
                candidate.mesh = hit->mesh;
                if (!mesh.point(candidate, budget))
                    continue;
            } else {
                if (retained && retained->mesh)
                    continue;
                const auto hit =
                    pick_render_plane(selection->entry->pose->world, camera.view, x, y);
                if (!hit ||
                    (!retained && (std::abs(hit->local[0]) > .5 || std::abs(hit->local[1]) > .5)))
                    continue;
                depth = hit->depth;
                candidate.uv = selection->mapping.image(hit->local);
            }
            if (depth < nearest ||
                (depth == nearest && result && surface.entity < result->surface)) {
                nearest = depth;
                result = std::move(candidate);
            }
        } catch (const std::exception&) {
            if (budget.remaining == 0)
                return {}; // Never deliver a partial result after shared budget exhaustion.
            // The same retained selection is diagnosed by the Game host. Invalid
            // surfaces do not disable unrelated valid views.
        }
    }
    if (result && !retained) {
        try {
            const auto visible = pick_depth(scene, camera.view, x, y, camera.settings.layers, 0,
                                            MeshPickMode::VisibleGeometry, &budget);
            if (!visible || visible->first != result->surface)
                return {};
        } catch (const std::exception&) {
            return {};
        }
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
        MeshPickBudget budget;
        const auto selection =
            ui_surface_selection(scene, hit.surface, camera.settings.layers, budget);
        if (!selection || selection->surface->input.render_texture != hit.output ||
            !render_texture_inputs_.produced.contains(hit.output.id) ||
            !selection->entry->ready->render_textures_ready(render_texture_inputs_))
            return {};
        if (selection->surface->input.geometry == std::uint32_t(UiSurfaceGeometry::Mesh)) {
            const MeshSurfaceView mesh(selection->entry->ready->prepared(), *selection->entry->pose,
                                       camera, hit.output);
            const auto relative = mesh.point(hit, budget);
            auto view = camera.view;
            view.position = {};
            return relative ? project_render_point(view, *relative) : std::nullopt;
        }
        if (hit.mesh)
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
