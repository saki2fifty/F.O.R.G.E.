#pragma once
#include <cmath>
#include <limits>
#include <map>
namespace {
using InsetJson = nlohmann::json;
using InsetPoint = std::array<double, 3>;
const InsetJson& inset_face_data(const EditableMeshSource& source, std::uint32_t identity) {
    for (const auto& face : source.document.at("faces"))
        if (face.at("id") == identity)
            return face;
    throw std::runtime_error("Inset face missing");
}
InsetPoint inset_position(const EditableMeshSource& source, const InsetJson& corner) {
    for (const auto& vertex : source.document.at("vertices"))
        if (vertex.at("id") == corner.at("vertex"))
            return vertex.at("position").get<InsetPoint>();
    throw std::runtime_error("Inset vertex missing");
}
void inset_offset_invariant(const EditableMeshSource& before, const EditableMeshSource& after,
                            std::uint32_t identity, double distance) {
    auto minus = [](InsetPoint a, InsetPoint b) {
        return InsetPoint{a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    };
    auto dot = [](InsetPoint a, InsetPoint b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    auto cross = [](InsetPoint a, InsetPoint b) {
        return InsetPoint{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
                          a[0] * b[1] - a[1] * b[0]};
    };
    const auto& outer = inset_face_data(before, identity).at("corners");
    const auto& inner = inset_face_data(after, identity).at("corners");
    const auto origin = inset_position(before, outer[0]);
    auto normal = cross(minus(inset_position(before, outer[1]), origin),
                        minus(inset_position(before, outer[2]), origin));
    const auto magnitude = std::sqrt(dot(normal, normal));
    for (auto& value : normal)
        value /= magnitude;
    require(outer.size() == inner.size(), "Inset changed inner corner count");
    for (std::size_t i = 0; i < inner.size(); ++i) {
        const auto point = inset_position(after, inner[i]);
        require(std::abs(dot(minus(point, origin), normal)) < 1e-8,
                "Inset left original face plane");
        for (std::size_t j = 0; j < outer.size(); ++j) {
            const auto start = inset_position(before, outer[j]);
            auto direction = minus(inset_position(before, outer[(j + 1) % outer.size()]), start);
            const auto size = std::sqrt(dot(direction, direction));
            for (auto& value : direction)
                value /= size;
            const auto actual = dot(minus(point, start), cross(normal, direction));
            require(actual >= distance - 1e-8, "Inset crossed an inward edge");
            if (j == i || (j + 1) % outer.size() == i)
                require(std::abs(actual - distance) < 1e-8, "Inset edge distance is not uniform");
        }
    }
}
EditableMeshSource inset_plane(std::vector<InsetPoint> points) {
    auto result = EditableMeshSource::create_cube(AssetId::generate());
    result.document["vertices"] = InsetJson::array();
    InsetJson corners = InsetJson::array();
    std::uint32_t next = 1;
    for (auto point : points) {
        result.document["vertices"].push_back(
            {{"id", next},
             {"position", point},
             {"color", std::array<double, 4>{point[0] / 3, point[1] / 3, 0, 1}}});
        corners.push_back(
            {{"vertex", next++}, {"uv", std::array<double, 2>{point[0] / 3, point[1] / 3}}});
    }
    result.document["faces"] = InsetJson::array({{{"id", next++}, {"corners", corners}}});
    result.document["next_id"] = next;
    result.validate();
    return result;
}
void mesh_inset_tests() {
    auto cube = EditableMeshSource::create_cube(AssetId::generate());
    const auto original = cube;
    require(cube.inset_face(13, .1) == 13, "Inset lost selected inner identity");
    require(cube.document.at("vertices").size() == 12 && cube.document.at("faces").size() == 10 &&
                cube.document.at("next_id") == 23 && cube.asset() == original.asset(),
            "Inset topology or identity count differs");
    inset_offset_invariant(original, cube, 13, .1);
    for (std::size_t i = 0; i < 8; ++i)
        require(cube.document["vertices"][i] == original.document["vertices"][i],
                "Inset changed outer vertices");
    for (std::size_t i = 0; i < 6; ++i)
        if (original.document["faces"][i]["id"] != 13)
            require(cube.document["faces"][i] == original.document["faces"][i],
                    "Inset changed neighboring face");
    for (const auto& corner : inset_face_data(cube, 13).at("corners")) {
        const auto uv = corner.at("uv").get<std::array<double, 2>>();
        for (auto value : uv)
            require(std::abs(value - .1) < 1e-9 || std::abs(value - .9) < 1e-9,
                    "Square inset UV differs");
    }
    require(cube.cook().lods[0].parts[0].indices.size() == 60, "Inset fan triangle count differs");
    auto raised = cube;
    const auto raised_cap = raised.extrude_face(13, .2);
    for (const auto& corner : inset_face_data(raised, raised_cap).at("corners"))
        require(std::abs(inset_position(raised, corner)[1] - .7) < 1e-9,
                "Raised cap height differs");
    auto recessed = cube;
    const auto recessed_cap = recessed.extrude_face(13, -.2);
    for (const auto& corner : inset_face_data(recessed, recessed_cap).at("corners"))
        require(std::abs(inset_position(recessed, corner)[1] - .3) < 1e-9,
                "Recessed cap height differs");
    require(recessed.cook().lods[0].parts[0].indices.size() == 84, "Recessed panel cook differs");
    auto rectangle = original;
    rectangle.transform_vertices(std::array<std::uint32_t, 8>{1, 2, 3, 4, 5, 6, 7, 8}, {3, -2, 5},
                                 {24, 35, 18}, {2, 1.5, 3});
    auto rotated = rectangle;
    rotated.inset_face(13, .2);
    inset_offset_invariant(rectangle, rotated, 13, .2);
    auto triangle = inset_plane({{0, 0, 0}, {2, 0, 0}, {0, 2, 0}});
    auto inset_triangle = triangle;
    inset_triangle.inset_face(4, .2);
    inset_offset_invariant(triangle, inset_triangle, 4, .2);
    const auto& inner = inset_face_data(inset_triangle, 4).at("corners");
    require(std::abs(inset_position(inset_triangle, inner[1])[0] - (2 - .2 * (1 + std::sqrt(2.)))) <
                1e-9,
            "Triangle inset bisector differs");
    for (const auto& corner : inner) {
        const auto point = inset_position(inset_triangle, corner);
        const auto uv = corner.at("uv").get<std::array<double, 2>>();
        require(std::abs(uv[0] - point[0] / 3) < 1e-9 && std::abs(uv[1] - point[1] / 3) < 1e-9,
                "Barycentric UV differs");
        for (const auto& vertex : inset_triangle.document.at("vertices"))
            if (vertex.at("id") == corner.at("vertex")) {
                const auto color = vertex.at("color").get<std::array<double, 4>>();
                require(std::abs(color[0] - point[0] / 3) < 1e-9 &&
                            std::abs(color[1] - point[1] / 3) < 1e-9,
                        "Barycentric color differs");
            }
    }
    require(inset_triangle.cook().lods[0].parts[0].indices.size() == 21,
            "Triangle inset cook differs");
    auto skew = inset_plane({{0, 0, 0}, {3, 0, 0}, {2, 2, 0}, {0, 2, 0}});
    auto inset_skew = skew;
    inset_skew.inset_face(5, .2);
    inset_offset_invariant(skew, inset_skew, 5, .2);
    for (double distance : {0., -.1, std::numeric_limits<double>::infinity(),
                            std::numeric_limits<double>::quiet_NaN(), 2.}) {
        auto safe = skew;
        rejects([&] { safe.inset_face(5, distance); });
        require(safe.document == skew.document, "Invalid inset mutated source");
    }
    auto safe = skew;
    rejects([&] { safe.inset_face(99, .1); });
    require(safe.document == skew.document, "Missing inset face mutated source");
    auto exhausted = triangle;
    exhausted.document["next_id"] = std::numeric_limits<std::uint32_t>::max();
    exhausted.validate();
    const auto before = exhausted.document;
    rejects([&] { exhausted.inset_face(4, .1); });
    require(exhausted.document == before, "Inset ID exhaustion mutated source");
    auto clockwise = inset_plane({{0, 2, 0}, {2, 0, 0}, {0, 0, 0}});
    const auto clockwise_before = clockwise;
    clockwise.inset_face(4, .15);
    inset_offset_invariant(clockwise_before, clockwise, 4, .15);
    std::vector<InsetPoint> points;
    for (unsigned i = 0; i < 16; ++i) {
        const auto angle = double(i) * 2 * 3.141592653589793 / 16;
        points.push_back({1.5 + std::cos(angle), 1.5 + std::sin(angle), 0});
    }
    auto polygon16 = inset_plane(points);
    const auto polygon16_before = polygon16;
    polygon16.inset_face(17, .1);
    inset_offset_invariant(polygon16_before, polygon16, 17, .1);
    require(polygon16.document.at("vertices").size() == 32 &&
                polygon16.document.at("faces").size() == 17 &&
                polygon16.cook().lods[0].parts[0].indices.size() == 138,
            "Maximum-corner face inset differs");
    auto skinny = inset_plane({{0, 0, 0}, {3, 0, 0}, {.001, .01, 0}});
    const auto skinny_before = skinny.document;
    rejects([&] { skinny.inset_face(4, .02); });
    require(skinny.document == skinny_before, "Collapsed acute inset changed source");
    // Independent valid triangles approach the source vertex budget. No
    // overlapping topology or out-of-bounds element is used to test preflight.
    auto bounded = triangle;
    bounded.document["vertices"] = InsetJson::array();
    bounded.document["faces"] = InsetJson::array();
    std::uint32_t next = 1;
    for (unsigned i = 0; i < 1365; ++i) {
        auto vertices = triangle.document.at("vertices");
        auto face = triangle.document.at("faces")[0];
        for (unsigned k = 0; k < 3; ++k) {
            vertices[k]["id"] = next;
            face["corners"][k]["vertex"] = next++;
            vertices[k]["position"][2] = double(i) * .01;
            bounded.document["vertices"].push_back(vertices[k]);
        }
        face["id"] = next++;
        bounded.document["faces"].push_back(face);
    }
    bounded.document["next_id"] = next;
    bounded.validate();
    const auto bounded_before = bounded.document;
    rejects([&] { bounded.inset_face(4, .1); });
    require(bounded.document == bounded_before, "Vertex-limit inset changed source");
    auto repeated = original;
    for (unsigned i = 0; i < 5; ++i)
        repeated.inset_face(13, .05);
    require(repeated.document.at("vertices").size() == 28 &&
                repeated.document.at("faces").size() == 26 &&
                repeated.cook().lods[0].parts[0].indices.size() == 156,
            "Repeated inset topology differs");
}
} // namespace
