#include "render_bounds.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
float shader_dot(const std::array<float, 4>& row, const std::array<float, 4>& p, bool fused) {
    if (fused)
        return std::fma(row[0], p[0],
                        std::fma(row[1], p[1], std::fma(row[2], p[2], row[3] * p[3])));
    volatile float a = row[0] * p[0], b = row[1] * p[1], c = row[2] * p[2], d = row[3] * p[3];
    volatile float ab = a + b, cd = c + d;
    return ab + cd;
}
// Independent point oracle: execute actual float object rows, camera basis and
// projection, never call the producer's interval/bounds transform routines.
std::array<float, 4> shader_point(const AffineTransform& world, const CameraView& camera,
                                  std::array<float, 4> p, bool fused) {
    std::array<float, 4> relative{0, 0, 0, 1}, view{0, 0, 0, 1}, clip{};
    for (unsigned r = 0; r < 3; ++r) {
        std::array<float, 4> row{};
        for (unsigned c = 0; c < 4; ++c)
            row[c] = float(world.m[4 * r + c] - (c == 3 ? camera.position[r] : 0.));
        relative[r] = shader_dot(row, p, fused);
    }
    const Double3* axes[]{&camera.right, &camera.up, &camera.forward};
    for (unsigned r = 0; r < 3; ++r) {
        std::array<float, 4> row{};
        for (unsigned c = 0; c < 3; ++c)
            row[c] = float((*axes[r])[c]);
        view[r] = shader_dot(row, relative, fused);
    }
    for (unsigned r = 0; r < 4; ++r) {
        std::array<float, 4> row{};
        std::copy_n(camera.projection.begin() + r * 4, 4, row.begin());
        clip[r] = shader_dot(row, view, fused);
    }
    return clip;
}
} // namespace
int main() {
    try {
        Camera lens;
        lens.near_plane = .1;
        lens.far_plane = 10000;
        const MeshBounds unit{{-1, -1, -1}, {1, 1, 1}};
        auto camera = camera_view(lens, AffineTransform{}, 1920, 1080);
        AffineTransform world;
        world.m[11] = 20;
        const auto ordinary = mesh_occlusion_bounds(unit, world, camera);
        check(ordinary && ordinary->nearest_depth > .99f && ordinary->nearest_depth < 1,
              "Ordinary projected bounds declined or lost useful nearest depth");
        world.m[11] = 0;
        check(!mesh_occlusion_bounds(unit, world, camera), "Eye crossing admitted for culling");
        world.m[11] = 1;
        check(!mesh_occlusion_bounds(unit, world, camera), "Near crossing admitted for culling");
        world.m[11] = 10000;
        check(!mesh_occlusion_bounds(unit, world, camera), "Far crossing admitted for culling");
        world.m[11] = 20;
        world.m[0] = std::numeric_limits<double>::max();
        check(!mesh_occlusion_bounds(unit, world, camera), "GPU overflow admitted for culling");
        world.m[0] = 1;
        world.m[1] = 1e-40;
        check(!mesh_occlusion_bounds(unit, world, camera),
              "Flush-sensitive coefficient admitted for culling");
        world.m[1] = 0;
        camera.projection[0] = NAN;
        check(!mesh_occlusion_bounds(unit, world, camera), "Nonfinite projection admitted");
        camera = camera_view(lens, AffineTransform{}, 1920, 1080);
        auto invalid = unit;
        invalid.minimum[0] = 2;
        check(!mesh_occlusion_bounds(invalid, world, camera), "Reversed bounds admitted");
        invalid = unit;
        invalid.maximum[0] = INFINITY;
        check(!mesh_occlusion_bounds(invalid, world, camera), "Nonfinite bounds admitted");
        std::mt19937 random(923551);
        std::uniform_real_distribution<float> signed_value(-1, 1), ratio(0, 1);
        unsigned admitted = 0, points = 0;
        for (unsigned trial = 0; trial < 2400; ++trial) {
            lens.projection = trial % 2;
            lens.basis = (trial / 2) % 2;
            lens.flip_x = bool(trial & 4);
            lens.flip_y = bool(trial & 8);
            lens.orthographic_height = 100;
            lens.infinite_far = !lens.projection && bool(trial & 16);
            lens.viewport_x = .17;
            lens.viewport_y = .11;
            lens.viewport_width = .73;
            lens.viewport_height = .81;
            LocalTransform camera_local;
            camera_local.rotation = rotation_from_euler({17, -31, 9});
            const auto origin = trial % 3 ? Double3{1e12, -2e12, 3e12} : Double3{};
            auto camera_world = affine_transform(camera_local);
            for (unsigned i = 0; i < 3; ++i)
                camera_world.m[4 * i + 3] = origin[i];
            camera = camera_view(lens, camera_world, 1920, 1080);
            // Include actual projection jitter and arbitrary negative/sheared or
            // singular object matrices. Large source coordinates cancel only in
            // GPU-float rows; double authored world boxes alone are insufficient.
            camera.projection[2] += signed_value(random) * .001f;
            camera.projection[6] += signed_value(random) * .001f;
            MeshBounds source = unit;
            if (trial % 5 == 0)
                for (unsigned i = 0; i < 3; ++i) {
                    source.minimum[i] += 1000000;
                    source.maximum[i] += 1000000;
                }
            for (unsigned r = 0; r < 3; ++r) {
                double center_offset = 0;
                for (unsigned c = 0; c < 3; ++c) {
                    world.m[r * 4 + c] = trial % 13 ? signed_value(random) * 3 : 0;
                    center_offset +=
                        world.m[r * 4 + c] * (double(source.minimum[c]) + source.maximum[c]) * .5;
                }
                world.m[r * 4 + 3] = origin[r] + camera.forward[r] * 100 +
                                     camera.right[r] * signed_value(random) * 10 - center_offset;
            }
            const auto bounds = mesh_occlusion_bounds(source, world, camera);
            if (!bounds)
                continue;
            ++admitted;
            for (unsigned sample = 0; sample < 72; ++sample) {
                std::array<float, 4> p{0, 0, 0, 1};
                for (unsigned axis = 0; axis < 3; ++axis)
                    p[axis] = sample < 8
                                  ? ((sample & (1u << axis)) ? source.maximum[axis]
                                                             : source.minimum[axis])
                                  : float(double(source.minimum[axis]) +
                                          (double(source.maximum[axis]) - source.minimum[axis]) *
                                              ratio(random));
                for (const bool fused : {false, true}) {
                    const auto clip = shader_point(world, camera, p, fused);
                    check(clip[3] > 0 && clip[2] >= 0 && clip[2] <= clip[3],
                          "Admitted float oracle escaped near/far guards");
                    const double x = (double(clip[0]) / clip[3] + 1) * .5 * camera.viewport.width;
                    const double y = (1 - double(clip[1]) / clip[3]) * .5 * camera.viewport.height;
                    check(bounds->nearest_depth <= double(clip[2]) / clip[3],
                          "Float shader point lies in front of the conservative nearest depth");
                    if (x >= 0 && x < camera.viewport.width && y >= 0 && y < camera.viewport.height)
                        check(x >= bounds->rectangle[0] && x < bounds->rectangle[2] &&
                                  y >= bounds->rectangle[1] && y < bounds->rectangle[3],
                              "Float shader point escaped its conservative pixel rectangle");
                    ++points;
                }
            }
        }
        check(admitted > 1800 && points > 250000, "Projection oracle performed insufficient work");
        std::cout << "Conservative GPU-float bounds: " << admitted << " views, " << points
                  << " separate/fused point projections; near/far/invalid guards passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
