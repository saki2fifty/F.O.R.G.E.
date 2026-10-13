#pragma once
#include "editable_mesh_uv_tests.hpp"
namespace {
void refinement_tests() {
    using Json = nlohmann::json;
    auto source = EditableMeshSource::create_cube(AssetId::generate());
    source.document["extension"] = "root retained";
    for (auto& vertex : source.document["vertices"]) {
        const auto p = vertex["position"].get<std::array<double, 3>>();
        vertex["color"] = {p[0] + .5, p[1] + .5, p[2] + .5, 1.};
        vertex["extension"] = "common vertex data";
    }
    for (auto& face : source.document["faces"]) {
        face["extension"] = face["id"];
        for (auto& corner : face["corners"])
            corner["extension"] = "common corner data";
    }
    const auto original = source.document;
    const auto children = source.split_faces_at_centers(std::array<std::uint32_t, 2>{13, 9});
    require(children == std::vector<std::uint32_t>{9, 16, 17, 18, 13, 20, 21, 22} &&
                source.document["vertices"].size() == 10 && source.document["faces"].size() == 12 &&
                source.document["next_id"] == 23,
            "Center split allocation/counts are not canonical");
    for (unsigned i = 0; i < 8; ++i)
        require(source.document["vertices"][i] == original["vertices"][i],
                "Center split changed original vertex data");
    for (const auto& old : original["faces"])
        if (old["id"] != 9 && old["id"] != 13)
            require(std::find(source.document["faces"].begin(), source.document["faces"].end(),
                              old) != source.document["faces"].end(),
                    "Center split changed unselected face");
    require(source.document["vertices"][8]["position"] == Json::array({0., 0., .5}) &&
                source.document["vertices"][9]["position"] == Json::array({0., .5, 0.}) &&
                source.document["vertices"][8]["color"] == Json::array({.5, .5, 1., 1.}),
            "Center position/color mean differs");
    require(source.document["faces"][0]["corners"][2]["uv"] == Json::array({.5, .5}) &&
                source.document["faces"][0]["extension"] == 9 &&
                source.document["faces"][0]["corners"][2]["extension"] == "common corner data" &&
                source.document["vertices"][8]["extension"] == "common vertex data",
            "Center interpolation lost UV or opaque fields");
    auto permuted = EditableMeshSource{original};
    permuted.split_faces_at_centers(std::array<std::uint32_t, 2>{9, 13});
    require(permuted.document == source.document, "Center split selection order changes source");
    require(source.cook().lods[0].parts[0].indices.size() == 48,
            "Center split cooked triangle count differs");
    for (const auto& selection : std::vector<std::vector<std::uint32_t>>{{}, {9, 9}, {99}, {0}}) {
        auto rejected = EditableMeshSource{original};
        rejects([&] { rejected.split_faces_at_centers(selection); });
        require(rejected.document == original, "Rejected center selection changed source");
    }
    auto subdivided = EditableMeshSource{original};
    const auto refined_faces = subdivided.subdivide();
    require(refined_faces.size() == 48 && subdivided.document["vertices"].size() == 26 &&
                subdivided.document["faces"].size() == 48 && subdivided.document["next_id"] == 75,
            "Uniform subdivision counts/shared allocator differ");
    for (unsigned i = 0; i < 8; ++i)
        require(subdivided.document["vertices"][i] == original["vertices"][i],
                "Subdivision changed original vertex records");
    std::set<std::array<double, 3>> positions;
    unsigned edge_centers = 0, face_centers = 0;
    for (const auto& vertex : subdivided.document["vertices"]) {
        const auto p = vertex["position"].get<std::array<double, 3>>();
        require(positions.insert(p).second, "Subdivision duplicates shared cube midpoints");
        unsigned zeros = 0;
        for (const auto lane : p) {
            require(lane == -.5 || lane == 0 || lane == .5, "Midpoint left cube linear grid");
            zeros += lane == 0;
        }
        edge_centers += zeros == 1;
        face_centers += zeros == 2;
        require(vertex["color"] == Json::array({p[0] + .5, p[1] + .5, p[2] + .5, 1.}) &&
                    vertex["extension"] == "common vertex data",
                "Midpoint color/opaque interpolation differs");
    }
    require(edge_centers == 12 && face_centers == 6, "Cube refinement midpoint geometry differs");
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::vector<bool>> edge_uses;
    std::map<std::uint32_t, std::set<std::array<double, 2>>> uvs;
    for (const auto& face : subdivided.document["faces"]) {
        require(face["corners"].size() == 3, "Subdivision did not produce triangles");
        const auto& corners = face["corners"];
        for (unsigned i = 0; i < 3; ++i) {
            const auto a = corners[i]["vertex"].get<std::uint32_t>();
            const auto b = corners[(i + 1) % 3]["vertex"].get<std::uint32_t>();
            edge_uses[std::minmax(a, b)].push_back(a < b);
            uvs[a].insert(corners[i]["uv"].get<std::array<double, 2>>());
            require(corners[i]["extension"] == "common corner data",
                    "Midpoint corner lost metadata");
        }
    }
    for (const auto& [edge, uses] : edge_uses)
        require(uses.size() == 2 && uses[0] != uses[1],
                "Subdivision opened or reversed a cube edge");
    require(subdivided.connected_faces(9).size() == 48,
            "Shared subdivision disconnected the closed surface");
    require(
        std::any_of(uvs.begin(), uvs.end(),
                    [](const auto& item) { return item.first >= 15 && item.second.size() > 1; }),
        "Subdivision merged corner UV seams");
    const auto& first = subdivided.document["faces"][0]["corners"];
    require(first[0] == original["faces"][0]["corners"][0] &&
                first[1]["uv"] == Json::array({.5, 0.}) && first[2]["uv"] == Json::array({.5, .5}),
            "First fan triangle midpoint UV interpolation differs");
    for (unsigned i = 0; i < 6; ++i)
        require(subdivided.document["faces"][i * 8]["id"] == i + 9 &&
                    subdivided.document["faces"][i * 8]["extension"] == i + 9,
                "Subdivision did not preserve original face identity/metadata");
    require(subdivided.cook().lods[0].parts[0].indices.size() == 144,
            "Subdivided cook triangle count differs");
    subdivided.subdivide();
    require(subdivided.document["faces"].size() == 192 &&
                subdivided.document["vertices"].size() == 98,
            "Repeated subdivision did not retain shared connectivity");
    subdivided.set_smooth_shading(true);
    (void)subdivided.cook();
    auto smooth = EditableMeshSource{original};
    smooth.set_smooth_shading(true);
    smooth.split_faces_at_centers(std::array<std::uint32_t, 1>{9});
    (void)smooth.cook();
    auto missing = EditableMeshSource{original};
    for (auto& vertex : missing.document["vertices"])
        vertex.erase("color");
    missing.document["vertices"][4]["color"] = {0., 0., 0., 0.};
    missing.split_faces_at_centers(std::array<std::uint32_t, 1>{9});
    require(missing.document["vertices"][8]["color"] == Json::array({.75, .75, .75, .75}),
            "Missing colors did not use the cooked white default");
    for (const bool centers : {false, true}) {
        for (const bool corner : {false, true}) {
            auto conflict = EditableMeshSource{original};
            if (corner)
                conflict.document["faces"][0]["corners"][0]["extension"] = "conflict";
            else
                conflict.document["vertices"][4]["extension"] = "conflict";
            const auto before = conflict.document;
            rejects([&] {
                if (centers)
                    conflict.split_faces_at_centers(std::array<std::uint32_t, 1>{9});
                else
                    conflict.subdivide();
            });
            require(conflict.document == before, "Opaque conflict lost original source");
        }
        auto exhausted = EditableMeshSource{original};
        exhausted.document["next_id"] = std::numeric_limits<std::uint32_t>::max() - 1;
        const auto before = exhausted.document;
        rejects([&] {
            if (centers)
                exhausted.split_faces_at_centers(std::array<std::uint32_t, 1>{9});
            else
                exhausted.subdivide();
        });
        require(exhausted.document == before, "Refinement allocator overflow mutated source");
        auto maximum = uv_grid(64);
        const auto bounded = maximum.document;
        rejects([&] {
            if (centers)
                maximum.split_faces_at_centers(std::array<std::uint32_t, 1>{4097});
            else
                maximum.subdivide();
        });
        require(maximum.document == bounded, "Refinement profile overflow mutated source");
    }
    // Nine fully colored corners exercise a mean without rounded weight accumulation.
    auto polygon = EditableMeshSource::create_cube(AssetId::generate());
    polygon.document["vertices"] = Json::array();
    auto corners = Json::array();
    for (unsigned i = 0; i < 9; ++i) {
        const double angle = double(i) * 2 * std::numbers::pi / 9;
        polygon.document["vertices"].push_back(
            {{"id", i + 1},
             {"position", {std::cos(angle), std::sin(angle), 0.}},
             {"color", {1., 1., 1., 1.}}});
        corners.push_back({{"vertex", i + 1}, {"uv", {0., 0.}}});
    }
    polygon.document["faces"] = Json::array({{{"id", 10}, {"corners", corners}}});
    polygon.document["next_id"] = 11;
    polygon.split_faces_at_centers(std::array<std::uint32_t, 1>{10});
    require(polygon.document["vertices"].back()["color"] == Json::array({1., 1., 1., 1.}),
            "Center white color mean left its valid range");
    (void)polygon.cook();
    // Valid source triangles may become too small for existing geometry admission.
    auto thin = EditableMeshSource::create_cube(AssetId::generate());
    thin.document["vertices"] = Json::array({{{"id", 1}, {"position", {0., 0., 0.}}},
                                             {{"id", 2}, {"position", {.0002, 0., 0.}}},
                                             {{"id", 3}, {"position", {0., .0001, 0.}}}});
    thin.document["faces"] =
        Json::array({{{"id", 4},
                      {"corners", Json::array({{{"vertex", 1}, {"uv", {0., 0.}}},
                                               {{"vertex", 2}, {"uv", {1., 0.}}},
                                               {{"vertex", 3}, {"uv", {0., 1.}}}})}}});
    thin.document["next_id"] = 5;
    thin.validate();
    const auto small = thin.document;
    rejects([&] { thin.subdivide(); });
    require(thin.document == small, "Rejected small subdivision changed source");
    rejects([&] { thin.split_faces_at_centers(std::array<std::uint32_t, 1>{4}); });
    require(thin.document == small, "Rejected small center split changed source");
}
} // namespace
