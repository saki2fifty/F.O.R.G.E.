#pragma once
#include "render_bounds.hpp"
#include <forge/material_resource.hpp>
#include <forge/mesh_resource.hpp>
#include <forge/particle_effect.hpp>
namespace forge {
void validate_particle_mesh_profile(const MeshResourceData&);
// Effect values have already passed the shared source/resource admission.
void validate_particle_material_profile(const MaterialResourceData&, const ParticleEffectData&);
struct ParticleRenderInstance {
    std::uint64_t birth;
    AffineTransform world;
    std::array<float, 3> color;
    double opacity = 1;
};
std::vector<ParticleRenderInstance>
particle_render_instances(const ParticleEffectData&, double, std::uint32_t, const AffineTransform&,
                          std::size_t budget = 1024, double size = 1,
                          std::array<float, 3> tint = {1, 1, 1});
std::vector<ParticleRenderInstance>
particle_world_render_instances(const ParticleEffectData&, std::span<const ParticleWorldSample>,
                                double time, double size = 1,
                                std::array<float, 3> tint = {1, 1, 1});
std::optional<RenderBounds>
particle_world_bounds(const MeshBounds&, std::span<const ParticleRenderInstance>, bool billboards);
std::optional<RenderBounds>
particle_world_paths_bounds(const ParticleEffectData&, const ParticleWorldPaths&, double time,
                            std::span<const ParticleWorldSample> heads = {}, double size = 1);
// Conservative whole-lifetime local-space trajectory envelope, transformed by
// the emitter's full affine (including parent scale/shear). No saved AABB authority.
RenderBounds particle_effect_bounds(const ParticleEffectData&, const MeshBounds&,
                                    const AffineTransform&, double size = 1);
// CameraView comes from the admitted camera_view owner. Retain the source center
// and X/Y axis magnitudes; only detached per-view orientation changes.
AffineTransform particle_billboard_world(const AffineTransform&, const CameraView&);
} // namespace forge
