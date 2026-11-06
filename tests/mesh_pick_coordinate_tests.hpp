#pragma once
#include "mesh_pick.hpp"
#include "mesh_uv_surface.hpp"
#include "render_projection.hpp"
#include <cmath>
#include <iostream>
inline void check_mesh_pick_coordinates() {
    using namespace forge;
    const auto check = [](bool value, const char* message) {
        if (!value)
            throw std::runtime_error(message);
    };
    MeshPart part;
    part.vertices = 3;
    part.indices = {0, 1, 2};
    part.streams = {{"POSITION", 3, std::vector<float>{-1, -1, 2, 1, -1, 4, 0, 1, 3}}};
    MeshInstancePose pose;
    MeshPartPose part_pose;
    Camera lens;
    lens.near_plane = .1;
    lens.far_plane = 10;
    const Double3 expected{.2, .3, .5};
    const Double3 point{.1, 0, 3.1};
    const auto probe = [&](const CameraView& view, Double3 relative_point, Double3 weights) {
        auto relative_view = view;
        relative_view.position = {};
        const auto pixel = project_render_point(relative_view, relative_point);
        check(bool(pixel), "Analytical coordinate fixture is outside the clip range");
        MeshPickBudget detail_budget, depth_budget;
        const auto hit = pick_mesh_part_hit(part, part_pose, pose, view, (*pixel)[0], (*pixel)[1],
                                            detail_budget);
        const auto depth =
            pick_mesh_part(part, part_pose, pose, view, (*pixel)[0], (*pixel)[1], depth_budget);
        check(hit && hit->triangle && hit->triangle->primitive == 0 && depth == hit->depth,
              "Detailed triangle selection differs from existing depth admission");
        for (unsigned axis = 0; axis < 3; ++axis)
            check(std::abs(hit->triangle->barycentric[axis] - weights[axis]) < 2e-10,
                  "Perspective/clipped triangle lost original vertex coordinates");
        check(detail_budget.remaining == depth_budget.remaining,
              "Detailed coordinates changed the existing geometry work budget");
    };
    for (const auto projection : {CameraProjection::Perspective, CameraProjection::Orthographic})
        for (unsigned flips = 0; flips < 4; ++flips) {
            lens.projection = unsigned(projection);
            lens.flip_x = (flips & 1) != 0;
            lens.flip_y = (flips & 2) != 0;
            probe(camera_view(lens, {}, 240, 180), point, expected);
            part.indices.clear();
            probe(camera_view(lens, {}, 240, 180), point, expected);
            part.indices = {0, 1, 2};
            AffineTransform camera;
            camera.m[3] = pose.world.m[3] = 1e12;
            camera.m[7] = pose.world.m[7] = -1e12;
            probe(camera_view(lens, camera, 240, 180), point, expected);
            pose.world = {};
        }
    lens = {};
    lens.near_plane = .1;
    lens.far_plane = 10;
    // Independent object-space barycentrics survive homogeneous eye/near clipping.
    part.streams[0].values = std::vector<float>{-1, -1, 2, 1, -1, 2, 0, 1, -1};
    probe(camera_view(lens, {}, 240, 180), {0, -.6, 1.4}, {.4, .4, .2});
    part.streams[0].values = std::vector<float>{-1, -1, .05f, 1, -1, 2, 0, 1, 2};
    probe(camera_view(lens, {}, 240, 180), {0, 0, .25 * double(.05f) + 1.5}, {.25, .25, .5});
    part.streams[0].values = std::vector<float>{-1, -1, 2, 1, -1, 4, 0, 1, 3};
    part.morph_targets = {
        {{"POSITION", 3, std::vector<float>{.25f, 0, 0, .25f, 0, 0, .25f, 0, 0}}}};
    pose.morph_weights = {2};
    pose.skinned = true;
    part.streams.push_back({"JOINTS_0", 4, std::vector<std::uint32_t>(12)});
    part.streams.push_back(
        {"WEIGHTS_0", 4, std::vector<float>{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}});
    part_pose.skin = SkinPose{};
    part_pose.skin->palette = {AffineTransform{}};
    part_pose.skin->palette[0].m[3] = 1;
    part_pose.skin->palette[0].m[11] = 2;
    pose.world.m[3] = 999; // The same retained palette owns world placement.
    probe(camera_view(lens, {}, 240, 180), {1.6, 0, 5.1}, expected);
    part_pose.skin->palette[0].m[0] = -1;
    probe(camera_view(lens, {}, 240, 180), {.4, 0, 5.1}, expected);
    part.streams.push_back({"TEXCOORD_0", 2, std::vector<float>{0, 0, 1, 0, 0, 1}});
    MeshPickBudget budget;
    const MeshUvTriangle triangle(part, 0, budget);
    const auto uv = triangle.image(expected);
    check(std::abs(uv[0] - .3) < 1e-12 && std::abs(uv[1] - .5) < 1e-12,
          "Authored UV0 differs from original vertex weights");
    const auto inverse = triangle.coordinates(uv);
    check(bool(inverse), "Nondegenerate UV0 lost its inverse");
    for (unsigned axis = 0; axis < 3; ++axis)
        check(std::abs((*inverse)[axis] - expected[axis]) < 1e-12,
              "UV0 inverse differs from analytical barycentrics");
    const MeshPickVertices vertices(part, part_pose, pose);
    const auto relative = triangle.relative(vertices, {}, budget);
    const auto evaluated = mesh_triangle_point(relative, expected);
    for (unsigned axis = 0; axis < 3; ++axis)
        check(std::abs(evaluated[axis] - Double3{.4, 0, 5.1}[axis]) < 1e-12,
              "Reverse UV projection did not reuse morph and skin placement");
    auto degenerate = triangle;
    degenerate.uv[2] = degenerate.uv[1];
    check(!degenerate.coordinates(uv), "Degenerate UV0 supplied an inverse");
    MeshPickBudget insufficient{2};
    bool rejected = false;
    try {
        (void)MeshUvTriangle(part, 0, insufficient);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    check(rejected && insufficient.remaining == 0, "UV lookup bypassed its shared work budget");
    std::cout << "Original triangle coordinates: "
                 "perspective/orthographic/flips/large-world/clipping/skin/morph PASS\n";
}
