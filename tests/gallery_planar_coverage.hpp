#pragma once
#include "gallery_game_capture.hpp"
#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <stdexcept>

namespace forge::test_gallery_capture {
struct PlanarSilhouette {
    std::vector<std::vector<std::array<double, 2>>> loops;
    std::array<double, 2> position{};
    // Independent ideal ellipse radii and proportional opening, when present.
    std::optional<std::array<double, 3>> ellipse{};
};
inline Json planar_coverage(const test_gallery_capture::Pixels& pixels,
                            std::span<const PlanarSilhouette> shapes, double height) {
    const auto background = pixels.front();
    unsigned interior = 0, exterior = 0, openings = 0;
    // Independent even/odd contour test; never inspect generated triangles.
    for (unsigned y = 0; y < 360; ++y)
        for (unsigned x = 0; x < 640; ++x) {
            const std::array<double, 2> world{(double(x) + .5 - 320) * (height / 360),
                                              (180 - double(y) - .5) * (height / 360)};
            bool inside = false, near = false;
            bool in_opening = false;
            for (const auto& shape : shapes) {
                const std::array<double, 2> p{world[0] - shape.position[0],
                                              world[1] - shape.position[1]};
                if (shape.ellipse) {
                    const auto [rx, ry, opening] = *shape.ellipse;
                    const auto radius = std::hypot(p[0] / rx, p[1] / ry);
                    const auto minimum_radius = std::min(rx, ry);
                    near |= minimum_radius * std::abs(radius - 1) < .04;
                    if (opening > 0)
                        near |= minimum_radius * std::abs(radius - opening) < .04;
                    inside |= radius < 1 && (opening == 0 || radius > opening);
                    in_opening |= opening > 0 && radius < opening;
                    continue;
                }
                bool filled = false, outer = false;
                for (unsigned loop = 0; loop < shape.loops.size(); ++loop) {
                    const auto& contour = shape.loops[loop];
                    bool contained = false;
                    for (unsigned i = 0; i < contour.size(); ++i) {
                        const auto& a = contour[i];
                        const auto& b = contour[(i + 1) % contour.size()];
                        const double dx = b[0] - a[0], dy = b[1] - a[1];
                        const double t = std::clamp(((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) /
                                                        (dx * dx + dy * dy),
                                                    0., 1.);
                        near |= std::hypot(p[0] - a[0] - t * dx, p[1] - a[1] - t * dy) < .04;
                        if ((a[1] > p[1]) != (b[1] > p[1]) && p[0] < a[0] + (p[1] - a[1]) * dx / dy)
                            contained = !contained;
                    }
                    if (loop == 0)
                        outer = contained;
                    filled ^= contained;
                }
                in_opening |= outer && !filled;
                inside |= filled;
            }
            if (near)
                continue;
            if (in_opening)
                ++openings;
            if ((pixels[y * 640 + x] != background) != inside)
                throw std::runtime_error("Game differs from the independent outer/hole silhouette");
            if (inside)
                ++interior;
            else
                ++exterior;
        }
    if (interior <= 20000 || exterior <= 150000)
        throw std::runtime_error("Planar oracle did not cover enough pixels");
    return {
        {"interior_pixels", interior}, {"exterior_pixels", exterior}, {"opening_pixels", openings}};
}
} // namespace forge::test_gallery_capture
