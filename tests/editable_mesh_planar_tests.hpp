#pragma once
#include <cmath>
#include <limits>
#include <numbers>

inline void planar_mesh_tests() {
    using Point = std::array<double, 2>;
    const auto asset = AssetId::generate();
    const auto quad = EditableMeshSource::create_planar_quad(asset, 4, 2);
    const auto mesh = quad.cook();
    const auto& part = mesh.lods[0].parts[0];
    require(quad.asset() == asset && quad.document.at("version") == 1 &&
                quad.document.at("next_id") == 6 && part.vertices == 4 &&
                part.indices.size() == 6 &&
                part.bounds.minimum == std::array<float, 3>{-2, -1, 0} &&
                part.bounds.maximum == std::array<float, 3>{2, 1, 0},
            "Planar quad changed source identity, geometry or ordinary Mesh projection");
    const auto& uv = std::get<std::vector<float>>(part.find("TEXCOORD_0")->values);
    require(uv == std::vector<float>{0, 1, 1, 1, 1, 0, 0, 0},
            "Planar quad did not use top-left UV coordinates");
    for (unsigned count : {3u, 4u, 6u, 16u}) {
        std::vector<Point> outline;
        for (unsigned i = 0; i < count; ++i) {
            const auto angle = 2 * std::numbers::pi * i / count;
            outline.push_back({2 * std::cos(angle), std::sin(angle)});
        }
        for (bool reverse : {false, true}) {
            if (reverse)
                std::reverse(outline.begin(), outline.end());
            const auto source = EditableMeshSource::create_planar_polygon(asset, outline);
            const auto cooked = source.cook();
            const auto& polygon = cooked.lods[0].parts[0];
            const auto& normals = std::get<std::vector<float>>(polygon.find("NORMAL")->values);
            require(polygon.vertices == count && polygon.indices.size() == 3 * (count - 2) &&
                        source.document.at("next_id") == count + 2,
                    "Planar outline lost corners, triangulation or allocator metadata");
            for (unsigned i = 0; i < count; ++i)
                require(normals[i * 3] == 0 && normals[i * 3 + 1] == 0 && normals[i * 3 + 2] == 1,
                        "Planar outline front did not remain positive Z for both windings");
        }
    }
    for (double bad : {0., -1., std::numeric_limits<double>::infinity(), 200001.})
        rejects([&] { (void)EditableMeshSource::create_planar_quad(asset, bad, 1); });
    for (const auto& outline : std::vector<std::vector<Point>>{
             {},
             {{0, 0}, {1, 0}},
             {{0, 0}, {1, 0}, {2, 0}},
             {{0, 0}, {1, 0}, {.25, .25}, {0, 1}},
             {{0, 0}, {1, 0}, {1, 0}, {0, 1}},
             {{0, 0}, {1, 0}, {0, std::numeric_limits<double>::quiet_NaN()}}})
        rejects([&] { (void)EditableMeshSource::create_planar_polygon(asset, outline); });
    std::vector<Point> star;
    for (unsigned i = 0; i < 5; ++i) {
        const auto angle = 4 * std::numbers::pi * i / 5;
        star.push_back({std::cos(angle), std::sin(angle)});
    }
    rejects([&] { (void)EditableMeshSource::create_planar_polygon(asset, star); });
    std::vector<Point> pentagon;
    for (unsigned i = 0; i < 5; ++i) {
        const auto angle = 2 * std::numbers::pi * i / 5;
        pentagon.push_back({std::cos(angle), std::sin(angle)});
    }
    auto crossing = EditableMeshSource::create_planar_polygon(asset, pentagon);
    const auto boundary = crossing.document["faces"][0]["corners"];
    for (unsigned i = 0; i < 5; ++i)
        crossing.document["faces"][0]["corners"][i] = boundary[(i * 2) % 5];
    rejects([&] { crossing.validate(); });
    rejects([&] { (void)crossing.cook(); });
}

inline void planar_outline_tests() {
    using Point = std::array<double, 2>;
    const auto asset = AssetId::generate();
    std::vector<Point> outline{{0, 0}, {3, 0}, {3, 1}, {1, 1}, {1, 3}, {0, 3}};
    for (bool clockwise : {false, true}) {
        if (clockwise)
            std::reverse(outline.begin(), outline.end());
        const auto source = EditableMeshSource::create_planar_outline(asset, outline);
        require(source.asset() == asset && source.document.at("version") == 1 &&
                    source.document.at("vertices").size() == 6 &&
                    source.document.at("faces").size() == 4 && source.document.at("next_id") == 11,
                "Concave creation changed ordinary source identity or allocator metadata");
        for (unsigned i = 0; i < outline.size(); ++i) {
            const auto& vertex = source.document.at("vertices")[i];
            require(vertex.at("id") == i + 1 &&
                        vertex.at("position") == nlohmann::json({outline[i][0], outline[i][1], 0.}),
                    "Winding normalization renumbered the original outline vertices");
        }
        const auto cooked = source.cook();
        const auto& part = cooked.lods[0].parts[0];
        const auto& positions = std::get<std::vector<float>>(part.find("POSITION")->values);
        const auto& uv = std::get<std::vector<float>>(part.find("TEXCOORD_0")->values);
        const auto& normals = std::get<std::vector<float>>(part.find("NORMAL")->values);
        require(part.indices.size() == 12, "Concave source did not cook four triangles");
        double area = 0;
        const auto point = [&](unsigned index) {
            return Point{positions[index * 3], positions[index * 3 + 1]};
        };
        const auto turn = [](Point a, Point b, Point c) {
            return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
        };
        for (unsigned i = 0; i < part.indices.size(); i += 3) {
            const auto twice = turn(point(part.indices[i]), point(part.indices[i + 1]),
                                    point(part.indices[i + 2]));
            require(twice > 0, "Concave triangle did not face +Z");
            area += twice / 2;
        }
        require(area == 5, "Triangulation differs from independent L-shape area");
        for (unsigned y = 0; y < 6; ++y)
            for (unsigned x = 0; x < 6; ++x) {
                const Point p{.25 + x * .5, .25 + y * .5};
                bool covered = false;
                for (unsigned i = 0; i < part.indices.size(); i += 3) {
                    const auto a = point(part.indices[i]), b = point(part.indices[i + 1]),
                               c = point(part.indices[i + 2]);
                    covered |= turn(a, b, p) >= 0 && turn(b, c, p) >= 0 && turn(c, a, p) >= 0;
                }
                require(covered == (p[0] < 1 || p[1] < 1),
                        "Cooked triangles filled the notch or lost an outline arm");
            }
        for (unsigned i = 0; i < part.vertices; ++i) {
            require(normals[i * 3] == 0 && normals[i * 3 + 1] == 0 && normals[i * 3 + 2] == 1,
                    "Concave source lost flat +Z normals");
            require(std::abs(uv[i * 2] - positions[i * 3] / 3.) < 1e-6 &&
                        std::abs(uv[i * 2 + 1] - (1 - positions[i * 3 + 1] / 3.)) < 1e-6,
                    "Concave source stretched UVs independently per triangle");
        }
        const auto encoded = source.document.dump();
        require(EditableMeshSource::parse(std::as_bytes(std::span(encoded.data(), encoded.size())))
                        .document == source.document,
                "Concave source changed on parse/reopen");
    }
    for (const auto& invalid : std::vector<std::vector<Point>>{
             {},
             {{0, 0}, {1, 0}},
             {{0, 0}, {1, 0}, {2, 0}},
             {{0, 0}, {2, 2}, {0, 2}, {2, 0}},
             {{0, 0}, {2, 0}, {2, 2}, {1, 0}, {0, 2}},
             {{0, 0}, {1, 0}, {1, 1}, {0, 0}},
             {{0, 0}, {1, 0}, {1, 1}, {.5, 1}, {0, 1}},
             {{0, 0}, {1, 0}, {0, 100001}},
             {{0, 0}, {1, 0}, {0, std::numeric_limits<double>::quiet_NaN()}},
             {{0, 0}, {1, 0}, {0, std::numeric_limits<double>::infinity()}}})
        rejects([&] { (void)EditableMeshSource::create_planar_outline(asset, invalid); });
    std::vector<Point> too_many;
    for (unsigned i = 0; i < 17; ++i) {
        const auto angle = 2 * std::numbers::pi * i / 17;
        too_many.push_back({std::cos(angle), std::sin(angle)});
    }
    rejects([&] { (void)EditableMeshSource::create_planar_outline(asset, too_many); });
}
