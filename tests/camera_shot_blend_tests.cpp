#include "camera_shot_blend.hpp"
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
void near(double a, double b) {
    check(std::abs(a - b) < 1e-6, "Camera Shot view/lens interpolation differs from the oracle");
}
template <class Function> void rejects(Function&& operation) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Unsupported Camera Shot blend was accepted");
}
LocalRotation yaw(double degrees) {
    const double half = degrees * std::numbers::pi / 360;
    return {0, float(std::sin(half)), 0, float(std::cos(half))};
}
} // namespace
int main() {
    try {
        RenderCamera a{EntityId::generate(), {}, {}}, b{EntityId::generate(), {}, {}};
        a.camera.enabled = false;
        a.camera.near_plane = .1;
        a.camera.far_plane = 100;
        b.camera.near_plane = .5;
        b.camera.far_plane = 1000;
        b.camera.vertical_fov = std::numbers::pi / 2;
        a.world = affine_transform({{-10, 2, -4}, yaw(0), {2, 3, 4}});
        b.world = affine_transform({{10, 4, 6}, yaw(90), {5, 6, 7}});
        const auto first = blend_camera_shot(a, b, 0), last = blend_camera_shot(a, b, 1);
        check(first.entity == b.entity && first.world == a.world && first.camera.enabled &&
                  first.camera.vertical_fov == a.camera.vertical_fov && last.world == b.world &&
                  last.camera == b.camera,
              "Camera Shot endpoints changed identity or composition intent");
        const auto middle = blend_camera_shot(a, b, .5);
        const auto view = camera_view(middle.camera, middle.world, 800, 600);
        near(view.position[0], 0);
        near(view.position[1], 3);
        near(view.position[2], 1);
        near(view.forward[0], std::sqrt(.5));
        near(view.forward[2], std::sqrt(.5));
        near(middle.camera.near_plane, .3);
        near(middle.camera.far_plane, 550);
        near(middle.camera.vertical_fov, 5 * std::numbers::pi / 12);
        a.world = affine_transform({{1e12, 0, 0}, yaw(170), {1, 1, 1}});
        b.world = affine_transform({{1e12 + 2, 0, 0}, yaw(-170), {1, 1, 1}});
        const auto around = blend_camera_shot(a, b, .5);
        near(around.world.m[3], 1e12 + 1);
        check(camera_view(around.camera, around.world, 800, 600).forward[2] < -.999,
              "Camera Shot orientation took the long quaternion path");
        const auto before_a = a.world, before_b = b.world;
        for (auto member : {&Camera::aspect, &Camera::viewport_x}) {
            auto invalid = b;
            invalid.camera.*member = .25;
            rejects([&] { blend_camera_shot(a, invalid, .5); });
        }
        auto invalid = b;
        invalid.camera.layers = 1;
        rejects([&] { blend_camera_shot(a, invalid, .5); });
        invalid = b;
        invalid.camera.render_texture = {AssetId::generate()};
        rejects([&] { blend_camera_shot(a, invalid, .5); });
        invalid = b;
        invalid.world.m[0] = -invalid.world.m[0];
        rejects([&] { blend_camera_shot(a, invalid, .5); });
        for (const auto weight : {-1., 1.01, std::numeric_limits<double>::quiet_NaN()})
            rejects([&] { blend_camera_shot(a, b, weight); });
        check(a.world == before_a && b.world == before_b, "Rejected blend changed Camera inputs");
        a.camera.projection = b.camera.projection = unsigned(CameraProjection::Orthographic);
        a.camera.orthographic_height = 2;
        b.camera.orthographic_height = 6;
        near(blend_camera_shot(a, b, .5).camera.orthographic_height, 4);
        b.camera.orthographic_width = 2;
        rejects([&] { blend_camera_shot(a, b, .5); });
        std::cout << "Camera Shot pose/lens interpolation, stable identity, endpoint and "
                     "compatibility checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
