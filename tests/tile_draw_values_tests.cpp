#include "cooked_envelope.hpp"
#include "tile_draw_values.hpp"
#include <iostream>

using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void reject(F&& action) {
    bool failed = false;
    try {
        action();
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, "Invalid tile draw values were accepted");
}
void values() {
    const auto id = AssetId::generate(), material = AssetId::generate();
    const auto tile = UINT64_MAX - 1;
    const nlohmann::json metadata{
        {"asset_id", id},
        {"images",
         {{{"id", 17},
           {"name", "Original asymmetric sheet"},
           {"material", material},
           {"grid",
            {{"columns", 3},
             {"rows", 2},
             {"margin_x", 1},
             {"margin_y", 1},
             {"spacing_x", 0},
             {"spacing_y", 0}}}}}},
        {"tiles", {{{"id", tile}, {"name", "Wide scoped key"}, {"image", 17}, {"frame", 4}}}}};
    const auto definitions =
        decode_cooked_tile_set(asset_detail::encode_envelope(metadata, {}, tile_set_cooked_magic,
                                                             tile_set_cooked_metadata_limit),
                               id);
    // Independent corner permutations: TL,TR,BR,BL. Transpose precedes flips.
    const std::array<std::array<unsigned, 4>, 8> expected{{{{0, 1, 2, 3}},
                                                           {{1, 0, 3, 2}},
                                                           {{3, 2, 1, 0}},
                                                           {{2, 3, 0, 1}},
                                                           {{0, 3, 2, 1}},
                                                           {{1, 2, 3, 0}},
                                                           {{3, 0, 1, 2}},
                                                           {{2, 1, 0, 3}}}};
    const std::array<std::array<double, 2>, 4> corners{{{{0, 0}}, {{1, 0}}, {{1, 1}}, {{0, 1}}}};
    const std::array<std::array<double, 2>, 4> samples{{{{5.5 / 14, 6.5 / 12}},
                                                        {{8.5 / 14, 6.5 / 12}},
                                                        {{8.5 / 14, 10.5 / 12}},
                                                        {{5.5 / 14, 10.5 / 12}}}};
    for (unsigned flags = 0; flags < 8; ++flags) {
        const auto draw = tile_draw_values(definitions, tile, flags, 14, 12, {.5f, .75f, 1, .25f});
        check(draw.image == 17 && draw.material.id == material && draw.sprite.color[3] == .25f,
              "Tile draw changed image identity, Material selection or linear tint");
        for (unsigned corner = 0; corner < 4; ++corner) {
            auto uv = corners[corner];
            if (draw.sprite.transpose_uv)
                std::swap(uv[0], uv[1]);
            for (unsigned axis = 0; axis < 2; ++axis) {
                const auto& region = draw.sprite.region;
                const auto value =
                    std::clamp(region.origin[axis] + uv[axis] * region.scale[axis],
                               region.sample_minimum[axis], region.sample_maximum[axis]);
                check(std::abs(value - samples[expected[flags][corner]][axis]) < 1e-14,
                      "Tile UV transform differs from independent asymmetric frame corners");
            }
        }
    }
    reject([&] { tile_draw_values(definitions, 0, 0, 14, 12); });
    reject([&] { tile_draw_values(definitions, tile, 8, 14, 12); });
    reject([&] { tile_draw_values(definitions, tile, 0, 15, 12); });
    reject([&] { tile_draw_values(definitions, tile, 0, 0, 12); });
    reject([&] { tile_draw_values(definitions, tile, 0, 14, 12, {1, 1, 1, 2}); });
    auto sliced = tile_draw_values(definitions, tile, 4, 14, 12).sprite;
    sliced.image.mode = 1;
    reject([&] { validate_sprite_draw_values(sliced); });
}
void transforms() {
    // Reflection, shear, nonuniform scale and far translation, using exact halves.
    const AffineTransform world{{-2, 1, 0, 1e12, 0, 3, 1, -1e12, 1, 0, 2, 4}};
    for (auto cell : {TileCellAddress{-1, -1}, TileCellAddress{INT32_MIN, INT32_MAX},
                      TileCellAddress{INT32_MAX, INT32_MIN}, TileCellAddress{0, 0}}) {
        const auto draw = tile_cell_world(cell, {2, 3}, world);
        for (auto x : {0., 1.})
            for (auto y : {0., 1.}) {
                const auto corner = draw.point({x - .5, y - .5, 0});
                const auto expected =
                    world.point({(double(cell.x) + x) * 2, (double(cell.y) + y) * 3, 0});
                check(corner == expected,
                      "Tile Quad does not cover its double-precision grid cell");
            }
    }
    reject([&] { tile_cell_world({}, {0, 1}, world); });
    reject([&] { tile_cell_world({}, {NAN, 1}, world); });
    auto invalid = world;
    invalid.m[0] = std::numeric_limits<double>::max();
    reject([&] { tile_cell_world({}, {2, 3}, invalid); });
}
} // namespace
int main() {
    try {
        values();
        transforms();
        std::cout << "Tile frame transforms and double-precision cell placement passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
