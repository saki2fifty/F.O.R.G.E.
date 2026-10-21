#pragma once
#include <array>
#include <forge/transform.hpp>

namespace forge::physics_detail {
// Shared checked rigid-pose projection for native admission and copied Scene
// guides. The caller derives sign preferences from the existing spatial ancestry.
void validate_planar_rotation_2d(LocalRotation);
LocalTransform checked_planar_pose_2d(const AffineTransform&, const LocalTransform& hint);
// A derived XY conversion, never an ECS transform or a general 3D inverse.
// Zero Z scale is allowed; XY must be planar and sufficiently well conditioned.
// This checks affine geometry only. Body/ancestor rotation and collider admission
// remain the physics owner's responsibility.
class PlanarMapping {
  public:
    explicit PlanarMapping(const AffineTransform& parent);
    std::array<double, 2> local_point(std::array<double, 2> world) const;
    std::array<double, 2> local_vector(std::array<double, 2> world) const;

  private:
    std::array<double, 4> inverse_;
    std::array<double, 2> origin_;
};
} // namespace forge::physics_detail
