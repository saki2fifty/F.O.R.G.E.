#pragma once
#include "mesh_pick.hpp"
#include <chrono>
#include <iostream>
inline void check_mesh_picking() {
    using namespace forge;
    auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    MeshPart part;
    part.vertices = 3;
    part.indices = {0, 1, 2};
    part.streams = {{"POSITION", 3, std::vector<float>{-1, -1, 0, 1, -1, 0, 0, 1, 0}}};
    MeshInstancePose pose;
    pose.world.m[11] = 2;
    MeshPartPose pp;
    Camera lens;
    lens.near_plane = .1;
    lens.far_plane = 10;
    auto view = camera_view(lens, {}, 200, 200);
    auto hit = [&](double x = 100, double y = 100) {
        MeshPickBudget budget;
        return pick_mesh_part(part, pp, pose, view, x, y, budget);
    };
    const auto first = hit();
    check(first && *first > 0 && *first < 1, "Triangle selection missed admitted geometry");
    check(!hit(0, 0) && !hit(-1, 100), "Selection used bounds rather than actual triangle");
    pose.world.m[0] = -1;
    check(hit() == first, "Reflection changed geometric selection depth");
    pose.world.m[10] = 0;
    check(hit() == first, "Rank-two visible surface became unselectable");
    pose.world.m[0] = pose.world.m[5] = 0;
    check(!hit(), "Collapsed triangle remained selectable as a surface");
    pose.world = {};
    pose.world.m[11] = 2;
    pose.world.m[3] = 1e12;
    AffineTransform camera;
    camera.m[3] = 1e12;
    view = camera_view(lens, camera, 200, 200);
    check(hit() == first, "Large world origin lost mesh selection precision");
    lens.flip_y = true;
    view = camera_view(lens, camera, 200, 200);
    check(hit() == first, "Reflected camera broke mesh selection");
    lens.flip_y = false;
    view = camera_view(lens, {}, 200, 200);
    pose.world.m[3] = 0;
    part.morph_targets = {{{"POSITION", 3, std::vector<float>{2, 0, 0, 2, 0, 0, 2, 0, 0}}}};
    pose.morph_weights = {-1};
    pose.world.m[3] = 2;
    check(hit() == first, "Negative morph was not evaluated before placement transform");
    pose.skinned = true;
    part.streams.push_back({"JOINTS_0", 4, std::vector<std::uint32_t>(12)});
    part.streams.push_back(
        {"WEIGHTS_0", 4, std::vector<float>{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}});
    pp.skin = SkinPose{};
    pp.skin->palette = {pose.world};
    pose.world.m[3] = 999; // Skin is joint-world * bind, never multiplied by this again.
    check(hit() == first, "Skin picking double-applied mesh placement or lost morphing");
    pp.skin->palette[0].m[0] = -1;
    pp.skin->palette[0].m[3] = -2;
    check(hit() == first, "Negative skin blend orientation prevented geometric selection");
    part.morph_targets.clear();
    pose.morph_weights.clear();
    pose.skinned = false;
    pose.world = {};
    // A triangle crossing the eye must be clipped before perspective division.
    part.streams[0].values = std::vector<float>{-1, -1, 2, 1, -1, 2, 0, 1, -1};
    check(bool(hit()), "Eye-plane triangle clipping lost the visible portion");
    part.streams[0].values = std::vector<float>{-1, -1, -2, 1, -1, -2, 0, 1, -2};
    check(!hit(), "Behind-camera geometry was selectable");
    part.topology = MeshTopology::Lines;
    part.indices = {0, 1};
    part.streams[0].values = std::vector<float>{-1, 0, 2, 1, 0, 2, 0, 0, 2};
    check(bool(hit(100, 104)) && !hit(100, 106), "Line selection radius is not in viewport pixels");
    part.topology = MeshTopology::Points;
    part.indices = {2};
    check(bool(hit(104, 100)) && !hit(106, 100),
          "Point selection radius is not in viewport pixels");
    MeshPickBudget empty{0};
    bool rejected = false;
    try {
        (void)pick_mesh_part(part, pp, pose, view, 100, 100, empty);
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Exhausted selection budget returned a partial result");
    part.topology = MeshTopology::Triangles;
    part.streams[0].values = std::vector<float>{-1, -1, 2, 1, -1, 2, 0, 1, 2};
    part.indices.resize(300000);
    for (std::size_t i = 0; i < part.indices.size(); ++i)
        part.indices[i] = i % 3;
    MeshPickBudget measured;
    const auto start = std::chrono::steady_clock::now();
    check(bool(pick_mesh_part(part, pp, pose, view, 100, 100, measured)),
          "Dense picking fixture missed");
    const auto elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::cout << "100000 triangle selection: " << elapsed << " ms; work units "
              << MeshPickBudget{}.remaining - measured.remaining << '\n';
}
