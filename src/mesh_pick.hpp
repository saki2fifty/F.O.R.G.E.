#pragma once
#include "model_instance_pose.hpp"
namespace forge {
// Selection uses admitted CPU geometry and the retained complete draw pose.
// The result is D3D projected depth, comparable across every object in one view.
// Visibility, opacity and culling do not change geometric selectability.
struct MeshPickBudget {
    // Shared by all meshes in one click; exhaustion rejects the whole query.
    std::uint64_t remaining = 1024 * 1024;
    void spend();
};
// Short-lived evaluator borrowing the admitted draw and its current pose. Picking,
// capture and reverse UV projection share deformation and camera-relative math.
class MeshPickVertices {
  public:
    MeshPickVertices(const MeshPart&, const MeshPartPose&, const MeshInstancePose&);
    Double3 relative(std::uint32_t index, Double3 origin, MeshPickBudget&) const;

  private:
    const MeshPart& part_;
    const MeshPartPose& part_pose_;
    const MeshInstancePose& pose_;
    const std::vector<float>& base_;
    std::array<const std::vector<float>*, 256> deltas_{};
    std::array<unsigned, 256> active_{};
    unsigned active_count_ = 0;
    const std::vector<std::uint32_t>* joints_ = nullptr;
    const std::vector<float>* weights_ = nullptr;
};
std::array<std::uint32_t, 3> mesh_triangle_indices(const MeshPart&, std::size_t primitive);
struct MeshPickTriangle {
    std::size_t primitive = 0;
    // Perspective-correct coordinates of the original (unclipped) triangle.
    Double3 barycentric{};
};
struct MeshPickHit {
    double depth = 0;
    std::optional<MeshPickTriangle> triangle;
};
std::optional<MeshPickHit> pick_mesh_part_hit(const MeshPart&, const MeshPartPose&,
                                              const MeshInstancePose&, const CameraView&,
                                              double pixel_x, double pixel_y, MeshPickBudget&,
                                              double point_line_radius = 5);
std::optional<double> pick_mesh_part(const MeshPart&, const MeshPartPose&, const MeshInstancePose&,
                                     const CameraView&, double pixel_x, double pixel_y,
                                     MeshPickBudget&, double point_line_radius = 5);
} // namespace forge
