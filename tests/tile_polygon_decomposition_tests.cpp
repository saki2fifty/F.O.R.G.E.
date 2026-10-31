#include "tile_polygon_decomposition.hpp"
#include <iostream>
#include <numbers>
#include <random>
namespace {
using Point = forge::tile_detail::TilePolygonPoint;
using Contour = std::vector<Point>;
void require(bool value, const char* text) {
    if (!value)
        throw std::runtime_error(text);
}
long double triangle_area(Point a, Point b, Point c) {
    return ((static_cast<long double>(b[0]) - a[0]) * (static_cast<long double>(c[1]) - a[1]) -
            (static_cast<long double>(b[1]) - a[1]) * (static_cast<long double>(c[0]) - a[0])) /
           2;
}
bool inside_polygon(const Contour& p, Point q) {
    bool inside = false;
    for (std::size_t i = 0, j = p.size() - 1; i < p.size(); j = i++)
        if ((p[i][1] > q[1]) != (p[j][1] > q[1]) &&
            q[0] < (p[j][0] - p[i][0]) * (q[1] - p[i][1]) / (p[j][1] - p[i][1]) + p[i][0])
            inside = !inside;
    return inside;
}
bool inside_triangle(Point a, Point b, Point c, Point q) {
    const long double denominator = (static_cast<long double>(b[1]) - c[1]) * (a[0] - c[0]) +
                                    (static_cast<long double>(c[0]) - b[0]) * (a[1] - c[1]);
    const long double u = ((static_cast<long double>(b[1]) - c[1]) * (q[0] - c[0]) +
                           (static_cast<long double>(c[0]) - b[0]) * (q[1] - c[1])) /
                          denominator;
    const long double v = ((static_cast<long double>(c[1]) - a[1]) * (q[0] - c[0]) +
                           (static_cast<long double>(a[0]) - c[0]) * (q[1] - c[1])) /
                          denominator;
    return u >= -1e-12L && v >= -1e-12L && u + v <= 1 + 1e-12L;
}
unsigned cases = 0;
void verify(const Contour& p, long double expected_area = -1, bool coverage = true) {
    const auto result = forge::tile_detail::tile_polygon_triangles(p);
    require(result == forge::tile_detail::tile_polygon_triangles(p),
            "Decomposition is not deterministic");
    require(result.size() == p.size() - 2, "Simple polygon did not produce n-2 pieces");
    long double total = 0, source_area = 0;
    for (std::size_t i = 1; i + 1 < p.size(); ++i)
        source_area += triangle_area(p[0], p[i], p[i + 1]);
    for (const auto& t : result) {
        require(t[0] < p.size() && t[1] < p.size() && t[2] < p.size(),
                "Piece index escaped source");
        const auto area = triangle_area(p[t[0]], p[t[1]], p[t[2]]);
        require(area > 0, "Piece is not positively oriented");
        total += area;
    }
    require(std::abs(total - source_area) < std::max(1e-25L, source_area * 1e-11L),
            "Piece area does not conserve original boundary");
    if (expected_area > 0)
        require(std::abs(source_area - expected_area) < 1e-14L,
                "Known analytic source area changed");
    if (coverage)
        for (unsigned y = 0; y < 32; ++y)
            for (unsigned x = 0; x < 32; ++x) {
                const Point q{(x + .23) / 32, (y + .37) / 32};
                const bool expected = inside_polygon(p, q);
                bool observed = false;
                for (const auto& t : result)
                    observed |= inside_triangle(p[t[0]], p[t[1]], p[t[2]], q);
                require(observed == expected, "Piece union fills a notch or loses source interior");
            }
    ++cases;
}
} // namespace
int main() {
    try {
        const Contour l{{0, 0}, {1, 0}, {1, .5}, {.5, .5}, {.5, 1}, {0, 1}};
        const Contour u{{0, 0}, {1, 0}, {1, 1}, {.75, 1}, {.75, .25}, {.25, .25}, {.25, 1}, {0, 1}};
        for (const auto& [shape, area] : std::array{std::pair{l, .75L}, std::pair{u, .625L}})
            for (unsigned flags = 0; flags < 8; ++flags) {
                auto p = shape;
                for (auto& point : p) {
                    if (flags & 1)
                        point[0] = 1 - point[0];
                    if (flags & 2)
                        point[1] = 1 - point[1];
                    if (flags & 4)
                        std::swap(point[0], point[1]);
                }
                if ((bool(flags & 1) ^ bool(flags & 2) ^ bool(flags & 4)))
                    std::reverse(p.begin(), p.end());
                for (std::size_t start = 0; start < p.size(); ++start) {
                    verify(p, area);
                    std::rotate(p.begin(), p.begin() + 1, p.end());
                }
            }
        std::mt19937 random(199);
        std::uniform_real_distribution<double> radius(.12, .45);
        for (unsigned count = 3; count <= 8; ++count)
            for (unsigned example = 0; example < 100; ++example) {
                Contour p;
                for (unsigned i = 0; i < count; ++i) {
                    const double angle = 2 * std::numbers::pi * i / count, r = radius(random);
                    p.push_back({.5 + r * std::cos(angle), .5 + r * std::sin(angle)});
                }
                verify(p);
            }
        for (double scale : {1., 1e-3, 1e-6}) {
            auto p = l;
            for (auto& point : p)
                for (auto& v : point)
                    v = .9 + v * .1 * scale;
            verify(p, -1, false);
        }
        unsigned rejected = 0;
        const auto refuse = [&](Contour p) {
            try {
                (void)forge::tile_detail::tile_polygon_triangles(p);
            } catch (const std::exception&) {
                ++rejected;
                return;
            }
            throw std::runtime_error("Invalid polygon was admitted");
        };
        refuse({{0, 0}, {1, 0}});
        refuse({{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}});
        refuse({{0, 0}, {.5, 0}, {1, 0}, {1, 1}, {0, 1}});
        refuse({{0, 0}, {1, 1}, {0, 1}, {1, 0}});
        refuse({{0, 0}, {1, 0}, {1, 1}, {.5, 0}, {0, 1}});
        auto clockwise = l;
        std::reverse(clockwise.begin(), clockwise.end());
        refuse(clockwise);
        auto bad = l;
        bad[1][0] = -1e-8;
        refuse(bad);
        bad = l;
        bad[1][0] = 1.00001;
        refuse(bad);
        bad = l;
        bad[1][0] = std::numeric_limits<double>::infinity();
        refuse(bad);
        bad = l;
        bad[1][0] = std::numeric_limits<double>::quiet_NaN();
        refuse(bad);
        Contour star;
        for (unsigned i : {0u, 2u, 4u, 1u, 3u}) {
            const auto angle = 2 * std::numbers::pi * i / 5;
            star.push_back({.5 + .4 * std::cos(angle), .5 + .4 * std::sin(angle)});
        }
        refuse(star);
        bad = l;
        bad.insert(bad.end(), {{.1, .1}, {.2, .1}, {.2, .2}});
        refuse(bad);
        std::cout << cases
                  << " original deterministic decompositions, independent "
                     "area/grid-union oracles and "
                  << rejected << " invalid profiles PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
