#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numbers>
#include <set>
namespace {
using FacingVertex = std::array<float, 9>;
using FacingTriangle = std::array<FacingVertex, 3>;
FacingVertex facing_vertex(const MeshPart& part, unsigned row) {
    FacingVertex key{};
    unsigned at = 0;
    for (const auto* name : {"POSITION", "TEXCOORD_0", "COLOR_0"}) {
        const auto* field = part.find(name);
        require(field, "Facing fixture attribute missing");
        for (unsigned i = 0; i < field->components; ++i)
            key[at++] = std::get<std::vector<float>>(field->values)[row * field->components + i];
    }
    require(at == 9, "Facing fixture attribute dimensions differ");
    return key;
}
std::vector<FacingTriangle> facing_triangles(const MeshPart& part, bool reverse) {
    std::vector<FacingTriangle> result;
    for (std::size_t i = 0; i < part.indices.size(); i += 3) {
        FacingTriangle triangle{facing_vertex(part, part.indices[i]),
                                facing_vertex(part, part.indices[i + 1]),
                                facing_vertex(part, part.indices[i + 2])};
        if (reverse)
            std::swap(triangle[1], triangle[2]);
        const auto first = std::min_element(triangle.begin(), triangle.end());
        std::rotate(triangle.begin(), first, triangle.end());
        result.push_back(triangle);
    }
    std::sort(result.begin(), result.end());
    return result;
}
void facing_cook_parity(const EditableMeshSource& source, const EditableMeshSource& reversed) {
    const auto old_mesh = source.cook(), new_mesh = reversed.cook();
    const auto& old_part = old_mesh.lods[0].parts[0];
    const auto& new_part = new_mesh.lods[0].parts[0];
    require(
        facing_triangles(old_part, true) == facing_triangles(new_part, false),
        "Facing cook changed geometry/UV/color association or did not reverse triangle winding");
    unsigned start = 0;
    for (const auto& face : source.document.at("faces")) {
        const auto count = face.at("corners").size();
        for (unsigned i = 0; i < count; ++i) {
            const auto row = start + (i ? count - i : 0);
            for (unsigned axis = 0; axis < 3; ++axis)
                require(std::abs(std::get<std::vector<float>>(
                                     old_part.find("NORMAL")->values)[(start + i) * 3 + axis] +
                                 std::get<std::vector<float>>(
                                     new_part.find("NORMAL")->values)[row * 3 + axis]) < .000001,
                        "Facing cook normal did not reverse direction");
        }
        start += count;
    }
}
void facing_metadata(EditableMeshSource& source) {
    source.document["extension"] = {{"retained", true}};
    for (auto& vertex : source.document.at("vertices")) {
        const auto id = vertex.at("id").get<unsigned>();
        vertex["color"] = {double(id % 3) / 2., double(id % 5) / 4., double(id % 7) / 6., 1.};
        vertex["extension"] = {{"vertex", id}};
    }
    for (auto& face : source.document.at("faces")) {
        face["extension"] = {{"face", face.at("id")}};
        for (auto& corner : face.at("corners"))
            corner["extension"] = {{"vertex", corner.at("vertex")}, {"retained", true}};
    }
    source.validate();
}
EditableMeshSource facing_vertex_touch() {
    auto source = EditableMeshSource::create_cube(AssetId::generate());
    const std::array<std::array<double, 3>, 5> points{
        {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {-1, 0, 0}, {0, -1, 0}}};
    source.document["vertices"] = nlohmann::json::array();
    for (unsigned i = 0; i < 5; ++i)
        source.document["vertices"].push_back({{"id", i + 1}, {"position", points[i]}});
    source.document["faces"] = nlohmann::json::array();
    for (const auto ids : {std::array{1u, 2u, 3u}, std::array{1u, 4u, 5u}}) {
        auto corners = nlohmann::json::array();
        for (const auto id : ids)
            corners.push_back({{"vertex", id}, {"uv", {double(id) / 5., double(id) / 7.}}});
        source.document["faces"].push_back(
            {{"id", 6 + source.document["faces"].size()}, {"corners", corners}});
    }
    source.document["next_id"] = 8;
    facing_metadata(source);
    return source;
}
void mesh_facing_tests() {
    auto source = EditableMeshSource::create_cube(AssetId::generate());
    facing_metadata(source);
    std::vector<std::uint32_t> all;
    for (const auto& face : source.document.at("faces"))
        all.push_back(face.at("id").get<unsigned>());
    require(source.connected_faces(13) == all && source.connected_faces(all.back()) == all,
            "Cube connected surface omitted adjacent faces or changed canonical order");
    const auto original = source.document;
    for (const auto selection : {std::vector<std::uint32_t>{}, std::vector<std::uint32_t>{13, 13},
                                 std::vector<std::uint32_t>{0}, std::vector<std::uint32_t>{999},
                                 std::vector<std::uint32_t>{13}}) {
        rejects([&] { source.reverse_faces(selection); });
        require(source.document == original, "Facing rejection changed source");
    }
    rejects([&] { (void)source.connected_faces(999); });
    for (bool smooth : {false, true}) {
        auto before = source;
        before.set_smooth_shading(smooth);
        auto reversed = before;
        auto permuted = all;
        std::reverse(permuted.begin(), permuted.end());
        reversed.reverse_faces(permuted);
        auto expected = before.document;
        for (auto& face : expected.at("faces")) {
            const auto corners = face.at("corners");
            for (std::size_t i = 1; i < corners.size(); ++i)
                face["corners"][i] = corners[corners.size() - i];
        }
        require(reversed.document == expected, "Facing reversal lost complete corner/source data, "
                                               "geometry, IDs, shading or allocator");
        facing_cook_parity(before, reversed);
        reversed.reverse_faces(all);
        require(reversed.document == before.document,
                "Double facing reversal changed exact source");
    }
    auto touching = facing_vertex_touch();
    require(touching.connected_faces(6) == std::vector<std::uint32_t>{6} &&
                touching.connected_faces(7) == std::vector<std::uint32_t>{7},
            "Vertex-only contact incorrectly joined surfaces");
    auto reversed_touching = touching;
    reversed_touching.reverse_faces(std::array{6u});
    require(reversed_touching.document.at("faces")[1] == touching.document.at("faces")[1],
            "Disconnected surface reversal changed another face");
    touching.set_smooth_shading(true);
    const auto smooth_before = touching.document;
    rejects([&] { touching.reverse_faces(std::array{6u}); });
    require(touching.document == smooth_before,
            "Undefined candidate averaged normals changed source");
    auto edge = facing_vertex_touch();
    edge.document["vertices"][3]["position"] = {1, 1, 0};
    edge.document["faces"][1]["corners"][0]["vertex"] = 2;
    edge.document["faces"][1]["corners"][1]["vertex"] = 4;
    edge.document["faces"][1]["corners"][2]["vertex"] = 3;
    edge.validate();
    require(edge.connected_faces(7) == std::vector<std::uint32_t>{6, 7},
            "Shared edge did not join the surface");
    auto polygon = facing_vertex_touch();
    polygon.document["vertices"] = nlohmann::json::array();
    auto corners = nlohmann::json::array();
    for (unsigned i = 0; i < 16; ++i) {
        const double angle = 2 * std::numbers::pi * i / 16;
        polygon.document["vertices"].push_back(
            {{"id", i + 1}, {"position", {std::cos(angle), std::sin(angle), 0.}}});
        corners.push_back({{"vertex", i + 1}, {"uv", {double(i) / 16., double(i) / 19.}}});
    }
    polygon.document["faces"] = {{{"id", 17}, {"corners", corners}}};
    polygon.document["next_id"] = 18;
    facing_metadata(polygon);
    auto reversed_polygon = polygon;
    reversed_polygon.reverse_faces(std::array{17u});
    facing_cook_parity(polygon, reversed_polygon);
    reversed_polygon.reverse_faces(std::array{17u});
    require(reversed_polygon.document == polygon.document,
            "Maximum-corner polygon reversal changed source");
}
} // namespace
