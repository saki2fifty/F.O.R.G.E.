#include "terrain_foliage_authoring.hpp"
#include "asset_bytes.hpp"
#include "mesh_scatter_authoring.hpp"
#include "root_mesh_artifact.hpp"
#include "terrain_height_query.hpp"
#include <cmath>
#include <forge/project_paths.hpp>
namespace forge {
Json prepare_terrain_foliage(const ProjectLease& lease, const Scene& scene, const Json& arguments,
                             const std::string& terrain_entity, double height_offset) {
    lease.check();
    const auto revision = scene.revision();
    if (!std::isfinite(height_offset) || std::abs(height_offset) > 1000)
        throw AuthoringError("invalid_arguments",
                             "Terrain height offset must be within -1000 to 1000 meters");
    if (arguments.contains("world_heights"))
        throw AuthoringError("invalid_arguments",
                             "Terrain preparation computes world heights; omit world_heights");
    const auto settings = detail::mesh_scatter_settings(arguments);
    const auto layout = detail::mesh_scatter_layout(settings);
    const auto view = scene.effective_document();
    const Json* surface = nullptr;
    for (const auto& row : view.at("entities"))
        if (row.at("id") == terrain_entity)
            surface = &row;
    if (!surface || surface->value("prefab", false) ||
        !surface->at("components").contains("forge.mesh_renderer") ||
        !surface->value("spatial_resolved", false) || !surface->contains("world_affine"))
        throw AuthoringError(
            "invalid_surface",
            "Choose a resolved Scene Mesh Renderer with a published heightfield terrain");
    const auto& mesh_value = surface->at("components").at("forge.mesh_renderer").at("mesh");
    if (!mesh_value.is_string())
        throw AuthoringError("invalid_surface",
                             "Choose a Mesh Renderer with a published heightfield terrain Mesh");
    const auto mesh = mesh_value.get<AssetId>();
    const auto catalog = AssetCatalog::open_project(lease.root());
    const auto found = catalog.records().find(mesh);
    if (found == catalog.records().end() || found->second.type != MeshAsset::type ||
        found->second.subasset || !found->second.metadata.contains("forge.terrain"))
        throw AuthoringError("invalid_surface",
                             "The selected Mesh is not a published heightfield terrain");
    const auto& record = found->second;
    asset_detail::validate_root_mesh_publication(record);
    const auto& selected = record.metadata.at("forge.import");
    // The existing immutable artifact reader verifies the same publication used
    // by rendering/export. Editable source must match that selected revision.
    const auto key = selected.at("key").get<std::string>();
    auto cache =
        DerivedDataCache::open_existing(lease.root(), {32 * 1024 * 1024, 64 * 1024 * 1024, 3});
    (void)cache.load_selected(key, [&](const CachedArtifact& artifact) {
        (void)asset_detail::admit_root_mesh(artifact, record);
    });
    const auto bytes = asset_detail::read_bytes(ProjectPaths(lease.root()).resolve(record.source),
                                                terrain_source_byte_limit);
    if (asset_detail::content_digest(bytes) != selected.at("source_digest"))
        throw AuthoringError("surface_source_changed",
                             "Save and publish the heightfield before projecting foliage; source "
                             "differs from its rendered selection");
    const auto source = TerrainSource::parse(bytes);
    if (source.asset() != mesh)
        throw AuthoringError("invalid_surface",
                             "Terrain source and selected Mesh identities differ");
    AffineTransform transform;
    transform.m = surface->at("world_affine").get<std::array<double, 12>>();
    if (transform.m[1] != 0 || transform.m[9] != 0 || transform.m[5] <= 0)
        throw AuthoringError("unsupported_surface_transform",
                             "Terrain projection requires an upright surface with positive "
                             "vertical scale; yaw and horizontal scaling are supported");
    const auto world_to_surface = inverse(transform);
    std::vector<std::array<double, 2>> positions;
    positions.reserve(layout.size());
    for (const auto& placement : layout) {
        const auto point =
            world_to_surface.point({settings.center[0] + placement.offset[0], transform.m[7],
                                    settings.center[2] + placement.offset[1]});
        positions.push_back({point[0], point[2]});
    }
    const auto heights = detail::terrain_heights(source.raster(), positions);
    auto resolved = arguments;
    resolved["world_heights"] = Json::array();
    for (std::size_t i = 0; i < heights.size(); ++i) {
        if (!heights[i])
            throw AuthoringError("surface_coverage",
                                 "The heightfield must cover every placement origin; move the "
                                 "center or reduce the radius. No objects were created");
        resolved["world_heights"].push_back(
            transform.point({positions[i][0], *heights[i], positions[i][1]})[1] + height_offset);
    }
    (void)preview_authoring(
        scene, Json::array({{{"operation", "mesh.scatter"}, {"arguments", resolved}}}));
    lease.check();
    if (scene.revision() != revision)
        throw AuthoringError("stale_revision", "Scene changed while preparing terrain placement");
    return {{"arguments", std::move(resolved)},
            {"scene_revision", revision},
            {"terrain_mesh", mesh},
            {"terrain_revision", key},
            {"source_digest", selected.at("source_digest")}};
}
} // namespace forge
