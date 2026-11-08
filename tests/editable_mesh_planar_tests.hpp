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
