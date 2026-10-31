#include "tile_polygon_contours.hpp"
#include <iostream>
#include <random>
using namespace forge::tile_detail::contour_detail;
namespace {
std::vector<Triangle> prepared(const std::vector<Region>& regions) {
    const auto data = forge::tile_detail::prepare_tile_collision_contours(regions);
    require(data.contours == regions && data.vertices.size() <= 160,
            "Prepared geometry changed source contours or escaped vertex bounds");
    std::vector<Triangle> triangles;
    for (const auto& t : data.triangles)
        triangles.push_back(
            {data.vertices.at(t[0]), data.vertices.at(t[1]), data.vertices.at(t[2])});
    return triangles;
}
using Wide = std::array<long double, 2>;
long double cross(Wide a, Wide b, Wide c) {
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
}
Wide wide(Point p) { return {p[0], p[1]}; }
long double area(const std::vector<Wide>& p) {
    long double out = 0;
    for (std::size_t i = 1; i + 1 < p.size(); ++i)
        out += cross(p[0], p[i], p[i + 1]);
    return out / 2;
}
long double area(const Loop& p) {
    std::vector<Wide> v;
    for (auto q : p)
        v.push_back(wide(q));
    return area(v);
}
int winding(const Loop& loop, Wide p) {
    int n = 0;
    for (std::size_t i = 0; i < loop.size(); ++i) {
        auto a = wide(loop[i]), b = wide(loop[(i + 1) % loop.size()]);
        auto c = cross(a, b, p);
        if (c == 0 && p[0] >= std::min(a[0], b[0]) && p[0] <= std::max(a[0], b[0]) &&
            p[1] >= std::min(a[1], b[1]) && p[1] <= std::max(a[1], b[1]))
            return 2;
        if (a[1] <= p[1]) {
            if (b[1] > p[1] && c > 0)
                ++n;
        } else if (b[1] <= p[1] && c < 0)
            --n;
    }
    return n ? 1 : 0;
}
long double overlap(const Triangle& a, const Triangle& b) {
    std::vector<Wide> p;
    for (auto q : a)
        p.push_back(wide(q));
    for (unsigned i = 0; i < 3 && !p.empty(); ++i) {
        auto x = wide(b[i]), y = wide(b[(i + 1) % 3]);
        std::vector<Wide> out;
        for (std::size_t j = 0; j < p.size(); ++j) {
            auto c = p[j], d = p[(j + 1) % p.size()];
            auto s = cross(x, y, c), t = cross(x, y, d);
            if ((s >= 0) != (t >= 0)) {
                auto u = s / (s - t);
                out.push_back({c[0] + (d[0] - c[0]) * u, c[1] + (d[1] - c[1]) * u});
            }
            if (t >= 0)
                out.push_back(d);
        }
        p = std::move(out);
    }
    return std::abs(area(p));
}
Loop box(double x, double y, double w, double h) {
    return {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
}
std::size_t cases = 0, samples = 0;
void prove(const std::vector<Region>& regions) {
    auto triangles = prepared(regions);
    require(!triangles.empty() && triangles.size() <= 184, "Output escaped bounded shape envelope");
    long double expected = 0, actual = 0;
    for (const auto& r : regions) {
        expected += area(r.points);
        for (const auto& h : r.holes)
            expected -= area(h);
    }
    for (std::size_t i = 0; i < triangles.size(); ++i) {
        auto a = wide(triangles[i][0]), b = wide(triangles[i][1]), c = wide(triangles[i][2]);
        auto part = cross(a, b, c) / 2;
        require(part > 0, "Nonpositive output triangle");
        actual += part;
        for (std::size_t j = 0; j < i; ++j)
            require(overlap(triangles[i], triangles[j]) < 1e-16L,
                    "Output triangles overlap with positive area");
    }
    require(std::abs(expected - actual) <= expected * 1e-11L,
            "Filled area differs from independent long-double contour area");
    for (unsigned y = 0; y < 13; ++y)
        for (unsigned x = 0; x < 13; ++x) {
            Wide p{(x * 19 + .37L) / 257, (y * 19 + .61L) / 257};
            bool expected_inside = false, boundary = false;
            for (const auto& r : regions) {
                int a = winding(r.points, p);
                boundary |= a == 2;
                bool in = a == 1;
                for (const auto& h : r.holes) {
                    int b = winding(h, p);
                    boundary |= b == 2;
                    in &= b == 0;
                }
                expected_inside |= in;
            }
            if (boundary)
                continue;
            bool inside = false;
            for (const auto& t : triangles)
                inside |= cross(wide(t[0]), wide(t[1]), p) >= 0 &&
                          cross(wide(t[1]), wide(t[2]), p) >= 0 &&
                          cross(wide(t[2]), wide(t[0]), p) >= 0;
            require(inside == expected_inside,
                    "Triangle union differs from independent winding coverage");
            ++samples;
        }
    ++cases;
}
template <class F> void reject(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "Invalid compound topology was accepted");
}
} // namespace
int main() {
    try {
        prove({{box(0, 0, 1, 1), {box(.25, .25, .5, .5)}}});
        prove({{box(0, 0, 1, 1),
                {box(.125, .125, .25, .25), box(.625, .125, .25, .25), box(.125, .625, .25, .25),
                 box(.625, .625, .25, .25)}}});
        prove({{box(0, 0, 1, 1), {box(.125, .125, .75, .75)}},
               {box(.25, .25, .5, .5), {box(.375, .375, .25, .25)}}});
        prove(
            {{{{0, 0}, {1, 0}, {1, .5}, {.5, .5}, {.5, 1}, {0, 1}}, {box(.125, .125, .25, .25)}}});
        const auto octagon = [](double x, double y, double w, double h) {
            const auto dx = w / 4, dy = h / 4;
            return Loop{{x + dx, y},         {x + w - dx, y}, {x + w, y + dy}, {x + w, y + h - dy},
                        {x + w - dx, y + h}, {x + dx, y + h}, {x, y + h - dy}, {x, y + dy}};
        };
        Region maximum{octagon(0, 0, 1, 1), {}};
        for (unsigned h = 0; h < 4; ++h)
            maximum.holes.push_back(octagon(.125 + (h % 2) * .5, .125 + (h / 2) * .5, .25, .25));
        std::vector<Region> maximum_regions;
        for (unsigned group = 0; group < 4; ++group) {
            auto region = maximum;
            const auto transform = [&](Loop& loop) {
                for (auto& p : loop) {
                    p[0] = p[0] * .4375 + (group % 2) * .5625;
                    p[1] = p[1] * .4375 + (group / 2) * .5625;
                }
            };
            transform(region.points);
            for (auto& h : region.holes)
                transform(h);
            maximum_regions.push_back(region);
        }
        for (unsigned permutation = 0; permutation < 8; ++permutation) {
            auto value = maximum_regions;
            for (auto& r : value) {
                std::rotate(r.points.begin(), r.points.begin() + permutation, r.points.end());
                for (auto& h : r.holes)
                    std::rotate(h.begin(), h.begin() + (permutation * 3) % h.size(), h.end());
                if (permutation % 2)
                    std::reverse(r.holes.begin(), r.holes.end());
            }
            prove(value);
        }
        for (unsigned flags = 0; flags < 8; ++flags) {
            auto value = maximum_regions;
            for (auto& r : value) {
                const auto transform = [&](Loop& loop) {
                    for (auto& p : loop) {
                        double x = flags & 1 ? 1 - p[0] : p[0], v = flags & 2 ? p[1] : 1 - p[1];
                        if (flags & 4)
                            std::swap(x, v);
                        p = {x, 1 - v};
                    }
                    if (area(loop) < 0)
                        std::reverse(loop.begin(), loop.end());
                };
                transform(r.points);
                for (auto& h : r.holes)
                    transform(h);
            }
            prove(value);
        }
        prove({{box(.75, .75, 1. / 4096, 1. / 4096),
                {box(.75 + 1. / 16384, .75 + 1. / 16384, 1. / 8192, 1. / 8192)}}});
        const Loop u_hole{{.2, .2}, {.8, .2}, {.8, .8}, {.6, .8},
                          {.6, .4}, {.4, .4}, {.4, .8}, {.2, .8}};
        prove({{box(0, 0, 1, 1), {u_hole}}});
        prove({{box(0, 0, 1, 1), {u_hole}}, {box(.45, .25, .1, .1), {}}});
        reject([&] {
            prepared(std::vector<Region>{{box(0, 0, 1, 1), {u_hole}}, {box(.45, .5, .1, .1), {}}});
        });
        reject([] { prepared(std::vector<Region>(5, Region{box(0, 0, .1, .1), {}})); });
        reject([] {
            prepared(
                std::vector<Region>{{box(0, 0, 1, 1), std::vector<Loop>(5, box(.1, .1, .1, .1))}});
        });
        reject([] {
            auto outer = box(0, 0, 1, 1);
            outer[0][0] = std::numeric_limits<double>::quiet_NaN();
            prepared(std::vector<Region>{{outer, {}}});
        });
        std::mt19937 random(20261031);
        for (unsigned i = 0; i < 1000; ++i) {
            Region r{box(0, 0, 1, 1), {}};
            unsigned count = 1 + random() % 4;
            for (unsigned h = 0; h < count; ++h) {
                double x = (h % 2) * .5 + double(1 + random() % 4) / 32,
                       y = (h / 2) * .5 + double(1 + random() % 4) / 32,
                       w = double(4 + random() % 5) / 32, height = double(4 + random() % 5) / 32;
                r.holes.push_back(box(x, y, w, height));
            }
            prove({r});
        }
        reject([] { prepared(std::vector<Region>{}); });
        reject([] {
            auto h = box(.25, .25, .5, .5);
            std::reverse(h.begin(), h.end());
            prepared(std::vector<Region>{{box(0, 0, 1, 1), {h}}});
        });
        for (auto h : std::vector<Loop>{box(0, .25, .25, .25), box(.75, .25, .5, .25),
                                        box(.25, .25, 0, .25)})
            reject([&] { prepared(std::vector<Region>{{box(0, 0, 1, 1), {h}}}); });
        reject([] {
            prepared(std::vector<Region>{
                {box(0, 0, 1, 1), {box(.25, .25, .5, .5), box(.375, .375, .125, .125)}}});
        });
        reject([] {
            prepared(std::vector<Region>{{box(0, 0, .75, .75), {}}, {box(.5, .5, .5, .5), {}}});
        });
        reject([] {
            prepared(std::vector<Region>{{box(0, 0, .5, .5), {}}, {box(.5, 0, .5, .5), {}}});
        });
        std::cout << cases << " original compound geometry cases, " << samples
                  << " independent winding samples; positive area, pairwise "
                     "triangle clipping and rejection checks PASS\n";
    } catch (const std::exception& e) {
        std::cerr << "case " << cases << ": " << e.what() << '\n';
        return 1;
    }
}
