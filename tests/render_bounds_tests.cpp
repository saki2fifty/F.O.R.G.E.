#include "render_bounds.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Invalid render bounds/LOD input accepted");
}
} // namespace
int main() {
    try {
        using namespace forge;
        const MeshBounds unit{{-1, -1, -1}, {1, 1, 1}};
        AffineTransform world;
        world.m = {-2, .5, 0, 3, 0, 0, 0, 7, 0, 0, 3, 5};
        const auto transformed = transform_bounds(unit, world);
        check(transformed == RenderBounds{{.5, 7, 2}, {5.5, 7, 8}},
              "Reflection/shear/collapse produced incorrect affine bounds");
        world = AffineTransform{};
        world.m[11] = 5;
        Camera camera;
        camera.vertical_fov = std::numbers::pi / 2;
        camera.near_plane = 1;
        camera.far_plane = 100;
        const auto view = camera_view(camera, {}, 400, 400);
        auto bounds = transform_bounds(unit, world);
        check(bounds_visible(bounds, view), "Visible box culled");
        check(std::abs(bounds_screen_coverage(bounds, view) - .25f) < 1e-6,
              "Perspective bounds use incorrect projected diameter");
        check(!bounds_visible({{-1, -1, -6}, {1, 1, -4}}, view), "Behind-camera box visible");
        check(!bounds_visible({{-.01, -.01, .1}, {.01, .01, .9}}, view), "Near clipping ignored");
        check(!bounds_visible({{-1, -1, 101}, {1, 1, 102}}, view), "Far clipping ignored");
        check(!bounds_visible({{10, -1, 4}, {12, 1, 6}}, view), "Side clipping ignored");
        check(bounds_visible({{-1000, -1000, -1000}, {1000, 1000, 1000}}, view),
              "Frustum-enclosing bounds incorrectly culled");
        check(bounds_screen_coverage({{-1, -1, -1}, {1, 1, 1}}, view) == 1,
              "Eye-plane crossing did not retain full detail");
        for (auto x : {-1.f, 0.f, 1.f}) {
            world.m[0] = x;
            check(bounds_visible(transform_bounds(unit, world), view),
                  "Mirrored or planar object lost visibility");
        }
        camera.flip_x = camera.flip_y = true;
        const auto flipped = camera_view(camera, {}, 400, 400);
        check(bounds_visible(bounds, flipped) &&
                  bounds_screen_coverage(bounds, flipped) == bounds_screen_coverage(bounds, view),
              "Image flips changed visibility or LOD");
        camera.flip_x = camera.flip_y = false;
        camera.infinite_far = true;
        check(bounds_visible({{-1, -1, 1e9}, {1, 1, 1e9 + 2}}, camera_view(camera, {}, 400, 400)),
              "Infinite camera inherited a finite far cull");
        camera.infinite_far = false;
        camera.basis = std::uint32_t(ViewBasis::GltfNegativeZ);
        const auto gltf = camera_view(camera, {}, 400, 400);
        check(bounds_visible({{-1, -1, -6}, {1, 1, -4}}, gltf) && !bounds_visible(bounds, gltf),
              "Imported camera basis disagrees with culling");
        camera.basis = std::uint32_t(ViewBasis::ForgePositiveZ);
        LocalTransform pose;
        pose.rotation = rotation_from_euler({0, 90, 0});
        const auto rotated = camera_view(camera, affine_transform(pose), 400, 400);
        check(bounds_visible({{4, -1, -1}, {6, 1, 1}}, rotated), "Rotated camera culling failed");
        camera.projection = std::uint32_t(CameraProjection::Orthographic);
        camera.orthographic_height = 4;
        const auto orthographic = camera_view(camera, {}, 400, 400);
        check(bounds_visible(bounds, orthographic) &&
                  std::abs(bounds_screen_coverage(bounds, orthographic) - .5f) < 1e-6,
              "Orthographic coverage is incorrect");
        world = AffineTransform{};
        world.m[11] = 20;
        check(bounds_screen_coverage(transform_bounds(unit, world), orthographic) == .5f,
              "Orthographic LOD changes with depth");
        // Camera-relative projection retains a small nearby box at a large origin.
        pose = {};
        pose.translation = {1e12, 1e12, 1e12};
        const auto distant = camera_view(camera, affine_transform(pose), 400, 400);
        check(bounds_visible({{1e12 - 1, 1e12 - 1, 1e12 + 4}, {1e12 + 1, 1e12 + 1, 1e12 + 6}},
                             distant),
              "Camera-relative culling lost a nearby object at a large world origin");
        MeshData mesh;
        mesh.lods = {{1, {}}, {.25f, {}}, {.1f, {}}};
        check(select_mesh_lod(mesh, .8f) == 0 && select_mesh_lod(mesh, .25f) == 1 &&
                  select_mesh_lod(mesh, .11f) == 1 && select_mesh_lod(mesh, 0) == 2,
              "LOD transition selected the wrong level");
        rejects([&] { select_mesh_lod(mesh, -1); });
        mesh.lods[1].screen_coverage = 1;
        rejects([&] { select_mesh_lod(mesh, .5f); });
        rejects([&] { bounds_visible({{1, 0, 0}, {0, 0, 0}}, view); });
        world.m[0] = std::numeric_limits<double>::infinity();
        rejects([&] { transform_bounds(unit, world); });
        world = AffineTransform{};
        world.m[0] = std::numeric_limits<double>::max();
        rejects([&] { transform_bounds({{-2, -1, -1}, {2, 1, 1}}, world); });
        std::cout << "Affine bounds, camera culling and LOD checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
