#include "physics_2d_transform.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace forge;
using physics_detail::PlanarMapping;
namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void rejects(F&& operation) {
    bool failed = false;
    try {
        operation();
    } catch (const std::runtime_error&) {
        failed = true;
    }
    require(failed, "Invalid XY conversion was accepted");
}
void close(std::array<double, 2> actual, std::array<double, 2> expected, double tolerance) {
    for (unsigned i = 0; i < 2; ++i)
        require(std::isfinite(actual[i]) && std::abs(actual[i] - expected[i]) <= tolerance,
                "XY conversion disagrees with the existing forward transform");
}
void forward_cases() {
    unsigned checked = 0;
    for (float sx : {-2.f, 2.f})
        for (float sy : {-3.f, 3.f})
            for (float sz : {-1.f, 0.f, 1.f})
                for (double angle : {-175., -30., 0., 67., 179.})
                    for (double offset : {0., -1000000., 1000000., 1000000000000.}) {
                        const LocalTransform parent{{offset, -offset, 12},
                                                    rotation_about_axis({0, 0, 1}, angle),
                                                    {sx, sy, sz}};
                        const LocalTransform child{
                            {2, -3, 7}, rotation_about_axis({0, 0, 1}, 25), {1, 1, 1}};
                        const std::map<std::uint64_t, TransformNode> nodes{{1, {parent, 0, true}},
                                                                           {2, {child, 1, true}}};
                        const auto original = nodes;
                        const auto evaluated = evaluate_transforms(nodes);
                        const auto& world = evaluated.at(1).affine;
                        const PlanarMapping mapping(world);
                        const double tolerance = std::max(
                            1e-9, 32 * std::numeric_limits<double>::epsilon() * std::abs(offset));
                        for (const std::array<double, 2> local :
                             {std::array{0., 0.}, std::array{2.5, -3.75}, std::array{-100., 64.}}) {
                            const auto point = world.point({local[0], local[1], 0});
                            close(mapping.local_point({point[0], point[1]}), local, tolerance);
                            const auto vector = world.vector({local[0], local[1], 0});
                            close(mapping.local_vector({vector[0], vector[1]}), local, 1e-10);
                            if (sz != 0) {
                                const auto general = inverse(world).point(point);
                                close(mapping.local_point({point[0], point[1]}),
                                      {general[0], general[1]}, tolerance);
                            }
                        }
                        const auto position = evaluated.at(2).affine.point({0, 0, 0});
                        close(mapping.local_point({position[0], position[1]}), {2, -3}, tolerance);
                        if (sz == 0)
                            rejects([&] { (void)inverse(world); });
                        require(nodes == original, "Derived XY conversion changed authored nodes");
                        ++checked;
                    }
    std::cout << checked << " existing signed/zero-depth/far parent transforms checked\n";
}
void operation_boundaries() {
    AffineTransform shear;
    shear.m[1] = .5;
    const auto point = shear.point({2, 3, 0});
    close(PlanarMapping(shear).local_point({point[0], point[1]}), {2, 3}, 1e-12);
    // Inverting the plane does not admit shear as a native collider transform.
    rejects([&] { (void)decompose(shear); });
    LocalTransform tilted;
    tilted.rotation = rotation_about_axis({1, 0, 0}, 25);
    rejects([&] { (void)PlanarMapping(affine_transform(tilted)); });
    for (unsigned index : {2u, 6u, 8u, 9u}) {
        AffineTransform coupling;
        coupling.m[index] = .01;
        rejects([&] { (void)PlanarMapping(coupling); });
    }
    AffineTransform huge_coupling;
    huge_coupling.m[0] = huge_coupling.m[4] = huge_coupling.m[8] = 1.7e308;
    rejects([&] { (void)PlanarMapping(huge_coupling); });
    for (unsigned index : {0u, 3u, 7u, 10u, 11u}) {
        AffineTransform invalid;
        invalid.m[index] = std::numeric_limits<double>::infinity();
        rejects([&] { (void)PlanarMapping(invalid); });
    }
    AffineTransform collapsed;
    collapsed.m[0] = 0;
    rejects([&] { (void)PlanarMapping(collapsed); });
    AffineTransform imbalanced;
    imbalanced.m[0] = 1e-12;
    rejects([&] { (void)PlanarMapping(imbalanced); });
    imbalanced.m[0] = 1e-6;
    close(PlanarMapping(imbalanced).local_point({1e-6, 2}), {1, 2}, 1e-10);
    // A uniform scale is well-conditioned independently of its magnitude.
    for (double scale : {1e-100, 1e100}) {
        AffineTransform uniform;
        uniform.m[0] = uniform.m[5] = scale;
        uniform.m[10] = 0;
        close(PlanarMapping(uniform).local_vector({2 * scale, -3 * scale}), {2, -3}, 1e-10);
    }
    AffineTransform unrepresentable;
    unrepresentable.m[0] = unrepresentable.m[5] = 1e-320;
    rejects([&] { (void)PlanarMapping(unrepresentable); });
    const PlanarMapping identity(AffineTransform{});
    const double nan = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { (void)identity.local_point({nan, 0}); });
    rejects([&] { (void)identity.local_vector({0, nan}); });
    AffineTransform far;
    far.m[3] = 1e308;
    const PlanarMapping far_mapping(far);
    close(far_mapping.local_point({1e308, 2}), {0, 2}, 1e-12);
    rejects([&] { (void)far_mapping.local_point({-1e308, 0}); });
    AffineTransform tiny;
    tiny.m[0] = tiny.m[5] = 1e-100;
    const PlanarMapping tiny_mapping(tiny);
    rejects([&] { (void)tiny_mapping.local_vector({1e300, 0}); });
}
} // namespace
int main() {
    try {
        forward_cases();
        operation_boundaries();
        std::cout << "Private XY mapping checks passed; body/shape admission remains separate\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
