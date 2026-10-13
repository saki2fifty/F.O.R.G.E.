#pragma once
#include "editable_mesh_uv_tests.hpp"
namespace {
void topology_cleanup_tests() {
    auto cube = EditableMeshSource::create_cube(AssetId::generate());
    cube.document["extension"] = {{"source", 42}};
    for (auto& vertex : cube.document["vertices"])
        vertex["extension"] = "keep vertex";
    for (auto& face : cube.document["faces"]) {
        face["extension"] = {{"face", 42}};
        for (auto& corner : face["corners"])
            corner["extension"] = corner["vertex"];
    }
    const auto original = cube.document;
    for (const auto& selection : std::vector<std::vector<std::uint32_t>>{
             {}, {9, 9}, {0}, {99}, {9, 10, 11, 12, 13, 14}, {9, 99}}) {
        rejects([&] { cube.delete_faces(selection); });
        require(cube.document == original, "Rejected face deletion changed source");
    }
    cube.delete_faces(std::array<std::uint32_t, 5>{9, 10, 11, 12, 14});
    auto retained = original;
    retained["faces"] = nlohmann::json::array({original["faces"][4]});
    require(cube.document == retained, "Deletion changed retained records or unused vertices");
    const auto before_cleanup = encode_mesh(cube.cook());
    require(cube.remove_unused_vertices() == 4, "Unused vertex cleanup count differs");
    auto used = nlohmann::json::array();
    for (const auto& vertex : original["vertices"])
        if (vertex["id"] == 3 || vertex["id"] == 4 || vertex["id"] == 7 || vertex["id"] == 8)
            used.push_back(vertex);
    retained["vertices"] = used;
    require(cube.document == retained && encode_mesh(cube.cook()) == before_cleanup,
            "Cleanup changed used attributes/high-water or cooked output");
    require(cube.remove_unused_vertices() == 0 && cube.document == retained,
            "Repeated cleanup is not a no-op");
    auto grid = uv_grid(64);
    const auto high_water = grid.document["next_id"];
    std::vector<std::uint32_t> deleted;
    for (std::size_t i = 1; i < grid.document["faces"].size(); ++i)
        deleted.push_back(grid.document["faces"][i]["id"]);
    grid.delete_faces(deleted);
    require(grid.document["vertices"].size() == 4096 && grid.document["faces"].size() == 1 &&
                grid.remove_unused_vertices() == 4092 && grid.document["vertices"].size() == 4 &&
                grid.document["next_id"] == high_water,
            "Maximum-profile deletion/cleanup changed limits or identity high-water");

    auto split = EditableMeshSource{original};
    split.triangulate_faces(std::array<std::uint32_t, 1>{9});
    const auto triangles = split.document;
    auto inverse = original;
    inverse["next_id"] = 16;
    require(split.join_triangles({5, 7}) == 9 && split.document == inverse,
            "Triangle join did not restore complete face/corner records and retained ID");
    auto reversed = EditableMeshSource{triangles};
    std::reverse(reversed.document["faces"].begin(), reversed.document["faces"].end());
    require(reversed.join_triangles({7, 5}) == 9, "Join retention depends on edge direction");
    std::reverse(reversed.document["faces"].begin(), reversed.document["faces"].end());
    require(reversed.document == inverse, "Join depends on source face array order");
    for (const auto edge :
         std::vector<std::array<std::uint32_t, 2>>{{0, 7}, {5, 5}, {5, 99}, {5, 6}, {3, 7}}) {
        auto bad = EditableMeshSource{triangles};
        rejects([&] { bad.join_triangles(edge); });
        require(bad.document == triangles, "Rejected edge join changed source");
    }
    for (unsigned conflict = 0; conflict < 3; ++conflict) {
        auto bad = EditableMeshSource{triangles};
        auto& other = bad.document["faces"][1];
        if (conflict == 0)
            other["corners"][0]["uv"][0] = 1e-10;
        else if (conflict == 1)
            other["corners"][0]["extension"] = "conflict";
        else
            other["extension"] = "conflicting face data";
        const auto safe = bad.document;
        rejects([&] { bad.join_triangles({5, 7}); });
        require(bad.document == safe, "Join discarded a UV/opaque-data conflict");
    }
    auto planar_pair = [] {
        auto mesh = uv_strip();
        mesh.document["faces"].erase(mesh.document["faces"].begin() + 1);
        mesh.triangulate_faces(std::array<std::uint32_t, 1>{7});
        return mesh;
    };
    auto flat = planar_pair();
    const auto flat_triangles = flat.cook();
    require(flat.join_triangles({1, 3}) == 7 && flat.document["faces"][0]["corners"].size() == 4,
            "Planar triangle pair did not join");
    const auto quad = flat.cook();
    const auto& a = flat_triangles.lods[0].parts[0];
    const auto& b = quad.lods[0].parts[0];
    require(a.indices.size() == b.indices.size(), "Join changed triangle count");
    for (const auto& stream : a.streams) {
        const auto* other = b.find(stream.semantic);
        require(other, "Join lost cooked attributes");
        const auto& expected = std::get<std::vector<float>>(stream.values);
        const auto& actual = std::get<std::vector<float>>(other->values);
        for (std::size_t i = 0; i < a.indices.size(); ++i)
            for (unsigned c = 0; c < stream.components; ++c)
                require(expected[a.indices[i] * stream.components + c] ==
                            actual[b.indices[i] * stream.components + c],
                        "Flat join changed the original indexed fan attributes");
    }
    for (unsigned shape = 0; shape < 2; ++shape) {
        auto bad = planar_pair();
        if (shape == 0)
            bad.document["vertices"][3]["position"][2] = .1;
        else
            bad.document["vertices"][2]["position"] = {.2, .2, 0.};
        bad.validate();
        const auto safe = bad.document;
        rejects([&] { bad.join_triangles({1, 3}); });
        require(bad.document == safe, "Nonplanar/concave triangle join changed source");
    }
    // Keeping the shared diagonal also works when the retained triangle starts
    // at its other vertex, rather than at a shared-edge endpoint.
    auto rotated = planar_pair();
    auto& corners = rotated.document["faces"][0]["corners"].get_ref<nlohmann::json::array_t&>();
    std::rotate(corners.begin(), corners.begin() + 1, corners.end());
    rotated.join_triangles({1, 3});
    const auto& loop = rotated.document["faces"][0]["corners"];
    require(loop[0]["vertex"] == 3 && loop[2]["vertex"] == 1,
            "Join changed the cooked diagonal when the original triangle anchor was not shared");

    auto smooth = EditableMeshSource::create_cube(AssetId::generate());
    smooth.document["vertices"] = nlohmann::json::array();
    const std::array<std::array<double, 3>, 7> positions{
        {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {-1, 0, 0}, {0, 1, 1}, {0, -1, 1}}};
    for (unsigned i = 0; i < positions.size(); ++i)
        smooth.document["vertices"].push_back({{"id", i + 1}, {"position", positions[i]}});
    smooth.document["faces"] = nlohmann::json::array();
    for (unsigned i = 0; i < 3; ++i)
        smooth.document["faces"].push_back({{"id", i + 8},
                                            {"corners",
                                             {{{"vertex", 1}, {"uv", {0, 0}}},
                                              {{"vertex", 2 * i + 2}, {"uv", {1, 0}}},
                                              {{"vertex", 2 * i + 3}, {"uv", {0, 1}}}}}});
    smooth.document["next_id"] = 11;
    smooth.set_smooth_shading(true);
    const auto safe = smooth.document;
    rejects([&] { smooth.delete_faces(std::array<std::uint32_t, 1>{10}); });
    require(smooth.document == safe, "Deletion accepted undefined smooth normals");
}
} // namespace
