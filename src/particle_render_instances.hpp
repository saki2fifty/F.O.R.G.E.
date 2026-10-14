#pragma once
#include "render_bounds.hpp"
#include <forge/material_resource.hpp>
#include <forge/mesh_resource.hpp>
#include <forge/particle_effect.hpp>
namespace forge {
void validate_particle_mesh_profile(const MeshResourceData&);
void validate_particle_material_profile(const MaterialResourceData&);
struct ParticleRenderInstance {
    std::uint64_t birth;
    AffineTransform world;
    std::array<float, 3> color;
};
std::vector<ParticleRenderInstance>
particle_render_instances(const ParticleEffectData&, double, std::uint32_t, const AffineTransform&,
                          std::size_t budget = 1024, double size = 1,
                          std::array<float, 3> tint = {1, 1, 1});
// Conservative whole-lifetime local-space trajectory envelope, transformed by
// the emitter's full affine (including parent scale/shear). No saved AABB authority.
RenderBounds particle_effect_bounds(const ParticleEffectData&, const MeshBounds&,
                                    const AffineTransform&, double size = 1);
} // namespace forge
