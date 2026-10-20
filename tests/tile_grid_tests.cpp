#include "tile_grid.hpp"
#include <iostream>
using namespace forge;
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void reject(F&& action) {
    try {
        action();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("Expected invalid Tile Map coordinate rejection");
}
void verify(TileCellAddress cell) {
    const auto address = tile_chunk_cell(cell);
    require(address.chunk.x == std::floor(double(cell.x) / 32) &&
                address.chunk.y == std::floor(double(cell.y) / 32),
            "Negative Tile Map partition differs from independent floor oracle");
    require(address.index < tile_chunk_cells && tile_cell_address(address) == cell,
            "Tile Map partition did not preserve exact cell address");
}
} // namespace
int main() {
    try {
        for (std::int32_t n = -1000000; n <= 1000000; ++n)
            verify({n, -n});
        for (std::int64_t delta = 0; delta < 2048; ++delta) {
            verify({std::int32_t(INT32_MIN + delta), std::int32_t(INT32_MAX - delta)});
            verify({std::int32_t(INT32_MAX - delta), std::int32_t(INT32_MIN + delta)});
        }
        require(tile_chunk_cell({-1, -1}) == TileChunkCell{{-1, -1}, 1023} &&
                    tile_chunk_cell({-32, -32}) == TileChunkCell{{-1, -1}, 0} &&
                    tile_chunk_cell({-33, 32}) == TileChunkCell{{-2, 1}, 31},
                "Tile Map seam examples differ");
        require(tile_cell_at({-.0001, -2.0}, {1, 2}) == TileCellAddress{-1, -1} &&
                    tile_cell_at({32., 64.}, {1, 2}) == TileCellAddress{32, 32} &&
                    tile_cell_at({16777216.5, 16777217.5}, {1, 1}) ==
                        TileCellAddress{16777216, 16777217},
                "Tile Map local XY half-open/double-precision placement differs");
        require(tile_cell_at({double(INT32_MIN), double(INT32_MAX) + .5}, {1, 1}) ==
                    TileCellAddress{INT32_MIN, INT32_MAX},
                "Tile Map local coordinate limits differ");
        reject([] { (void)tile_cell_address({{INT32_MIN, 0}, 0}); });
        reject([] { (void)tile_cell_address({{INT32_MAX, 0}, 0}); });
        reject([] { (void)tile_cell_address({{0, INT32_MIN}, 0}); });
        reject([] { (void)tile_cell_address({{0, INT32_MAX}, 0}); });
        reject([] { (void)tile_cell_address({{}, 1024}); });
        for (const auto bad : {0., -1., std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN()}) {
            reject([&] { (void)tile_cell_at({0, 0}, {bad, 1}); });
            reject([&] { (void)tile_cell_at({0, 0}, {1, bad}); });
        }
        for (const auto bad :
             {double(INT32_MIN) - 1., double(INT32_MAX) + 1.,
              std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
            reject([&] { (void)tile_cell_at({bad, 0}, {1, 1}); });
            reject([&] { (void)tile_cell_at({0, bad}, {1, 1}); });
        }
        reject([] { (void)tile_cell_at({1, 1}, {std::numeric_limits<double>::min(), 1}); });
        std::cout
            << "Tile Map local XY addressing, negative seams, signed limits and precision passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
