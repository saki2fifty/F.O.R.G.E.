#include "terrain_foliage_authoring.hpp"
#include "asset_bytes.hpp"
#include "mesh_foliage_surface.hpp"
#include "mesh_scatter_authoring.hpp"
#include "model_render_resource.hpp"
#include "root_mesh_artifact.hpp"
#include "terrain_height_query.hpp"
#include <cmath>
#include <forge/project_paths.hpp>
#include <numbers>
namespace forge {
namespace {
Json prepare_foliage(const ProjectLease& lease, const Scene& scene, const Json& arguments,
                     const std::string& terrain_entity, double height_offset,
                     TerrainFoliageSlopeRange slopes, bool align_to_slope, bool mesh_surface,
                     bool stroke = false) {
    lease.check();
    const auto revision = scene.revision();
    if (!std::isfinite(height_offset) || std::abs(height_offset) > 1000)
        throw AuthoringError("invalid_arguments",
                             "Foliage height offset must be within -1000 to 1000 meters");
    if (!std::isfinite(slopes.minimum) || !std::isfinite(slopes.maximum) || slopes.minimum < 0 ||
        slopes.maximum > 90 || slopes.minimum > slopes.maximum)
        throw AuthoringError("invalid_arguments",
                             "Foliage slopes must satisfy 0 <= minimum <= maximum <= 90 degrees");
    // Assemble the bounded candidate pool before loading the published surface.
    // Every dab still uses the existing scatter layout and validation rules.
    std::vector<Json> dabs;
    if (stroke) {
        const auto& input = arguments.at("dabs");
        if (!input.is_array() || input.empty() || input.size() > 16)
            throw AuthoringError("invalid_arguments", "A foliage stroke needs 1 to 16 dabs");
        for (const auto& value : input) {
            auto dab = value;
            dab["entity"] = arguments.at("entity");
            dab["name"] = arguments.at("name");
            dabs.push_back(std::move(dab));
        }
    } else
        dabs.push_back(arguments);
    std::vector<Double2> world_positions;
    std::vector<std::size_t> counts;
    for (const auto& dab : dabs) {
        if (dab.contains("world_heights") || dab.contains("placement_mask") ||
            dab.contains("world_normals"))
            throw AuthoringError("invalid_arguments",
                                 "Foliage preparation computes heights and selection; omit "
                                 "world_heights, placement_mask and world_normals");
        const auto settings = detail::mesh_scatter_settings(dab);
        const auto layout = detail::mesh_scatter_layout(settings);
        if (world_positions.size() + layout.size() > 512)
            throw AuthoringError("invalid_arguments",
                                 "A foliage stroke allows at most 512 candidates");
        counts.push_back(layout.size());
        for (const auto& placement : layout)
            world_positions.push_back({settings.center[0] + placement.offset[0],
                                       settings.center[2] + placement.offset[1]});
    }
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
            "Choose a resolved Scene Mesh Renderer with published surface geometry");
    const auto& mesh_value = surface->at("components").at("forge.mesh_renderer").at("mesh");
    if (!mesh_value.is_string())
        throw AuthoringError("invalid_surface",
                             "Choose a Mesh Renderer with a published surface Mesh");
    const auto mesh = mesh_value.get<AssetId>();
    const auto catalog = AssetCatalog::open_project(lease.root());
    AffineTransform transform;
    transform.m = surface->at("world_affine").get<std::array<double, 12>>();
    std::vector<std::optional<detail::FoliageSurfaceSample>> samples;
    Json selection;
    if (mesh_surface) {
        const auto& renderer = surface->at("components").at("forge.mesh_renderer");
        if (renderer.contains("wind") && renderer.at("wind").value("enabled", false))
            throw AuthoringError("unsupported_surface",
                                 "Mesh projection does not sample renderer wind");
        const auto key = asset_detail::selected_mesh_revision(catalog, {mesh});
        MeshLimits limits;
        limits.bytes = 8 * 1024 * 1024;
        limits.vertices = 16384;
        limits.indices = 4096 * 3;
        limits.parts = 32;
        const auto prepared =
            asset_detail::load_mesh_selection(lease.root(), catalog, {mesh}, limits);
        samples = detail::mesh_foliage_surfaces(*prepared.value, transform, world_positions);
        if (asset_detail::selected_mesh_revision(AssetCatalog::open_project(lease.root()),
                                                 {mesh}) != key)
            throw AuthoringError("surface_changed",
                                 "Published Mesh changed while preparing placement");
        selection = {{"surface_mesh", mesh},
                     {"surface_revision", key},
                     {"surface_profile", "static_mesh_lod0_v1"}};
    } else {
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
        const auto bytes = asset_detail::read_bytes(
            ProjectPaths(lease.root()).resolve(record.source), terrain_source_byte_limit);
        if (Json(asset_detail::content_digest(bytes)) != selected.at("source_digest"))
            throw AuthoringError(
                "surface_source_changed",
                "Save and publish the heightfield before projecting foliage; source "
                "differs from its rendered selection");
        const auto source = TerrainSource::parse(bytes);
        if (source.asset() != mesh)
            throw AuthoringError("invalid_surface",
                                 "Terrain source and selected Mesh identities differ");
        if (transform.m[1] != 0 || transform.m[9] != 0 || transform.m[5] <= 0)
            throw AuthoringError("unsupported_surface_transform",
                                 "Terrain projection requires an upright surface with positive "
                                 "vertical scale; yaw and horizontal scaling are supported");
        const auto world_to_surface = inverse(transform);
        std::vector<std::array<double, 2>> positions;
        positions.reserve(world_positions.size());
        for (const auto& position : world_positions) {
            const auto point = world_to_surface.point({position[0], transform.m[7], position[1]});
            positions.push_back({point[0], point[2]});
        }
        const auto local_samples = detail::terrain_surfaces(source.raster(), positions);
        const auto normals_to_world = normal_transform(transform);
        samples.resize(local_samples.size());
        for (std::size_t i = 0; i < local_samples.size(); ++i)
            if (local_samples[i])
                samples[i] = detail::FoliageSurfaceSample{
                    transform.point(
                        {positions[i][0], local_samples[i]->height, positions[i][1]})[1],
                    normals_to_world.vector(local_samples[i]->normal)};
        selection = {{"terrain_mesh", mesh},
                     {"terrain_revision", key},
                     {"source_digest", selected.at("source_digest")}};
    }
    const bool filtered = slopes.minimum != 0 || slopes.maximum != 90;
    unsigned retained = 0;
    std::size_t sample_index = 0;
    auto resolved = arguments;
    for (std::size_t dab_index = 0; dab_index < dabs.size(); ++dab_index) {
        auto& dab = dabs[dab_index];
        Json mask = Json::array();
        dab["world_heights"] = Json::array();
        if (align_to_slope)
            dab["world_normals"] = Json::array();
        unsigned dab_retained = 0;
        for (std::size_t i = 0; i < counts[dab_index]; ++i, ++sample_index) {
            const auto& sample = samples.at(sample_index);
            if (!sample)
                throw AuthoringError("surface_coverage",
                                     "The surface must cover every placement origin; move the "
                                     "center or reduce the radius. No objects were created");
            dab["world_heights"].push_back(sample->height + height_offset);
            const auto normal = sample->normal;
            const auto slope =
                std::atan2(std::hypot(normal[0], normal[2]), normal[1]) * (180 / std::numbers::pi);
            if (!std::isfinite(slope) || normal[1] <= 0)
                throw AuthoringError("invalid_surface",
                                     "Surface geometric normal is not upward and finite");
            if (align_to_slope)
                dab["world_normals"].push_back(
                    {{"x", normal[0]}, {"y", normal[1]}, {"z", normal[2]}});
            const bool selected = slope >= slopes.minimum && slope <= slopes.maximum;
            mask.push_back(selected);
            dab_retained += selected;
        }
        if (filtered)
            dab["placement_mask"] = std::move(mask);
        if (!dab_retained)
            throw AuthoringError("slope_excluded",
                                 "No foliage candidates in a dab match the surface slope range; "
                                 "no objects were created");
        retained += dab_retained;
        if (stroke) {
            dab.erase("entity");
            dab.erase("name");
            resolved["dabs"][dab_index] = std::move(dab);
        } else
            resolved = std::move(dab);
    }
    // Complete candidate admission remains with the ordinary Scene authoring owner.
    (void)preview_authoring(scene,
                            Json::array({{{"operation", stroke ? "mesh.stroke" : "mesh.scatter"},
                                          {"arguments", resolved}}}));
    lease.check();
    if (scene.revision() != revision)
        throw AuthoringError("stale_revision", "Scene changed while preparing foliage placement");
    selection["arguments"] = std::move(resolved);
    selection["scene_revision"] = revision;
    selection["candidate_count"] = world_positions.size();
    selection["retained_count"] = retained;
    selection["aligned_to_slope"] = align_to_slope;
    return selection;
}
} // namespace
Json prepare_terrain_foliage(const ProjectLease& lease, const Scene& scene, const Json& arguments,
                             const std::string& surface, double offset,
                             TerrainFoliageSlopeRange slopes, bool align) {
    return prepare_foliage(lease, scene, arguments, surface, offset, slopes, align, false);
}
Json prepare_mesh_foliage(const ProjectLease& lease, const Scene& scene, const Json& arguments,
                          const std::string& surface, double offset,
                          TerrainFoliageSlopeRange slopes, bool align) {
    return prepare_foliage(lease, scene, arguments, surface, offset, slopes, align, true);
}
Json prepare_foliage_stroke(const ProjectLease& lease, const Scene& scene, const Json& arguments,
                            const std::string& surface, bool mesh_surface, double offset,
                            TerrainFoliageSlopeRange slopes, bool align) {
    return prepare_foliage(lease, scene, arguments, surface, offset, slopes, align, mesh_surface,
                           true);
}
} // namespace forge
