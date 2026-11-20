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
struct alignas(16) ParticleGpuGraphLimits {
    // Symmetric emitter-local position extents (meters), then maximum unmodulated size.
    ParticleGpuRow extent_size{1, 1, 1, 1};
};
void validate_particle_gpu_graph_limits(const ParticleGpuGraphLimits&);
struct alignas(16) ParticleGpuValues {
    std::array<std::uint32_t, 4> births{}, modes{};
    ParticleGpuRow timing{}, axis{}, side{}, up{}, acceleration{}, shape{}, sizes{}, tint{};
    std::array<ParticleGpuRow, 3> emitter{};
    std::array<ParticleGpuRow, 2> colors{};
    std::array<ParticleGpuRow, 16> size_stops{}, color_stops{};
};
static_assert(sizeof(ParticleGpuValues) == 47 * 16);
// Prepared opacity uses device float precision. This is private projection input,
// independent of source history and the immutable 47-row evaluator/graph layout.
struct ParticleGpuOpacityValues {
    std::array<float, 2> endpoints{1, 1};
    unsigned count = 0;
    std::array<ParticleGpuRow, 16> stops{};
};
void validate_particle_gpu_opacity_values(const ParticleGpuOpacityValues&);
ParticleGpuOpacityValues prepare_particle_gpu_opacity_values(const ParticleEffectData&);
// Separate view/appearance projection preserves the original evaluator and graph
// program input layout. The camera comes from the shared admitted view owner.
struct alignas(16) ParticleGpuBillboardValues {
    std::array<std::uint32_t, 4> counts{};
    std::array<ParticleGpuRow, 3> camera{};
    ParticleGpuRow timing{}, opacity{};
    std::array<ParticleGpuRow, 16> opacity_stops{};
};
static_assert(sizeof(ParticleGpuBillboardValues) == 22 * 16);
ParticleGpuBillboardValues prepare_particle_gpu_billboard_values(const CameraView&, unsigned count);
void validate_particle_gpu_graph_envelope(const ParticleGpuValues&, const ParticleGpuGraphLimits&);
ParticleGpuValues prepare_particle_gpu_values(const ParticleEffectData&, double seconds,
                                              std::uint32_t seed, const AffineTransform&,
                                              const CameraView&, double size = 1,
                                              std::array<float, 3> tint = {1, 1, 1});
} // namespace forge
