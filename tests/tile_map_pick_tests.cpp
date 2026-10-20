// Windows headers define these empty legacy macros. Keep this header usable
// in that include environment on every build platform.
#define near
#define far
#include "tile_map_pick.hpp"
#undef far
#undef near
#include <forge/render_components.hpp>
#include <iostream>
#include <limits>

using namespace forge;
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
Double3 point(const AffineTransform& world, std::array<double, 2> local) {
    Double3 result;
    for (unsigned axis = 0; axis < 3; ++axis)
        result[axis] =
            world.m[axis * 4 + 3] + world.m[axis * 4] * local[0] + world.m[axis * 4 + 1] * local[1];
    return result;
}
CameraView view_at(Double3 center, bool perspective) {
    Camera camera;
    camera.projection =
        unsigned(perspective ? CameraProjection::Perspective : CameraProjection::Orthographic);
    camera.orthographic_height = 10;
    camera.near_plane = .1;
    camera.far_plane = 100;
    AffineTransform position;
    position.m[3] = center[0];
    position.m[7] = center[1];
    position.m[11] = center[2] - 5;
    auto view = camera_view(camera, position, 400, 300);
    view.viewport.x = 17;
    view.viewport.y = 23;
    return view;
}
void projections() {
    const std::array<TileCellAddress, 9> cells{{{-33, -32},
                                                {-32, -1},
                                                {-1, -1},
                                                {0, 0},
                                                {31, 32},
                                                {32, 33},
                                                {1048576, -1048576},
                                                {INT32_MIN, INT32_MAX},
                                                {INT32_MAX, INT32_MIN}}};
    std::array<AffineTransform, 4> transforms{};
    transforms[1].m = {-2, .5, 0, 3, 0, 3, 0, -4, .25, -.5, 1, 2};
    transforms[2].m = {1, .25, 0, 1e12, .5, 2, 0, -1e12, 0, 0, 1, 1e12};
    transforms[3].m[10] = 0; // A valid XY surface with a collapsed unused Z axis.
    const std::array<double, 2> size{.5, 2};
    for (const auto& world : transforms)
        for (const auto cell : cells) {
            const std::array<double, 2> local{(double(cell.x) + .5) * size[0],
                                              (double(cell.y) + .5) * size[1]};
            const auto center = point(world, local);
            for (const bool perspective : {false, true}) {
                const auto view = view_at(center, perspective);
                const auto pixel = project_render_point(view, center);
                check(bool(pixel), "Known cell center did not project");
                const auto hit = pick_tile_grid(size, world, view, (*pixel)[0], (*pixel)[1]);
                check(hit && hit->cell == cell && hit->depth >= 0 && hit->depth <= 1,
                      "Grid picking differs across projection/negative/far/transformed cells");
            }
        }
    const auto view = view_at({0, 0, 0}, false);
    for (const auto local :
         {std::array<double, 2>{-.25, -.25}, {0., 0.}, {.25, .25}, {-.001, .999}, {1., 1.}}) {
        const auto pixel = project_render_point(view, {local[0], local[1], 0});
        check(bool(pixel), "Boundary fixture projection failed");
        const auto hit = pick_tile_grid({1, 1}, {}, view, (*pixel)[0], (*pixel)[1]);
        check(hit && hit->cell == tile_cell_at(local, {1, 1}),
              "Grid picking changed floor addressing at a cell boundary");
    }
}
void rejection() {
    const auto view = view_at({0, 0, 0}, false);
    const double x = view.viewport.x + view.viewport.width * .5;
    const double y = view.viewport.y + view.viewport.height * .5;
    check(!pick_tile_grid({1, 1}, {}, view, -1, y) &&
              !pick_tile_grid({1, 1}, {}, view, view.viewport.x + view.viewport.width, y),
          "Off-viewport grid hit was accepted");
    AffineTransform world;
    world.m[11] = -10;
    check(!pick_tile_grid({1, 1}, world, view, x, y), "Behind-camera plane was selected");
    world.m[11] = 1000;
    check(!pick_tile_grid({1, 1}, world, view, x, y), "Beyond-far plane was selected");
    world.m = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0};
    check(!pick_tile_grid({1, 1}, world, view, x, y), "Parallel grid plane was selected");
    world.m = {1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0};
    check(!pick_tile_grid({1, 1}, world, view, x, y), "Collapsed XY grid was selected");
    world = {};
    world.m[0] = std::numeric_limits<double>::quiet_NaN();
    check(!pick_tile_grid({1, 1}, world, view, x, y) && !pick_tile_grid({0, 1}, {}, view, x, y) &&
              !pick_tile_grid({1, 1}, {}, view, INFINITY, y),
          "Invalid finite grid input was accepted");
    world = {};
    world.m[3] = -double(INT32_MAX) - 100;
    check(!pick_tile_grid({1, 1}, world, view, x, y),
          "Out-of-range signed cell address was accepted");
}
} // namespace
int main() {
    try {
        projections();
        rejection();
        std::cout << "Sparse tile grid picking, transforms, clipping and signed limits passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
