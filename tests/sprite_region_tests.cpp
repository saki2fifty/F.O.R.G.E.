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
} // namespace
int main() {
    try {
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
