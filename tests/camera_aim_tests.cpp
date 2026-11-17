#include "camera_aim.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>

using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(double actual, double expected) {
    check(std::isfinite(actual) && std::abs(actual - expected) <= 1e-6,
          "Camera Aim differs from the independent direction oracle");
}
void near(Double3 actual, Double3 expected) {
    for (unsigned i = 0; i < 3; ++i)
        near(actual[i], expected[i]);
}
template <class Function> void rejects(Function&& operation) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Invalid Camera Aim input was accepted");
}
} // namespace
int main() {
    try {
        for (const auto basis : {ViewBasis::ForgePositiveZ, ViewBasis::GltfNegativeZ}) {
            Camera camera;
            camera.basis = unsigned(basis);
            camera.enabled = false;
            const double sign = basis == ViewBasis::ForgePositiveZ ? 1 : -1;
            const auto source = affine_transform({{10, 20, 30}, {}, {2, 3, 4}});
            for (double weight : {0., .25, .5, 1.}) {
                const auto aimed =
                    detail::aim_camera_transform(camera, source, {20, 20, 30}, weight);
                const auto view = camera_view(camera, aimed, 800, 600);
                const auto angle = weight * std::numbers::pi / 2;
                near(view.forward, {std::sin(angle), 0, sign * std::cos(angle)});
                near(view.position, {10, 20, 30});
                near(view.up, {0, 1, 0});
            }
            for (const auto direction :
                 {Double3{0, 1, 0}, Double3{0, -1, 0}, Double3{0, 0, -sign}, Double3{1, 0, 0}}) {
                Double3 target{10, 20, 30};
                for (unsigned i = 0; i < 3; ++i)
                    target[i] += direction[i] * 10;
                near(camera_view(camera, detail::aim_camera_transform(camera, source, target, 1),
                                 800, 600)
                         .forward,
                     direction);
            }
            check(source == affine_transform({{10, 20, 30}, {}, {2, 3, 4}}),
                  "Camera Aim changed the source node transform");
            near(detail::aim_camera_transform(camera, source, {10, 20, 30}, 0).point({0, 0, 0}),
                 {10, 20, 30});
            rejects([&] { detail::aim_camera_transform(camera, source, {10, 20, 30}, 1); });
            for (double weight : {-1., 1.01, std::numeric_limits<double>::infinity(),
                                  std::numeric_limits<double>::quiet_NaN()})
                rejects(
                    [&] { detail::aim_camera_transform(camera, source, {20, 20, 30}, weight); });
            for (double bad : {std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN()})
                rejects([&] { detail::aim_camera_transform(camera, source, {bad, 20, 30}, 1); });
        }
        Camera camera;
        const auto rolled = affine_transform({{}, rotation_from_euler({0, 0, 90}), {2, 3, 4}});
        const auto turned = camera_view(
            camera, detail::aim_camera_transform(camera, rolled, {10, 0, 0}, 1), 800, 600);
        near(turned.forward, {1, 0, 0});
        near(turned.right, {0, 1, 0});
        near(turned.up, {0, 0, 1});
        const auto opposite = camera_view(
            camera, detail::aim_camera_transform(camera, rolled, {0, 0, -10}, 1), 800, 600);
        near(opposite.forward, {0, 0, -1});
        near(opposite.up, {-1, 0, 0});
        const auto far = affine_transform({{1e9, -2e9, 3e9}, {}, {1, 1, 1}});
        const auto far_view = camera_view(
            camera, detail::aim_camera_transform(camera, far, {1e9 + 10, -2e9, 3e9}, 1), 800, 600);
        near(far_view.position, {1e9, -2e9, 3e9});
        near(far_view.forward, {1, 0, 0});
        near(camera_view(camera, detail::aim_camera_transform(camera, {}, {1e-300, 0, 0}, 1), 800,
                         600)
                 .forward,
             {1, 0, 0});
        for (auto invalid :
             {affine_transform({{}, {}, {-1, 1, 1}}), affine_transform({{}, {}, {0, 1, 1}})})
            rejects([&] { detail::aim_camera_transform(camera, invalid, {10, 0, 0}, 1); });
        auto shear = AffineTransform{};
        shear.m[1] = .5;
        rejects([&] { detail::aim_camera_transform(camera, shear, {10, 0, 0}, 1); });
        camera.projection = unsigned(CameraProjection::Orthographic);
        near(camera_view(camera, detail::aim_camera_transform(camera, {}, {10, 0, 0}, 1), 800, 600)
                 .forward,
             {1, 0, 0});
        camera.basis = 2;
        rejects([&] { detail::aim_camera_transform(camera, {}, {10, 0, 0}, 1); });
        std::cout << "Camera Aim basis/weighted direction/roll/antiparallel/large-origin and "
                     "invalid-frame math PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
