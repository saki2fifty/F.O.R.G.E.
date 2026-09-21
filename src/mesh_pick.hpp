#pragma once
#include "model_instance_pose.hpp"
namespace forge {
// Selection uses admitted CPU geometry and the retained complete draw pose.
// The result is D3D projected depth, comparable across every object in one view.
// Visibility, opacity and culling do not change geometric selectability.
struct MeshPickBudget {
    // Shared by all meshes in one click; exhaustion rejects the whole query.
    std::uint64_t remaining = 1024 * 1024;
};
std::optional<double> pick_mesh_part(const MeshPart&, const MeshPartPose&, const MeshInstancePose&,
                                     const CameraView&, double pixel_x, double pixel_y,
                                     MeshPickBudget&, double point_line_radius = 5);
} // namespace forge
