#pragma once
#include <cmath>
#include <forge/render_view.hpp>
#include <limits>
#include <stdexcept>
inline void test_light_image_frames() {
    using namespace forge;
    const auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    const auto reject = [&](auto action) {
        bool rejected = false;
        try {
            action();
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, "Invalid light image frame was admitted");
    };
    const auto close = [&](double a, double b) {
        check(std::abs(a - b) < 1e-12, "Light image frame changed its physical axes or scale");
    };
    Light source;
    AffineTransform world;
    world.m[3] = 10;
    world.m[7] = 20;
    world.m[11] = 30;
    source.image_width = 4;
    source.image_height = 2;
    const auto directional = light_image_frame(source, world);
    check(directional.position == Double3{10, 20, 30} && directional.clipped &&
              directional.kind == unsigned(LightKind::Directional),
          "Directional image lost its anchor or clipping policy");
    close(directional.rows[0][0], .25);
    close(directional.rows[1][1], .5);
    close(directional.rows[2][2], 1);
    const auto scaled = [&] {
        auto value = world;
        value.m[0] = 5;
        value.m[5] = 7;
        value.m[10] = 11;
        auto tiled = source;
        tiled.image_tiled = true;
        return light_image_frame(tiled, value);
    }();
    check(scaled.rows == directional.rows && !scaled.clipped,
          "Node scale resized a directional image or forced clipping");
    for (const auto kind : {LightKind::Rectangle, LightKind::Sphere}) {
        source.kind = unsigned(kind);
        reject([&] { light_image_frame(source, world); });
    }
    source.kind = unsigned(LightKind::Point);
    const auto point = light_image_frame(source, world);
    check(!point.clipped, "Point cubemap was given planar clipping");
    close(point.rows[0][0], 1);
    close(point.rows[1][1], 1);
    close(point.rows[2][2], 1);
    auto mirrored_world = world;
    mirrored_world.m[0] = -5;
    mirrored_world.m[5] = 7;
    mirrored_world.m[10] = 11;
    const auto mirrored = light_image_frame(source, mirrored_world);
    close(mirrored.rows[0][0], -1);
    close(mirrored.rows[1][1], 1);
    close(mirrored.rows[2][2], 1);
    source.basis = unsigned(ViewBasis::GltfNegativeZ);
    const auto negative_z = light_image_frame(source, world);
    close(negative_z.rows[0][0], 1);
    close(negative_z.rows[1][1], 1);
    close(negative_z.rows[2][2], -1);
    source.basis = unsigned(ViewBasis::ForgePositiveZ);
    source.kind = unsigned(LightKind::Spot);
    const auto spot = light_image_frame(source, world);
    const double cosine = light_view(source, world).cosine_outer;
    const double tangent = std::sqrt(1 - cosine * cosine) / cosine;
    close(spot.rows[0][0] * tangent, .5);
    close(spot.rows[1][1] * tangent, .5);
    check(spot.clipped, "Spot image escaped its projected bounds");
    source.image_width = 8;
    source.image_height = 3;
    source.image_tiled = true;
    const auto other_size = light_image_frame(source, world);
    check(other_size.rows == spot.rows && other_size.clipped,
          "Directional-only image settings changed a spot projection");
    // A known orthonormal frame with nonuniform scale and two independent shears.
    // These golden axes do not repeat the producer's orthogonalization algorithm.
    constexpr Double3 right{.6, 0, -.8}, up{-.48, .8, -.36}, forward{.64, .6, .48};
    AffineTransform shear;
    for (unsigned axis = 0; axis < 3; ++axis) {
        shear.m[4 * axis] = 5 * right[axis] + 2 * forward[axis];
        shear.m[4 * axis + 1] = 7 * up[axis] + right[axis] + 3 * forward[axis];
        shear.m[4 * axis + 2] = 11 * forward[axis];
    }
    source.kind = unsigned(LightKind::Point);
    const auto oriented = light_image_frame(source, shear);
    for (unsigned axis = 0; axis < 3; ++axis) {
        close(oriented.rows[0][axis], right[axis]);
        close(oriented.rows[1][axis], up[axis]);
        close(oriented.rows[2][axis], forward[axis]);
    }
    for (unsigned axis = 0; axis < 3; ++axis)
        shear.m[4 * axis + 1] = -7 * up[axis] + right[axis] + 3 * forward[axis];
    const auto reflected_up = light_image_frame(source, shear);
    for (unsigned axis = 0; axis < 3; ++axis)
        close(reflected_up.rows[1][axis], -up[axis]);
    const Double3 origin{std::ldexp(1., 40), -std::ldexp(1., 40), std::ldexp(1., 40)};
    AffineTransform near, distant;
    near.m[3] = 3;
    near.m[7] = 1;
    near.m[11] = -2;
    distant = near;
    for (unsigned axis = 0; axis < 3; ++axis)
        distant.m[4 * axis + 3] += origin[axis];
    const auto gpu = light_image_gpu_frame(light_image_frame(source, distant), origin);
    check(gpu == light_image_gpu_frame(light_image_frame(source, near), {}),
          "Light image origin rebasing changed GPU coordinates");
    check(gpu[0] == std::array<float, 4>{3, 1, -2, 0},
          "GPU image anchor was baked into an imprecise affine translation");
    auto collapsed = world;
    collapsed.m[0] = 0;
    collapsed.m[8] = 1;
    (void)light_view(source, collapsed); // Ordinary point lighting remains admitted.
    reject([&] { light_image_frame(source, collapsed); });
    collapsed = world;
    collapsed.m[1] = 1;
    collapsed.m[5] = 0;
    (void)light_view(source, collapsed);
    reject([&] { light_image_frame(source, collapsed); });
    for (double size : {0., .00099, 10001., std::numeric_limits<double>::infinity()})
        reject([&] {
            auto invalid = source;
            invalid.image_width = float(size);
            light_image_frame(invalid, world);
        });
    auto invalid = point;
    invalid.rows[0][0] = std::numeric_limits<double>::infinity();
    reject([&] { light_image_gpu_frame(invalid, {}); });
    reject([&] { light_image_gpu_frame(point, {std::numeric_limits<double>::infinity(), 0, 0}); });
    invalid = point;
    invalid.rows[0][0] = std::numeric_limits<double>::denorm_min();
    reject([&] { light_image_gpu_frame(invalid, {}); });
}
