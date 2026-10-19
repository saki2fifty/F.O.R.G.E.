#include "editor/sprite_sheet_layout.hpp"
#include "sprite_draw_values.hpp"
#include "sprite_region.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>
using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void near(double value, double expected) {
    check(std::abs(value - expected) <= 1e-12, "Sprite UV mapping differs");
}
template <class F> void rejects(F&& action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected, "Invalid sprite region/grid was admitted");
}
void image_mapping() {
    const auto large = sprite_image_axis(1, 10, 2, 3, 2, 10);
    near(large.source_low, .2);
    near(large.source_high, .3);
    near(large.target_low, .1);
    near(large.target_high, .15);
    const auto small = sprite_image_axis(1, 10, 2, 3, .2, 10);
    near(small.target_low, .4);
    near(small.target_high, .6);
    const auto partial = sprite_image_axis(2, 16, 0, 0, 1, 24);
    near(partial.repeat, 1.5);
    const auto simple = sprite_image_axis(0, 16, UINT32_MAX, UINT32_MAX, 10000, 1000000);
    near(simple.repeat, 1);
    near(simple.source_low, 0);
    const auto inert = sprite_image_axis(2, 16, UINT32_MAX, UINT32_MAX, 1, 24);
    near(inert.repeat, 1.5);
    std::uint64_t cases = 0;
    for (unsigned pixels = 1; pixels <= 64; ++pixels)
        for (unsigned left = 0; left < pixels; ++left)
            for (double meters : {.0001, .25, 2., 10000.}) {
                const auto right = (pixels - left - 1) / 2;
                const auto axis = sprite_image_axis(1, pixels, left, right, meters, 100);
                const double target = meters * 100;
                // Independent pixel-space invariants: preserved corners at/above
                // natural size, proportional compression below it, no overlap.
                near(axis.source_low * pixels, left);
                near(axis.source_high * pixels, right);
                if (target >= left + right) {
                    near(axis.target_low * target, left);
                    near(axis.target_high * target, right);
                } else {
                    near(axis.target_low + axis.target_high, 1);
                    near(axis.target_low * (left + right), left);
                }
                check(axis.target_low + axis.target_high <= 1 + 1e-12 &&
                          axis.source_low + axis.source_high < 1,
                      "Sliced image overlaps its opposing borders");
                ++cases;
            }
    check(cases == 8320, "Image mapping partition coverage changed");
    rejects([] { sprite_image_axis(3, 16, 0, 0, 1, 100); });
    rejects([] { sprite_image_axis(1, 16, 8, 8, 1, 100); });
    rejects([] { sprite_image_axis(1, 16, UINT32_MAX, 2, 1, 100); });
    rejects([] { sprite_image_axis(2, 16, 0, 0, 10000, 1000000); });
    rejects([] { sprite_image_axis(0, 0, 0, 0, 1, 100); });
    rejects([] { sprite_image_axis(1, 16, 0, 0, 1, 0); });
    rejects([] { sprite_image_axis(1, 16, 0, 0, 1, std::numeric_limits<double>::quiet_NaN()); });
}
void sheet_selection() {
    // Independent pixel partition with outer borders and internal empty gaps.
    for (unsigned y = 0; y < 8; ++y)
        for (unsigned x = 0; x < 12; ++x) {
            const auto frame = sprite_sheet_frame_at(12, 8, {2, 2, 1, 1, 2, 2}, x + .5, y + .5);
            const bool covered = ((x >= 1 && x < 5) || (x >= 7 && x < 11)) &&
                                 ((y >= 1 && y < 3) || (y >= 5 && y < 7));
            check(bool(frame) == covered, "Sheet selection included a margin/gap or lost a cell");
            if (frame)
                check(*frame == (y >= 5 ? 2u : 0u) + (x >= 7 ? 1u : 0u),
                      "Visual sheet selection changed top-left row-major order");
        }
    check(!sprite_sheet_frame_at(12, 8, {2, 2, 1, 1, 2, 2}, 5, 1) &&
              sprite_sheet_frame_at(12, 8, {2, 2, 1, 1, 2, 2}, std::nextafter(5., 0.), 1) == 0 &&
              !sprite_sheet_frame_at(12, 8, {2, 2, 1, 1, 2, 2}, 12, 1),
          "Sheet edges are not half-open");
    check(sprite_sheet_frame_at(65536, 65536, {65536, 65536}, 65535.5, 65535.5) == UINT32_MAX &&
              !sprite_sheet_frame_at(65536, 65537, {65536, 65537}, .5, 65536.5),
          "Large sheet selection wrapped its U32 frame value");
    for (double bad :
         {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -1.})
        check(!sprite_sheet_frame_at(16, 8, {4, 2}, bad, 0),
              "Nonfinite/outside sheet coordinates were admitted");
    rejects([] { (void)sprite_sheet_frame_at(16, 8, {0, 2}, 0, 0); });
    rejects([] { (void)sprite_sheet_frame_at(17, 8, {4, 2}, 0, 0); });

    SpriteSheetView state;
    auto view = sprite_sheet_viewport(1024, 256, 800, 600, state);
    near(view.left, 0);
    near(view.top, 200);
    near(view.width, 800);
    near(view.height, 200);
    check(!view.pixel_at(400, 100) && !view.pixel_at(400, 400),
          "Sheet letterbox or bottom edge became selectable");
    const auto anchor = view.pixel_at(500, 300);
    check(bool(anchor), "Expected sheet pixel missing");
    near((*anchor)[0], 640);
    near((*anchor)[1], 128);
    check(sprite_sheet_zoom_at(state, 1024, 256, 800, 600, 500, 300, std::log(4.) / .15),
          "Sheet zoom did not acquire its image");
    near(state.zoom, 4);
    view = sprite_sheet_viewport(1024, 256, 800, 600, state);
    const auto zoomed = view.pixel_at(500, 300);
    check(bool(zoomed), "Zoom lost the pointer anchor");
    near((*zoomed)[0], (*anchor)[0]);
    near((*zoomed)[1], (*anchor)[1]);
    near(view.width / (1024 * (view.region[2] - view.region[0])),
         view.height / (256 * (view.region[3] - view.region[1])));
    check(sprite_sheet_pan(state, 1024, 256, 800, 600, 100, -50), "Sheet pan failed");
    view = sprite_sheet_viewport(1024, 256, 800, 600, state);
    const auto panned = view.pixel_at(500, 300);
    check(bool(panned), "Pan lost a visible sheet pixel");
    near((*panned)[0], 608);
    near((*panned)[1], 144);
    check(sprite_sheet_pan(state, 1024, 256, 800, 600, 1e300, 1e300),
          "Finite extreme sheet pan failed to clamp");
    view = sprite_sheet_viewport(1024, 256, 800, 600, state);
    near(view.region[0], 0);
    near(view.region[1], 0);
    const auto before = state;
    check(
        !sprite_sheet_pan(state, 1024, 256, 800, 600, std::numeric_limits<double>::infinity(), 0) &&
            state == before &&
            !sprite_sheet_zoom_at(state, 1024, 256, 800, 600, 0, 0,
                                  std::numeric_limits<double>::quiet_NaN()) &&
            state == before,
        "Invalid sheet navigation partially changed the view");
    rejects([] { (void)sprite_sheet_viewport(0, 256, 800, 600, {}); });
    rejects([] { (void)sprite_sheet_viewport(1024, 256, 0, 600, {}); });
    rejects([] { (void)sprite_sheet_viewport(1024, 256, 800, 600, {0}); });
}
} // namespace
int main() {
    try {
        image_mapping();
        sheet_selection();
        check(sprite_grid_region(16, 8, {4, 2}, 0) == SpritePixelRegion{0, 0, 4, 4},
              "First frame differs");
        check(sprite_grid_region(16, 8, {4, 2}, 5) == SpritePixelRegion{4, 4, 4, 4},
              "Top-left row-major frame order differs");
        check(sprite_grid_region(16, 8, {4, 2}, 7) == SpritePixelRegion{12, 4, 4, 4},
              "Last frame differs");
        const SpriteGridLayout padded{3, 2, 2, 1, 1, 2};
        check(sprite_grid_region(24, 14, padded, 4) == SpritePixelRegion{9, 8, 6, 5},
              "Padding or spacing consumed frame pixels");
        rejects([&] { sprite_grid_region(23, 14, padded, 0); });
        rejects([&] { sprite_grid_region(24, 13, padded, 0); });
        rejects([] { sprite_grid_region(0, 8, {}, 0); });
        rejects([] { sprite_grid_region(8, 0, {}, 0); });
        rejects([] { sprite_grid_region(8, 8, {0, 1}, 0); });
        rejects([] { sprite_grid_region(8, 8, {1, 0}, 0); });
        rejects([] { sprite_grid_region(8, 8, {9, 1}, 0); });
        rejects([] { sprite_grid_region(8, 8, {2, 2}, 4); });
        const auto maximum = std::numeric_limits<std::uint32_t>::max();
        rejects([&] { sprite_grid_region(8, 8, {1, 1, maximum}, 0); });
        rejects([&] { sprite_grid_region(8, 8, {maximum, 1, 0, 0, maximum}, 0); });
        rejects([&] { sprite_grid_region(8, 8, {}, UINT64_MAX); });
        check(sprite_grid_region(maximum, 1, {maximum, 1}, maximum - 1) ==
                  SpritePixelRegion{maximum - 1, 0, 1, 1},
              "Wide frame coordinates overflowed");
        rejects([&] { sprite_region_mapping({maximum - 1, 0, 2, 1}, maximum, 1); });
        rejects([] { sprite_region_mapping({}, 8, 8); });
        rejects([] { sprite_region_mapping({0, 0, 1, 1}, 0, 8); });
        rejects([] { sprite_region_mapping({0, 8, 1, 1}, 8, 8); });
        rejects([] { sprite_region_mapping({7, 0, 2, 1}, 8, 8); });
        for (bool flip_x : {false, true})
            for (bool flip_y : {false, true}) {
                const auto map = sprite_region_mapping({4, 4, 4, 4}, 16, 8, flip_x, flip_y);
                near(map.origin[0], flip_x ? .5 : .25);
                near(map.origin[1], flip_y ? 1 : .5);
                near(map.scale[0], flip_x ? -.25 : .25);
                near(map.scale[1], flip_y ? -.5 : .5);
                near(map.sample_minimum[0], 4.5 / 16);
                near(map.sample_maximum[0], 7.5 / 16);
                near(map.sample_minimum[1], 4.5 / 8);
                near(map.sample_maximum[1], 7.5 / 8);
            }
        const auto pixel = sprite_region_mapping({1, 0, 1, 1}, 2, 1, true, true);
        near(pixel.sample_minimum[0], .75);
        near(pixel.sample_maximum[0], .75);
        near(pixel.sample_minimum[1], .5);
        near(pixel.sample_maximum[1], .5);
        SpriteDrawValues draw{sprite_region_mapping({4, 4, 4, 4}, 16, 8, true, false),
                              {1, .5f, .25f, .75f}};
        validate_sprite_draw_values(draw);
        auto invalid_draw = draw;
        invalid_draw.color[3] = -1;
        rejects([&] { validate_sprite_draw_values(invalid_draw); });
        invalid_draw = draw;
        invalid_draw.color[0] = std::numeric_limits<float>::quiet_NaN();
        rejects([&] { validate_sprite_draw_values(invalid_draw); });
        invalid_draw = draw;
        invalid_draw.region.scale[0] = 0;
        rejects([&] { validate_sprite_draw_values(invalid_draw); });
        invalid_draw = draw;
        invalid_draw.region.origin[1] = std::numeric_limits<double>::infinity();
        rejects([&] { validate_sprite_draw_values(invalid_draw); });
        invalid_draw = draw;
        invalid_draw.region.sample_minimum[0] = 0;
        rejects([&] { validate_sprite_draw_values(invalid_draw); });
        invalid_draw = draw;
        invalid_draw.region.sample_maximum[1] = 1.01;
        rejects([&] { validate_sprite_draw_values(invalid_draw); });
        invalid_draw = draw;
        invalid_draw.region.sample_minimum[1] = invalid_draw.region.sample_maximum[1] + .01;
        rejects([&] { validate_sprite_draw_values(invalid_draw); });
        // Independent occupancy oracle: every pixel is covered exactly once by
        // every admitted unpadded partition, with no overlap or out-of-bounds cell.
        unsigned layouts = 0;
        for (unsigned width = 1; width <= 12; ++width)
            for (unsigned height = 1; height <= 12; ++height)
                for (unsigned columns = 1; columns <= width; ++columns)
                    for (unsigned rows = 1; rows <= height; ++rows) {
                        if (width % columns || height % rows)
                            continue;
                        std::vector<unsigned> occupancy(width * height);
                        for (unsigned frame = 0; frame < columns * rows; ++frame) {
                            const auto region =
                                sprite_grid_region(width, height, {columns, rows}, frame);
                            check(region.x + region.width <= width &&
                                      region.y + region.height <= height,
                                  "Grid region exceeds the texture");
                            for (unsigned y = region.y; y < region.y + region.height; ++y)
                                for (unsigned x = region.x; x < region.x + region.width; ++x)
                                    ++occupancy[y * width + x];
                        }
                        for (auto value : occupancy)
                            check(value == 1, "Grid partition contains gaps or overlaps");
                        ++layouts;
                    }
        std::cout << "Sprite region bounds/UV/flips/overflow and " << layouts
                  << " complete pixel partitions PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
