#pragma once
#include "decal_clusters.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>
inline void test_decal_cluster_selection() {
    using namespace forge;
    const auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    const auto rejects = [&](auto action) {
        bool failed = false;
        try {
            action();
        } catch (const std::exception&) {
            failed = true;
        }
        check(failed, "Invalid projector grid input accepted");
    };
    std::vector<DecalTransformRows> rows(decal_scene_limit);
    for (unsigned i = 0; i < rows.size(); ++i) {
        const float x = (int(i % 5) - 2) * .25f, y = (int((i / 5) % 5) - 2) * .25f,
                    z = (int((i / 25) % 3) - 1) * .25f;
        if (i < 16)
            rows[i] = {{{.5f, 0, 0, 0}, {0, .5f, 0, 0}, {0, 0, .5f, 0}}};
        else {
            const float sx = i % 2 ? -4.f : 8.f, shear = (int(i % 3) - 1) * 1.5f;
            rows[i] = {{{sx, shear, 0, -sx * x - shear * y}, {0, 4, 0, -4 * y}, {0, 0, 2, -2 * z}}};
        }
    }
    const RenderBounds bounds{{-1, -1, -1}, {1, 1, 1}};
    const auto grid = prepare_decal_clusters(rows, bounds, {});
    check(grid.has_value(), "Ordinary receiver has no spatial grid");
    check(grid->indices.size() < decal_cluster_cells * rows.size() / 2,
          "Localized projectors did not reduce the spatial candidate list");
    for (const auto& range : grid->ranges) {
        check(range[0] <= grid->indices.size() && range[1] <= grid->indices.size() - range[0],
              "Projector cell points outside its index payload");
        const auto begin = grid->indices.begin() + range[0], end = begin + range[1];
        check(std::is_sorted(begin, end) && std::adjacent_find(begin, end) == end &&
                  std::all_of(begin, end, [&](unsigned i) { return i < rows.size(); }),
              "Projector cell changed source order or duplicated indices");
    }
    const auto cell_for = [&](const std::array<float, 3>& p) {
        std::array<unsigned, 3> coord{};
        for (unsigned a = 0; a < 3; ++a) {
            volatile float scaled = p[a] * grid->controls[2][a];
            volatile float anchor = grid->controls[0][a] * grid->controls[2][a];
            coord[a] = unsigned(std::clamp(std::floor(float(scaled - anchor)), 0.f, 3.f));
        }
        return coord[0] + decal_cluster_side * (coord[1] + decal_cluster_side * coord[2]);
    };
    const auto verify = [&](const std::array<float, 3>& p) {
        for (unsigned a = 0; a < 3; ++a)
            if (p[a] < grid->controls[0][a] || p[a] > grid->controls[1][a])
                return;
        const auto range = grid->ranges[cell_for(p)];
        const auto begin = grid->indices.begin() + range[0], end = begin + range[1];
        for (unsigned i = 0; i < rows.size(); ++i) {
            bool possibly_inside = true, invalid = false;
            for (const auto& row : rows[i]) {
                double exact = row[3];
                volatile float accumulated = row[3];
                float fused = row[3];
                for (unsigned a = 0; a < 3; ++a) {
                    exact += double(row[a]) * p[a];
                    volatile float product = row[a] * p[a];
                    accumulated = float(accumulated + product);
                    fused = std::fma(row[a], p[a], fused);
                }
                possibly_inside = possibly_inside &&
                                  (std::abs(exact) <= .5 || std::abs(float(accumulated)) <= .5f ||
                                   std::abs(fused) <= .5f);
                invalid = invalid || !std::isfinite(float(accumulated)) || !std::isfinite(fused);
            }
            if (possibly_inside || invalid)
                check(std::binary_search(begin, end, i),
                      "Conservative grid removed a possible GPU projector hit or diagnostic");
        }
    };
    std::mt19937 random(195);
    std::uniform_real_distribution<float> position(-1, 1);
    for (unsigned i = 0; i < 4000; ++i)
        verify({position(random), position(random), position(random)});
    for (float edge : {-1.f, -.5f, 0.f, .5f, 1.f})
        for (float x : {std::nextafter(edge, -std::numeric_limits<float>::infinity()), edge,
                        std::nextafter(edge, std::numeric_limits<float>::infinity())})
            for (float y : {-.5f, 0.f, .5f})
                verify({x, y, .25f});
    const Double3 origin{std::ldexp(1., 40), -std::ldexp(1., 40), 0};
    const RenderBounds distant{{origin[0] - 1, origin[1] - 1, -1},
                               {origin[0] + 1, origin[1] + 1, 1}};
    const auto relocated = prepare_decal_clusters(rows, distant, origin);
    check(relocated && relocated->controls == grid->controls && relocated->ranges == grid->ranges &&
              relocated->indices == grid->indices,
          "Camera-relative grid changed at a large absolute origin");
    auto flat = bounds;
    flat.minimum[1] = flat.maximum[1] = 0;
    const auto flat_grid = prepare_decal_clusters(rows, flat, {});
    check(flat_grid && flat_grid->controls[2][1] == 0,
          "Flat receiver axis must use one spatial cell");
    const auto poor = prepare_decal_clusters(
        rows, {{1e8, -1, -1}, {std::nextafter(1e8f, std::numeric_limits<float>::infinity()), 1, 1}},
        {});
    check(poor && poor->controls[2][0] == 0, "Unresolvable receiver axis did not collapse safely");
    check(!prepare_decal_clusters({}, bounds, {}) &&
              !prepare_decal_clusters(rows, {{0, 0, 0}, {0, 0, 0}}, {}) &&
              !prepare_decal_clusters(rows, {{-1, -1, -1}, {1e100, 1, 1}}, {}),
          "Missing or unrepresentable grid must retain complete-list fallback");
    auto invalid = rows;
    invalid.push_back(rows.front());
    rejects([&] { prepare_decal_clusters(invalid, bounds, {}); });
    invalid = rows;
    invalid[0][0][0] = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { prepare_decal_clusters(invalid, bounds, {}); });
    auto overflow = rows;
    overflow.back()[0] = {std::numeric_limits<float>::max(), 0, 0,
                          std::numeric_limits<float>::max()};
    overflow.back()[1] = {0, 0, 0, 2}; // Outside another plane must not hide a potential overflow.
    const auto overflow_grid = prepare_decal_clusters(overflow, bounds, {});
    check(overflow_grid.has_value(), "Overflow diagnostic candidate lost its grid");
    for (const auto& range : overflow_grid->ranges)
        check(std::binary_search(overflow_grid->indices.begin() + range[0],
                                 overflow_grid->indices.begin() + range[0] + range[1],
                                 unsigned(rows.size() - 1)),
              "Spatial rejection hid a potential nonfinite projector diagnostic");
}
