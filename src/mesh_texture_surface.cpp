#include "mesh_texture_surface.hpp"
#include "material_shader.hpp"
#include <algorithm>
#include <cmath>

namespace forge {
MeshTextureSurfacePoint mesh_texture_surface_point(const MeshPart& part,
                                                   const MaterialResourceData& material,
                                                   const MeshTextureSurfaceTarget& target,
                                                   const MeshPickTriangle& hit,
                                                   MeshPickBudget& budget) {
    const auto require = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    require(target.texture.id && (target.semantic == TextureSemantic::Color ||
                                  target.semantic == TextureSemantic::Data),
            "Surface painting requires a Color or Data paint texture");
    const auto& values = material.values;
    require(!material.surface && values.alpha == MaterialAlpha::Opaque && values.double_sided &&
                values.depth_test && values.depth_write,
            "Surface painting requires an opaque, two-sided, depth-tested built-in Material");
    const auto binding = material.textures.find(target.slot);
    const auto slot = values.textures.find(target.slot);
    require(binding != material.textures.end() && binding->second == target.texture &&
                slot != values.textures.end(),
            "The chosen Material image slot must use the open paint texture; assign it and publish "
            "the Material first");
    const auto& mapping = slot->second;
    require(mapping.semantic == target.semantic && mapping.dimension == TextureDimension::D2 &&
                mapping.uv_set == 0 && mapping.rotation == 0 &&
                mapping.sampler.compare == TextureCompare::None,
            "Surface painting requires matching Color/Data 2D usage and UV0 without rotation or "
            "comparison sampling");
    for (unsigned axis = 0; axis < 2; ++axis)
        require((mapping.scale[axis] == 1 && mapping.offset[axis] == 0) ||
                    (mapping.scale[axis] == -1 && mapping.offset[axis] == 1),
                "Surface painting supports identity or complete image-axis flips");
    double sum = 0;
    for (const auto weight : hit.barycentric) {
        require(std::isfinite(weight) && weight >= -1e-10 && weight <= 1 + 1e-10,
                "Surface painting coordinates must be inside the triangle");
        sum += weight;
    }
    require(std::abs(sum - 1) <= 1e-8, "Surface painting triangle coordinates must sum to one");
    const MeshUvTriangle triangle(part, hit.primitive, budget);
    require(bool(triangle.coordinates(triangle.uv[0])),
            "Surface painting requires a nondegenerate UV0 triangle");
    const auto rows = material_texture_transform_rows(mapping);
    const auto transform = [&](Double2 uv) -> Double2 {
        return {rows[0][0] * uv[0] + rows[0][1] * uv[1] + rows[0][2],
                rows[1][0] * uv[0] + rows[1][1] * uv[1] + rows[1][2]};
    };
    MeshTextureSurfacePoint result;
    result.uv = transform(triangle.image(hit.barycentric));
    for (auto& value : result.uv)
        value = std::clamp(value, 0., 1.); // Only the admitted barycentric edge tolerance.
    result.indices = triangle.indices;
    result.primitive = hit.primitive;
    for (unsigned k = 0; k < 3; ++k)
        result.triangle_uv[k] = transform(triangle.uv[k]);
    return result;
}
bool mesh_texture_surface_connected(const MeshTextureSurfacePoint& a,
                                    const MeshTextureSurfacePoint& b) {
    if (a.primitive == b.primitive)
        return true;
    std::array<Double2, 2> edge;
    unsigned shared = 0;
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j)
            if (a.indices[i] == b.indices[j] && a.triangle_uv[i] == b.triangle_uv[j]) {
                if (shared < 2)
                    edge[shared] = a.triangle_uv[i];
                ++shared;
            }
    if (shared != 2)
        return false;
    const auto cross = [](Double2 x, Double2 y) { return x[0] * y[1] - x[1] * y[0]; };
    const auto subtract = [](Double2 x, Double2 y) -> Double2 {
        return {x[0] - y[0], x[1] - y[1]};
    };
    const auto direction = subtract(b.uv, a.uv), border = subtract(edge[1], edge[0]),
               offset = subtract(edge[0], a.uv);
    const double area = cross(direction, border), epsilon = 1e-10;
    if (std::abs(area) <= epsilon)
        return std::abs(cross(offset, border)) <= epsilon;
    const double t = cross(offset, border) / area, u = cross(offset, direction) / area;
    return t >= -epsilon && t <= 1 + epsilon && u >= -epsilon && u <= 1 + epsilon;
}
bool MeshTextureSurfaceHit::same_selection(const MeshTextureSurfaceHit& other) const {
    return entity == other.entity && mesh == other.mesh && material == other.material &&
           mesh_revision == other.mesh_revision && mesh_variant == other.mesh_variant &&
           material_revision == other.material_revision &&
           material_variant == other.material_variant && lod == other.lod;
}
} // namespace forge
