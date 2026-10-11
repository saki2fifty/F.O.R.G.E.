#pragma once
#include <cmath>
#include <limits>
#include <numbers>
namespace {
EditableMeshSource uv_grid(unsigned side);
void mesh_triangulation_tests() {
    for (unsigned count = 3; count <= 16; ++count) {
        auto mesh = EditableMeshSource::create_cube(AssetId::generate());
        mesh.document["vertices"] = nlohmann::json::array();
        auto corners = nlohmann::json::array();
        for (unsigned i = 0; i < count; ++i) {
            const auto a = 2 * std::numbers::pi * i / count;
            mesh.document["vertices"].push_back({{"id", i + 1},
                                                 {"position", {std::cos(a), std::sin(a), 0.}},
                                                 {"color", {.2, .4, .6, 1.}},
                                                 {"extension", {{"vertex", 42}}}});
            corners.push_back({{"vertex", i + 1},
                               {"uv", {double(i) / count, double(i % 3) / 3}},
                               {"extension", {{"corner", i}}}});
        }
        mesh.document["faces"] = {
            {{"id", count + 1}, {"corners", corners}, {"extension", {{"face", 42}}}}};
        mesh.document["next_id"] = count + 2;
        mesh.document["extension"] = {{"source", 42}};
        mesh.validate();
        const auto original = mesh.document;
        const auto before = mesh.cook();
        mesh.triangulate_faces(std::array<std::uint32_t, 1>{count + 1});
        require(mesh.document.at("faces").size() == count - 2, "Polygon fan count differs");
        require(mesh.document.at("vertices") == original.at("vertices") &&
                    mesh.document.at("extension") == original.at("extension"),
                "Triangulation changed geometry/colors/vertex/source extensions");
        for (unsigned i = 0; i < count - 2; ++i) {
            const auto& face = mesh.document["faces"][i];
            require(face.at("id") == unsigned(i ? count + 1 + i : count + 1) &&
                        face.at("corners") ==
                            nlohmann::json::array({corners[0], corners[i + 1], corners[i + 2]}) &&
                        face.at("extension") == original["faces"][0]["extension"],
                    "Triangulation fan attributes/order/IDs differ");
        }
        require(mesh.document.at("next_id") == 2 * count - 1, "Triangulation high-water differs");
        const auto after = mesh.cook();
        const auto& a = before.lods[0].parts[0];
        const auto& b = after.lods[0].parts[0];
        require(a.indices.size() == b.indices.size(),
                "Triangulation changed rendered triangle count");
        for (const auto& stream : a.streams) {
            const auto* other = b.find(stream.semantic);
            require(other, "Triangulation lost a cooked stream");
            const auto& values = std::get<std::vector<float>>(stream.values);
            const auto& actual = std::get<std::vector<float>>(other->values);
            for (std::size_t i = 0; i < a.indices.size(); ++i)
                for (unsigned c = 0; c < stream.components; ++c)
                    require(std::abs(values[a.indices[i] * stream.components + c] -
                                     actual[b.indices[i] * stream.components + c]) < 1e-6,
                            "Triangulation changed indexed flat cooked fan attributes");
        }
        const auto once = mesh.document;
        std::vector<std::uint32_t> ids;
        for (const auto& f : mesh.document["faces"])
            ids.push_back(f["id"]);
        mesh.triangulate_faces(ids);
        require(mesh.document == once, "Triangle-only selection is not a no-op");
    }
    auto cube = EditableMeshSource::create_cube(AssetId::generate());
    const auto original = cube.document;
    for (const auto& selection : std::vector<std::vector<std::uint32_t>>{{}, {9, 9}, {0}, {99}}) {
        rejects([&] { cube.triangulate_faces(selection); });
        require(cube.document == original, "Rejected triangulation changed source");
    }
    auto ordered = cube, reversed = cube;
    ordered.triangulate_faces(std::array<std::uint32_t, 2>{9, 13});
    reversed.triangulate_faces(std::array<std::uint32_t, 2>{13, 9});
    require(ordered.document == reversed.document,
            "Selection order changed triangle IDs/fan order");
    auto exhausted = cube;
    exhausted.document["next_id"] = std::numeric_limits<std::uint32_t>::max();
    const auto safe = exhausted.document;
    rejects([&] { exhausted.triangulate_faces(std::array<std::uint32_t, 1>{13}); });
    require(exhausted.document == safe, "ID overflow mutated source");
    auto grid = uv_grid(64);
    const auto large = grid.document;
    std::vector<std::uint32_t> all;
    for (const auto& f : grid.document["faces"])
        all.push_back(f["id"]);
    rejects([&] { grid.triangulate_faces(all); });
    require(grid.document == large, "Face-limit triangulation mutated source");
}
} // namespace
