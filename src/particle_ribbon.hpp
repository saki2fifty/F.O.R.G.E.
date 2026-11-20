#pragma once
#include "mesh_instance_layout.hpp"
#include "particle_path.hpp"
#include "render_bounds.hpp"
#include <forge/render_view.hpp>
namespace forge {
struct ParticleRibbonSegment {
    std::uint64_t birth = 0;
    unsigned segment = 0;
    MeshRibbonValues vertices;
    AffineTransform world;
    RenderBounds bounds;
};
// Detached camera geometry. Adjacent segments share the same endpoint edges;
// World is a centered composition proxy; bounds enclose the actual four corners.
// Drawing uses explicit endpoints/sides, not a second affine geometry model.
std::vector<ParticleRibbonSegment> particle_ribbon_segments(const ParticlePathSamples&,
                                                            const AffineTransform&,
                                                            const CameraView&, double size = 1,
                                                            std::array<float, 3> tint = {1, 1, 1});
} // namespace forge
