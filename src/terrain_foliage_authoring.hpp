#pragma once
#include <forge/authoring.hpp>
#include <forge/project_lease.hpp>
namespace forge {
// Read one published terrain selection and prepare ordinary mesh.scatter values.
// The caller uses the returned Scene revision with the existing command/history
// owner. This function does not publish assets or change Scene intent.
Json prepare_terrain_foliage(const ProjectLease&, const Scene&, const Json& arguments,
                             const std::string& terrain_entity, double height_offset = 0);
} // namespace forge
