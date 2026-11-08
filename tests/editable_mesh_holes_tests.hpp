#pragma once
#include <cmath>
#include <limits>
#include <numbers>

inline void planar_holes_tests() {
    using Point = std::array<double, 2>;
    using Loop = std::vector<Point>;
    const auto asset = AssetId::generate();
    const Loop outer{{-4, -3}, {4, -3}, {4, 3}, {-4, 3}};
    const std::vector<Loop> holes{{{-3, -1}, {-1, -1}, {-1, 1}, {-3, 1}},
                                  {{1, -1}, {3, -1}, {3, 1}, {1, 1}}};
    for (unsigned winding = 0; winding < 8; ++winding) {
        auto boundary = outer;
        auto inner = holes;
        if (winding & 1)
            std::reverse(boundary.begin(), boundary.end());
        for (unsigned i = 0; i < inner.size(); ++i)
            if (winding & (2u << i))
                std::reverse(inner[i].begin(), inner[i].end());
        const auto source = EditableMeshSource::create_planar_with_holes(asset, boundary, inner);
        auto input = boundary;
        for (const auto& h : inner)
            input.insert(input.end(), h.begin(), h.end());
        require(source.asset() == asset && source.document.at("version") == 1 &&
                    source.document.at("vertices").size() == 12 &&
                    source.document.at("faces").size() == 14 && source.document.at("next_id") == 27,
                "Mesh holes changed source identity, topology or allocator metadata");
        for (unsigned i = 0; i < input.size(); ++i) {
            const auto& v = source.document.at("vertices")[i];
            require(v.at("id") == i + 1 &&
                        v.at("position") == nlohmann::json({input[i][0], input[i][1], 0.}),
                    "Hole winding normalization changed input vertex identity");
        }
        const auto cooked = source.cook();
        const auto& part = cooked.lods[0].parts[0];
        const auto& positions = std::get<std::vector<float>>(part.find("POSITION")->values);
        const auto& normals = std::get<std::vector<float>>(part.find("NORMAL")->values);
        const auto& uv = std::get<std::vector<float>>(part.find("TEXCOORD_0")->values);
        const auto point = [&](unsigned i) {
            return Point{positions[3 * i], positions[3 * i + 1]};
        };
        const auto turn = [](Point a, Point b, Point c) {
            return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
        };
        double area = 0;
        for (unsigned i = 0; i < part.indices.size(); i += 3) {
            const auto t = turn(point(part.indices[i]), point(part.indices[i + 1]),
                                point(part.indices[i + 2]));
            require(t > 0, "Mesh hole triangle did not face positive Z");
            area += t / 2;
        }
        require(area == 40, "Mesh hole triangles differ from independent rectangle area");
        for (unsigned y = 0; y < 14; ++y)
            for (unsigned x = 0; x < 18; ++x) {
                const Point p{-4.25 + .5 * x, -3.25 + .5 * y};
                unsigned covers = 0;
                bool diagonal = false;
                for (unsigned i = 0; i < part.indices.size(); i += 3) {
                    const auto a = turn(point(part.indices[i]), point(part.indices[i + 1]), p),
                               b = turn(point(part.indices[i + 1]), point(part.indices[i + 2]), p),
                               c = turn(point(part.indices[i + 2]), point(part.indices[i]), p);
                    if (a >= 0 && b >= 0 && c >= 0) {
                        ++covers;
                        diagonal |= a == 0 || b == 0 || c == 0;
                    }
                }
                const bool filled = p[0] > -4 && p[0] < 4 && p[1] > -3 && p[1] < 3 &&
                                    !(p[1] > -1 && p[1] < 1 &&
                                      ((p[0] > -3 && p[0] < -1) || (p[0] > 1 && p[0] < 3)));
                require((covers > 0) == filled && (diagonal || covers <= 1),
                        "Hole triangles fill an opening, overlap or lose the outer region");
            }
        for (unsigned i = 0; i < part.vertices; ++i)
            require(normals[3 * i] == 0 && normals[3 * i + 1] == 0 && normals[3 * i + 2] == 1 &&
                        std::abs(uv[2 * i] - (positions[3 * i] + 4) / 8.) < 1e-6 &&
                        std::abs(uv[2 * i + 1] - (3 - positions[3 * i + 1]) / 6.) < 1e-6,
                    "Hole Mesh normals or global bounds UVs changed per triangle");
        const auto encoded = source.document.dump();
        require(EditableMeshSource::parse(std::as_bytes(std::span(encoded.data(), encoded.size())))
                        .document == source.document,
                "Hole Mesh source changed on parse/reopen");
    }
    for (const auto& invalid : std::vector<std::vector<Loop>>{
             {},
             {holes[0], holes[1], holes[0], holes[1]},
             {{{-5, -1}, {-4, -1}, {-4, 1}, {-5, 1}}},
             {{{-4, -1}, {-2, -1}, {-2, 1}, {-4, 1}}},
             {{{-5, -1}, {-2, -1}, {-2, 1}, {-5, 1}}},
             {holes[0], {{-2, -.5}, {0, -.5}, {0, .5}, {-2, .5}}},
             {holes[0], {{-3, -.5}, {-2, -.5}, {-2, .5}, {-3, .5}}},
             {holes[0], {{-2.5, -.5}, {-1.5, -.5}, {-1.5, .5}, {-2.5, .5}}},
             {{{0, 0}, {1, 0}}},
             {{{0, 0}, {1, 0}, {2, 0}}},
             {{{0, 0}, {1, 1}, {0, 1}, {1, 0}}},
             {{{0, 0}, {1, 0}, {0, std::numeric_limits<double>::quiet_NaN()}}},
             {{{0, 0}, {1, 0}, {0, 100001}}}})
        rejects([&] { (void)EditableMeshSource::create_planar_with_holes(asset, outer, invalid); });
    const auto circle = [](double x, double radius, unsigned count) {
        Loop result;
        for (unsigned i = 0; i < count; ++i) {
            const auto a = 2 * std::numbers::pi * i / count;
            result.push_back({x + radius * std::cos(a), radius * std::sin(a)});
        }
        return result;
    };
    const std::vector<Loop> maximum{circle(-2, .5, 16), circle(0, .5, 16), circle(2, .5, 16)};
    const auto largest =
        EditableMeshSource::create_planar_with_holes(asset, circle(0, 4, 16), maximum);
    require(largest.document.at("vertices").size() == 64,
            "Maximum hole profile did not retain all64 original source vertices");
    const auto largest_cooked = largest.cook();
    const auto& largest_part = largest_cooked.lods[0].parts[0];
    const auto& largest_positions =
        std::get<std::vector<float>>(largest_part.find("POSITION")->values);
    double largest_area = 0;
    for (unsigned i = 0; i < largest_part.indices.size(); i += 3) {
        const auto a = largest_part.indices[i] * 3, b = largest_part.indices[i + 1] * 3,
                   c = largest_part.indices[i + 2] * 3;
        largest_area += ((largest_positions[b] - largest_positions[a]) *
                             (largest_positions[c + 1] - largest_positions[a + 1]) -
                         (largest_positions[b + 1] - largest_positions[a + 1]) *
                             (largest_positions[c] - largest_positions[a])) /
                        2.;
    }
    // Collinear corners introduced by a bridge can be removed by decomposition;
    // independent regular-polygon area is the coverage contract, not ear count.
    const auto expected_area = 8 * std::sin(2 * std::numbers::pi / 16) * (16 - 3 * .25);
    require(std::abs(largest_area - expected_area) < 1e-4,
            "Maximum hole profile changed the independent regular-polygon area");
    rejects([&] {
        (void)EditableMeshSource::create_planar_with_holes(asset, circle(0, 4, 17), maximum);
    });
    rejects([&] {
        const std::vector<Loop> oversized{circle(0, .5, 17)};
        (void)EditableMeshSource::create_planar_with_holes(asset, outer, oversized);
    });
    const Loop concave{{0, 0}, {5, 0}, {5, 1}, {1, 1}, {1, 5}, {0, 5}};
    const std::vector<Loop> cutout{{{.2, 2}, {.8, 2}, {.8, 3}, {.2, 3}}};
    (void)EditableMeshSource::create_planar_with_holes(asset, concave, cutout).cook();
}
