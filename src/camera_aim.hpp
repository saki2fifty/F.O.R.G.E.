#pragma once
#include <forge/render_view.hpp>

namespace forge::detail {
// Detached view correction, without a transform writer or independent clock.
// Weight selects the smallest turn from the authored physical forward direction.
AffineTransform aim_camera_transform(const Camera&, const AffineTransform&, Double3 target,
                                     double weight);
} // namespace forge::detail
