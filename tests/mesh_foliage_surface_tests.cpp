#include "mesh_foliage_surface.hpp"
#include <cmath>
#include <forge/editable_mesh.hpp>
#include <iostream>
#include <stdexcept>

using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F operation) {
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid Mesh foliage surface was admitted");
}
MeshResourceData ramp() {
    auto source = EditableMeshSource::create_cube(AssetId::generate());
    for (auto& vertex : source.document["vertices"]) {
        auto p = vertex["position"].get<Double3>();
        p[1] += .25 * p[0] + .5 * p[2];
        vertex["position"] = p;
    }
    MeshResourceData result;
    result.mesh = source.cook();
    return result;
}
bool near(double a, double b) { return std::abs(a - b) < 1e-10; }
} // namespace
int main() {
    try {
        auto data = ramp();
        const std::array<Double2, 5> points{{{0, 0}, {.2, -.2}, {.5, .5}, {-.5, -.5}, {2, 0}}};
        const auto sampled = detail::mesh_foliage_surfaces(data, {}, points);
        for (unsigned i = 0; i < 4; ++i) {
            check(sampled[i] &&
                      near(sampled[i]->height, .5 + .25 * points[i][0] + .5 * points[i][1]),
                  "Mesh query differs from independent ramp plane");
            const auto n = sampled[i]->normal;
            check(near(n[0] / n[1], -.25) && near(n[2] / n[1], -.5),
                  "Projection used shading normals instead of the geometric ramp normal");
        }
        check(!sampled.back(), "Mesh query invented coverage beyond its boundary");
        AffineTransform mirrored;
        mirrored.m[0] = -2;
        mirrored.m[5] = 3;
        mirrored.m[10] = 1.5;
        mirrored.m[7] = 4;
        const std::array<Double2, 1> center{{{0, 0}}};
        const auto transformed = detail::mesh_foliage_surfaces(data, mirrored, center)[0];
        check(transformed && near(transformed->height, 5.5) &&
                  near(transformed->normal[0] / transformed->normal[1], .375) &&
                  near(transformed->normal[2] / transformed->normal[1], -1),
              "Signed/nonuniform surface transform changed height or geometric normal");
        auto upper = data.mesh.lods[0].parts[0];
        auto& values = std::get<std::vector<float>>(upper.streams[0].values);
        check(upper.streams[0].semantic == "POSITION", "Fixture position stream changed");
        for (unsigned i = 1; i < values.size(); i += 3)
            values[i] += 2;
        upper.bounds = mesh_bounds(upper);
        data.mesh.lods[0].parts.push_back(upper);
        check(near(detail::mesh_foliage_surfaces(data, {}, center)[0]->height, 2.5),
              "Stacked surfaces did not select the highest upward intersection");
        auto crossing = ramp();
        auto horizontal_part = crossing.mesh.lods[0].parts[0];
        auto& flat_positions = std::get<std::vector<float>>(horizontal_part.streams[0].values);
        for (unsigned i = 1; i < flat_positions.size(); i += 3)
            flat_positions[i] = .5f;
        horizontal_part.bounds = mesh_bounds(horizontal_part);
        crossing.mesh.lods[0].parts.push_back(horizontal_part);
        const auto crossing_samples = detail::mesh_foliage_surfaces(crossing, {}, points);
        check(near(crossing_samples[0]->normal[0] / crossing_samples[0]->normal[1], -.25),
              "Equal-height surfaces lost stable first-part ordering");
        check(near(crossing_samples[1]->height, .5) && crossing_samples[1]->normal[0] == 0 &&
                  crossing_samples[1]->normal[2] == 0,
              "Crossing surfaces retained the wrong highest normal or shading normal");
        auto flat = EditableMeshSource::create_planar_quad(AssetId::generate());
        MeshResourceData panel;
        panel.mesh = flat.cook();
        AffineTransform horizontal;
        horizontal.m = {1, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0};
        check(detail::mesh_foliage_surfaces(panel, horizontal, center)[0].has_value(),
              "Rotated planar Mesh did not provide an upward surface");
        const std::array<std::uint32_t, 1> face{5};
        flat.reverse_faces(face);
        panel.mesh = flat.cook();
        check(!detail::mesh_foliage_surfaces(panel, horizontal, center)[0],
              "Downward-only surface was treated as an upward floor");
        rejects([&] { (void)detail::mesh_foliage_surfaces(data, {}, {}); });
        std::vector<Double2> excessive(513);
        rejects([&] { (void)detail::mesh_foliage_surfaces(data, {}, excessive); });
        AffineTransform flattened;
        flattened.m[5] = 0;
        const auto flat_sample = detail::mesh_foliage_surfaces(data, flattened, center)[0];
        check(flat_sample && near(flat_sample->height, 0) && flat_sample->normal[1] > 0,
              "Rank-two horizontal geometry lost its existing oriented area normal");
        flattened.m[0] = 0;
        check(!detail::mesh_foliage_surfaces(data, flattened, center)[0],
              "Collapsed projected triangles invented surface coverage");
        auto too_many = ramp();
        auto& part = too_many.mesh.lods[0].parts[0];
        const auto triangle = std::array{part.indices[0], part.indices[1], part.indices[2]};
        part.indices.clear();
        for (unsigned i = 0; i < 4097; ++i)
            part.indices.insert(part.indices.end(), triangle.begin(), triangle.end());
        rejects([&] { (void)detail::mesh_foliage_surfaces(too_many, {}, center); });
        std::cout << "Static Mesh foliage height/normal/coverage/stacking/bounds pass\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
