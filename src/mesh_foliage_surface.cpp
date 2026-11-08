#include "mesh_foliage_surface.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace forge::detail {
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
Double3 subtract(Double3 a, Double3 b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Double3 cross(Double3 a, Double3 b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
} // namespace
std::vector<std::optional<FoliageSurfaceSample>>
mesh_foliage_surfaces(const MeshResourceData& data, const AffineTransform& world,
                      std::span<const Double2> positions) {
    require(!positions.empty() && positions.size() <= 512,
            "Mesh projection requires 1 to 512 placement origins");
    for (const auto& p : positions)
        require(std::isfinite(p[0]) && std::isfinite(p[1]),
                "Mesh projection origins must be finite");
    const auto& mesh = data.mesh;
    validate_mesh(mesh);
    require(mesh.morph_names.empty() && mesh.morph_defaults.empty(),
            "Mesh projection requires static geometry without morph targets");
    if (data.model)
        for (const auto& node : data.model->nodes)
            require(!node.skin && node.morph_weights.empty(),
                    "Mesh projection does not sample skinned or morphing Model nodes");
    const auto normals = normal_transform(world);
    std::vector<std::optional<FoliageSurfaceSample>> result(positions.size());
    unsigned triangles = 0;
    for (const auto& part : mesh.lods.at(0).parts) {
        require(part.topology == MeshTopology::Triangles && part.joint_palette.empty() &&
                    part.morph_targets.empty(),
                "Mesh projection requires static triangle parts");
        for (const auto& stream : part.streams)
            require(!stream.semantic.starts_with("JOINTS_") &&
                        !stream.semantic.starts_with("WEIGHTS_"),
                    "Mesh projection does not sample skin attributes");
        const auto* stream = part.find("POSITION");
        require(stream && stream->components == 3 &&
                    std::holds_alternative<std::vector<float>>(stream->values),
                "Mesh projection requires float3 POSITION");
        const auto& values = std::get<std::vector<float>>(stream->values);
        const auto point = [&](std::size_t corner) {
            const auto index = part.indices.empty() ? corner : part.indices.at(corner);
            return Double3{values.at(index * 3), values.at(index * 3 + 1),
                           values.at(index * 3 + 2)};
        };
        const auto count = part.indices.empty() ? part.vertices : part.indices.size();
        for (std::size_t t = 0; t < count; t += 3) {
            require(++triangles <= 4096, "Mesh projection exceeds 4096 LOD0 triangles");
            const auto la = point(t), lb = point(t + 1), lc = point(t + 2);
            const auto normal = normals.vector(cross(subtract(lb, la), subtract(lc, la)));
            require(std::all_of(normal.begin(), normal.end(),
                                [](double v) { return std::isfinite(v); }),
                    "Mesh projection normal is nonfinite");
            if (normal[1] <= 0)
                continue;
            const auto a = world.point(la), b = world.point(lb), c = world.point(lc);
            for (const auto& p : {a, b, c})
                require(std::all_of(p.begin(), p.end(), [](double v) { return std::isfinite(v); }),
                        "Mesh projection position is nonfinite");
            const auto ab = subtract(b, a), ac = subtract(c, a);
            const double scale =
                std::max({std::abs(ab[0]), std::abs(ab[2]), std::abs(ac[0]), std::abs(ac[2])});
            if (scale == 0)
                continue;
            const double bx = ab[0] / scale, bz = ab[2] / scale, cx = ac[0] / scale,
                         cz = ac[2] / scale, area = bx * cz - bz * cx;
            // Near-vertical projections do not define a stable height surface.
            if (std::abs(area) <= 1e-12)
                continue;
            for (std::size_t i = 0; i < positions.size(); ++i) {
                const auto p = positions[i];
                if (p[0] < std::min({a[0], b[0], c[0]}) || p[0] > std::max({a[0], b[0], c[0]}) ||
                    p[1] < std::min({a[2], b[2], c[2]}) || p[1] > std::max({a[2], b[2], c[2]}))
                    continue;
                const double x = (p[0] - a[0]) / scale, z = (p[1] - a[2]) / scale;
                double u = (x * cz - z * cx) / area, v = (bx * z - bz * x) / area;
                if (u < -1e-12 || v < -1e-12 || u + v > 1 + 1e-12)
                    continue;
                u = std::clamp(u, 0., 1.);
                v = std::clamp(v, 0., 1. - u);
                const double height = a[1] + ab[1] * u + ac[1] * v;
                require(std::isfinite(height), "Mesh projection height is nonfinite");
                if (!result[i] || height > result[i]->height)
                    result[i] = FoliageSurfaceSample{height, normal};
            }
        }
    }
    return result;
}
} // namespace forge::detail
