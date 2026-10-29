#pragma once
#include <forge/reflection_probe.hpp>
#include <forge/transform.hpp>
namespace forge {
// Derived geometry, not another authored component. Unit world plane normals
// keep fade distances in world metres under nonuniform or sheared transforms.
struct ReflectionProbeVolume {
    Double3 center{};
    std::array<std::array<double, 4>, 3> planes{};
    std::array<double, 2> map_rotation{1, 0};
};
ReflectionProbeVolume reflection_probe_volume(const ReflectionProbe&, const AffineTransform&);
std::array<float, 3> reflection_probe_view_center(const ReflectionProbeVolume&, Double3 origin);
} // namespace forge
