#pragma once
#include "mesh_instance_layout.hpp"
#include <array>
#include <cstdint>
#include <forge/particle_effect.hpp>
#include <forge/render_view.hpp>

namespace forge {
using ParticleGpuRow = MeshInstanceRow;
// One immutable, bounded compute submission. Shader storage matches these rows;
// it carries no World, resource pointer, source identity or independent clock.
struct alignas(16) ParticleGpuValues {
    std::array<std::uint32_t, 4> births{}, modes{};
    ParticleGpuRow timing{}, axis{}, side{}, up{}, acceleration{}, shape{}, sizes{}, tint{};
    std::array<ParticleGpuRow, 3> emitter{};
    std::array<ParticleGpuRow, 2> colors{};
    std::array<ParticleGpuRow, 16> size_stops{}, color_stops{};
};
static_assert(sizeof(ParticleGpuValues) == 47 * 16);
ParticleGpuValues prepare_particle_gpu_values(const ParticleEffectData&, double seconds,
                                              std::uint32_t seed, const AffineTransform&,
                                              const CameraView&, double size = 1,
                                              std::array<float, 3> tint = {1, 1, 1});
} // namespace forge
