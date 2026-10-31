#pragma once
#include "reflection_probe_volume.hpp"
#include <forge/identity.hpp>
namespace Diligent {
struct ITextureView;
}
namespace forge {
inline constexpr unsigned environment_probe_limit = 8;
inline constexpr unsigned environment_probe_rows = 1 + 5 * environment_probe_limit;
// Borrowed, priority-ordered lighting from the existing probe resource owner.
// Prepared draws and the specular pass retain no source identity or map leases.
struct EnvironmentProbe {
    ReflectionProbeVolume volume;
    Diligent::ITextureView* specular = nullptr;
    double intensity = 1, blend_distance = 1;
    bool box_projection = true;
    EntityId entity{};
    Diligent::ITextureView* irradiance = nullptr;
    double diffuse_intensity = 0;
};
} // namespace forge
