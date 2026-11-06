#include "paint_texture_mesh.hpp"
#include "model_render_resource.hpp"

namespace forge {
std::vector<PaintPoint> project_paint_mesh_stroke(const std::filesystem::path& project,
                                                  const AssetCatalog& catalog,
                                                  const PaintMeshSelection& selection,
                                                  std::span<const PaintMeshPoint> points,
                                                  const PaintTextureValue& canvas,
                                                  std::stop_token stop) {
    if (catalog.project_root() != std::filesystem::weakly_canonical(project))
        throw std::runtime_error("Surface stroke selection belongs to another project");
    if (points.empty() || points.size() > paint_texture_stroke_point_limit)
        throw std::runtime_error("Surface stroke needs 1 to 256 triangle samples");
    if (selection.mesh_revision != asset_detail::selected_mesh_revision(catalog, selection.mesh) ||
        selection.material_revision !=
            asset_detail::selected_material_revision(catalog, selection.material))
        throw std::runtime_error(
            "Surface stroke Mesh or Material publication has changed; refresh its revision");
    MeshLimits limits;
    limits.bytes = 32 * 1024 * 1024;
    const auto mesh =
        asset_detail::load_mesh_selection(project, catalog, selection.mesh, limits, stop);
    const auto material =
        asset_detail::load_pbr_material(project, catalog, selection.material, stop);
    const auto& parts = mesh.value->mesh.lods.at(selection.lod).parts;
    MeshTextureSurfaceTarget target{{canvas.asset()}, canvas.semantic(), selection.slot};
    std::vector<PaintPoint> result;
    result.reserve(points.size());
    MeshPickBudget budget;
    std::optional<MeshTextureSurfacePoint> previous;
    unsigned previous_part = 0;
    for (const auto& sample : points) {
        if (stop.stop_requested())
            throw std::runtime_error("Surface stroke cancelled");
        const auto next = mesh_texture_surface_point(parts.at(sample.part), material, target,
                                                     sample.triangle, budget);
        const bool split = sample.break_before || !previous || previous_part != sample.part ||
                           !mesh_texture_surface_connected(*previous, next);
        result.push_back({next.uv[0] * canvas.width(), next.uv[1] * canvas.height(), split});
        previous = next;
        previous_part = sample.part;
    }
    return result;
}
} // namespace forge
