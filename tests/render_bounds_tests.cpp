#include "animation_debug_pose_tests.hpp"
#include "mesh_morph.hpp"
#include "mesh_pick_tests.hpp"
#include "model_instance_pose_tests.hpp"
#include "render_bounds.hpp"
#include "render_projection.hpp"
#include "render_sort.hpp"
#include "sky_view.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
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
        check_model_instance_poses();
        check_mesh_picking();
        check_animation_debug_poses();
        const MeshBounds unit{{-1, -1, -1}, {1, 1, 1}};
        {
            std::mt19937 random(47181);
            std::uniform_real_distribution<float> value(-1, 1), positive(0, 1);
            const Double3 origin{1e12, -1e12, 2e12};
            for (unsigned trial = 0; trial < 2000; ++trial) {
                std::array<AffineTransform, 4> joints{}, binds{};
                double largest = 0;
                for (auto& matrix : joints)
                    for (unsigned row = 0; row < 3; ++row) {
                        for (unsigned col = 0; col < 3; ++col) {
                            const auto v = trial % 11 ? double(value(random)) * 1000 : 0;
                            matrix.m[row * 4 + col] = v;
                            largest = std::max(largest, std::abs(v));
                        }
                        matrix.m[row * 4 + 3] = origin[row] + double(value(random)) * 1e5;
                    }
                const auto pose = prepare_skin_pose(joints, binds,
                                                    std::array<std::uint32_t, 4>{0, 1, 2, 3}, unit);
                const auto bounds = skin_bounds_for_camera(pose, origin);
                const std::array<float, 3> point{value(random), value(random), value(random)};
                std::array<float, 4> weights{positive(random), positive(random), positive(random),
                                             positive(random)};
                const float sum = weights[0] + weights[1] + weights[2] + weights[3];
                for (auto& weight : weights)
                    weight /= sum;
                for (unsigned row = 0; row < 3; ++row) {
                    std::array<float, 3> basis{};
                    float translation = 0;
                    for (unsigned joint = 0; joint < 4; ++joint) {
                        for (unsigned col = 0; col < 3; ++col)
                            basis[col] +=
                                weights[joint] *
                                float(largest ? joints[joint].m[row * 4 + col] / largest : 0);
                        translation +=
                            weights[joint] * float(joints[joint].m[row * 4 + 3] - origin[row]);
                    }
                    const float gpu =
                        (basis[0] * point[0] + basis[1] * point[1] + basis[2] * point[2]) *
                            float(largest) +
                        translation;
                    const double world = origin[row] + gpu;
                    check(world >= bounds.minimum[row] && world <= bounds.maximum[row],
                          "Camera-relative float skin blend escaped conservative bounds");
                }
            }
            auto pose = prepare_skin_pose(std::array<AffineTransform, 1>{},
                                          std::array<AffineTransform, 1>{},
                                          std::array<std::uint32_t, 1>{0}, unit);
            rejects([&] { skin_bounds_for_camera(pose, {INFINITY, 0, 0}); });
            pose.palette[0].m[0] = NAN;
            rejects([&] { skin_bounds_for_camera(pose, {}); });
        }
        {
            MeshPart part;
            part.vertices = 2;
            part.bounds = {{0, 0, 0}, {1, 1, 1}};
            part.morph_targets = {{{"POSITION", 3, std::vector<float>{-2, 0, 1, 3, 0, 2}}},
                                  {{"POSITION", 3, std::vector<float>{0, -4, 0, 0, 4, 0}}}};
            const std::array<float, 2> weights{-2, .5f};
            const auto bounds = morph_bounds(part, weights);
            check(bounds.minimum[0] <= -6 && bounds.minimum[0] > -6.001 &&
                      bounds.minimum[1] <= -2 && bounds.minimum[2] <= -4 &&
                      bounds.maximum[0] >= 5 && bounds.maximum[0] < 5.001 &&
                      bounds.maximum[1] >= 3 && bounds.maximum[2] >= -1,
                  "Signed morph interval bounds lost extrema or changed weights");
            rejects([&] { morph_bounds(part, std::array<float, 1>{1}); });
            rejects([&] { morph_bounds(part, std::array<float, 2>{INFINITY, 0}); });
            rejects([&] {
                morph_bounds(part, std::array<float, 2>{std::numeric_limits<float>::max(), 0});
            });
        }

        {
            std::array<AffineTransform, 3> joints{}, inverse_bind{};
            joints[0].m = {-2, 0, 0, 1e12, 0, 3, 0, 4, 0, 0, 0, 8};
            joints[1].m = {1, 0, 0, 1e12 + 10, 0, -1, 0, 0, 0, 0, 2, 0};
            joints[2].m[3] = -1e12; // Not in this draw's palette.
            inverse_bind[0].m[3] = -3;
            const std::array<std::uint32_t, 2> order{1, 0};
            const auto pose = prepare_skin_pose(joints, inverse_bind, order, unit);
            check(pose.palette[0] == joints[1] && pose.palette[1] == joints[0] * inverse_bind[0] &&
                      pose.bounds.minimum[0] == 1e12 + 4 && pose.bounds.maximum[0] == 1e12 + 11,
                  "Skin inverse bind/palette order or unused-joint exclusion changed");
            for (unsigned corner = 0; corner < 8; ++corner) {
                const Double3 p{corner & 1 ? 1. : -1., corner & 2 ? 1. : -1.,
                                corner & 4 ? 1. : -1.};
                const auto a = pose.palette[0].point(p), b = pose.palette[1].point(p);
                for (double weight : {0., .125, .5, 1.})
                    for (unsigned axis = 0; axis < 3; ++axis) {
                        const double value = weight * a[axis] + (1 - weight) * b[axis];
                        check(value >= pose.bounds.minimum[axis] &&
                                  value <= pose.bounds.maximum[axis],
                              "Conservative skin bounds omitted a reflected/singular blend");
                    }
            }
            rejects([&] {
                prepare_skin_pose(joints, inverse_bind, std::array<std::uint32_t, 1>{3}, unit);
            });
            rejects([&] {
                prepare_skin_pose(joints, std::span<const AffineTransform>{}, order, unit);
            });
            joints[0].m[3] = INFINITY;
            rejects([&] { prepare_skin_pose(joints, inverse_bind, order, unit); });
        }

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
        check(bounds_camera_depth(bounds, view) == 5, "Sort depth ignored camera basis");
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
        check(std::abs(bounds_camera_depth({{4, -1, -1}, {6, 1, 1}}, rotated) - 5) < 1e-6,
              "Rotated camera sort depth used world Z");
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
        check(bounds_camera_depth({{1e12 - 1, 1e12 - 1, 1e12 + 4}, {1e12 + 1, 1e12 + 1, 1e12 + 6}},
                                  distant) == 5,
              "Large-origin sorting lost relative depth");
        const auto ortho_sky = sky_ray_matrix(orthographic);
        check(ortho_sky == sky_ray_matrix(distant) && ortho_sky[0] == 0 && ortho_sky[5] == 0 &&
                  ortho_sky[14] == 1 && ortho_sky[15] == 1,
              "Orthographic sky rays depended on world position or screen position");
        Camera sky_camera;
        sky_camera.vertical_fov = std::numbers::pi / 2;
        auto sky = sky_ray_matrix(camera_view(sky_camera, {}, 400, 400));
        sky_camera.infinite_far = true;
        check(sky == sky_ray_matrix(camera_view(sky_camera, affine_transform(pose), 400, 400)) &&
                  sky[0] == 1 && sky[5] == 1 && sky[14] == 1 && sky[15] == 1,
              "Infinite-far or large-origin sky produced invalid ray reconstruction");
        sky_camera.flip_x = true;
        const auto flipped_sky = sky_ray_matrix(camera_view(sky_camera, {}, 400, 400));
        check(flipped_sky[0] == -1 && flipped_sky[5] == 1, "Sky ignored projection reflection");
        auto bad_sky = view;
        bad_sky.projection[0] = 0;
        rejects([&] { sky_ray_matrix(bad_sky); });
        const auto projected = project_render_point(distant, {1e12, 1e12, 1e12 + 5});
        check(projected && (*projected)[0] == 200 && (*projected)[1] == 200,
              "Game overlay projection lost camera-relative placement");
        check(!project_render_point(view, {0, 0, -1}) && !project_render_point(view, {0, 0, 1000}),
              "Game overlay projection admitted points outside depth range");
        const auto first = EntityId::parse("00000000-0000-4000-8000-000000000001");
        const auto second = EntityId::parse("00000000-0000-4000-8000-000000000002");
        RenderSortKey opaque, mask, near, far, tied;
        opaque.entity = second;
        mask.alpha = MaterialAlpha::Mask;
        near.alpha = far.alpha = tied.alpha = MaterialAlpha::Blend;
        near.depth = 2;
        far.depth = tied.depth = 9;
        near.entity = far.entity = second;
        tied.entity = first;
        std::vector<RenderSortKey> queue{near, far, mask, tied, opaque};
        for (const auto& key : queue)
            validate_render_key(key);
        std::sort(queue.begin(), queue.end(), render_key_less);
        check(queue == std::vector<RenderSortKey>{opaque, mask, tied, far, near},
              "Queues ignored opacity/depth/stable identity order");
        std::reverse(queue.begin(), queue.end());
        std::sort(queue.begin(), queue.end(), render_key_less);
        check(queue == std::vector<RenderSortKey>{opaque, mask, tied, far, near},
              "Render order depends on extraction/allocation order");
        near.depth = std::numeric_limits<double>::quiet_NaN();
        rejects([&] { validate_render_key(near); });
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
