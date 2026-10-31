#include "tile_neighbors.hpp"
#include "tile_neighbors_source_tests.hpp"
#include <iostream>
#include <map>
#include <vector>
using namespace forge;
using namespace forge::tile_detail;
using Rule = TileNeighborRule;
void check(bool condition) {
    if (!condition)
        throw std::runtime_error("Independent neighbor oracle differs");
}
template <class F> void reject(F&& f) {
    try {
        f();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("Invalid neighbor input accepted");
}
int main() {
    test_tile_neighbors_source::exercise();
    constexpr unsigned permutation[8][8] = {{0, 1, 2, 3, 4, 5, 6, 7}, {0, 7, 6, 5, 4, 3, 2, 1},
                                            {4, 3, 2, 1, 0, 7, 6, 5}, {4, 5, 6, 7, 0, 1, 2, 3},
                                            {6, 5, 4, 3, 2, 1, 0, 7}, {2, 3, 4, 5, 6, 7, 0, 1},
                                            {6, 7, 0, 1, 2, 3, 4, 5}, {2, 1, 0, 7, 6, 5, 4, 3}};
    const std::array rules{Rule{0x07, 0x05, 3}, Rule{0x55, 0x55, 4}, Rule{0x11, 0x11, 5},
                           Rule{0x44, 0x44, 6}};
    const auto table = compile_tile_neighbors(rules);
    unsigned cases = 0;
    for (unsigned mask = 0; mask < 256; ++mask) {
        const bool n = mask & 1, ne = mask & 2, e = mask & 4, s = mask & 16, w = mask & 64;
        const auto expected = n && e && !ne      ? 3
                              : n && e && s && w ? 4
                              : n && s           ? 5
                              : e && w           ? 6
                                                 : 2;
        check(select_tile_neighbor(rules, table, mask, 2) == unsigned(expected));
        for (unsigned flags = 0; flags < 8; ++flags) {
            unsigned expected_mask = 0;
            for (unsigned bit = 0; bit < 8; ++bit)
                if (mask & (1u << bit))
                    expected_mask |= 1u << permutation[flags][bit];
            check(tile_neighbor_local_mask(mask, flags) == expected_mask);
            ++cases;
        }
    }
    const std::array centers{std::pair{-33, -33},
                             std::pair{-32, -1},
                             std::pair{-1, -32},
                             std::pair{31, 31},
                             std::pair{32, 32},
                             std::pair{INT32_MIN, 0},
                             std::pair{INT32_MAX, INT32_MAX}};
    constexpr std::pair<int, int> offsets[] = {{0, 1},  {1, 1},   {1, 0},  {1, -1},
                                               {0, -1}, {-1, -1}, {-1, 0}, {-1, 1}};
    for (auto [x, y] : centers)
        for (unsigned wanted = 0; wanted < 256; ++wanted) {
            std::map<std::pair<int, int>, std::uint64_t> grid;
            unsigned expected = 0, valid = 0;
            for (unsigned bit = 0; bit < 8; ++bit) {
                const auto nx = std::int64_t(x) + offsets[bit].first,
                           ny = std::int64_t(y) + offsets[bit].second;
                if (nx < INT32_MIN || nx > INT32_MAX || ny < INT32_MIN || ny > INT32_MAX)
                    continue;
                ++valid;
                grid[{int(nx), int(ny)}] =
                    wanted & (1u << bit) ? UINT64_MAX - 1 : (bit % 2 ? 9 : 0);
                if (wanted & (1u << bit))
                    expected |= 1u << bit;
            }
            unsigned reads = 0;
            check(tile_neighbor_mask(x, y, UINT64_MAX - 1, [&](int a, int b) {
                      ++reads;
                      return grid.at({a, b});
                  }) == expected);
            check(reads == valid);
            ++cases;
        }
    std::vector<Rule> maximum;
    for (unsigned i = 0; i < 64; ++i)
        maximum.push_back({0x3f, std::uint8_t(i), (1ull << 63) + i});
    const auto large = compile_tile_neighbors(maximum);
    for (unsigned mask = 0; mask < 256; ++mask)
        check(select_tile_neighbor(maximum, large, mask, 2) == (1ull << 63) + (mask % 64));
    check(select_tile_neighbor({}, compile_tile_neighbors({}), 255, UINT64_MAX - 1) ==
          UINT64_MAX - 1);
    check(tile_neighbor_mask(0, 0, 0, [](int, int) -> std::uint64_t {
              throw std::runtime_error("Empty cell sampled neighbors");
          }) == 0);
    reject([] { (void)tile_neighbor_local_mask(255, 8); });
    reject([] { (void)compile_tile_neighbors(std::array{Rule{1, 2, 3}}); });
    reject([] { (void)compile_tile_neighbors(std::array{Rule{1, 1, 0}}); });
    maximum.push_back({0, 0, 3});
    reject([&] { (void)compile_tile_neighbors(maximum); });
    std::cout << cases
              << " independent mask/orientation/signed-boundary cases; "
                 "rules/priority/64rows/high64IDs/invalids PASS\n";
}
