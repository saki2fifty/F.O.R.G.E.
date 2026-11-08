#pragma once
#include <forge/authoring.hpp>
#include <forge/project_lease.hpp>
namespace forge {
struct TerrainFoliageSlopeRange {
    double minimum = 0, maximum = 90;
};
// Read one published terrain selection and prepare ordinary mesh.scatter values.
// The caller uses the returned Scene revision with the existing command/history
// owner. This function does not publish assets or change Scene intent.
Json prepare_terrain_foliage(const ProjectLease&, const Scene&, const Json& arguments,
                             const std::string& terrain_entity, double height_offset = 0,
                             TerrainFoliageSlopeRange slopes = {}, bool align_to_slope = false);
// The same placement/history owner with a published static triangle Mesh surface.
// Source drafts are ignored; the highest upward-facing published surface wins.
Json prepare_mesh_foliage(const ProjectLease&, const Scene&, const Json& arguments,
                          const std::string& surface_entity, double height_offset = 0,
                          TerrainFoliageSlopeRange slopes = {}, bool align_to_slope = false);
// Prepare all ordinary dabs from one immutable surface selection, then validate
// the complete atomic mesh.stroke through the existing Scene command owner.
Json prepare_foliage_stroke(const ProjectLease&, const Scene&, const Json& arguments,
                            const std::string& surface_entity, bool mesh_surface,
                            double height_offset = 0, TerrainFoliageSlopeRange slopes = {},
                            bool align_to_slope = false);
} // namespace forge
