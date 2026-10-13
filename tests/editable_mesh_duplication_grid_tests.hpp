#pragma once
#include "editable_mesh_uv_tests.hpp"
namespace {
void duplication_grid_tests() {
    auto source = EditableMeshSource::create_cube(AssetId::generate());
    source.document["extension"] = "source preserved";
    for (auto& vertex : source.document["vertices"]) {
        vertex["color"] = {.2, .4, .6, 1.};
        vertex["extension"] = vertex["id"];
    }
    for (auto& face : source.document["faces"]) {
        face["extension"] = face["id"];
        for (auto& corner : face["corners"])
            corner["extension"] = corner["vertex"];
    }
    const auto original = source.document;
    const std::array<std::uint32_t, 2> selected{9, 13};
    const auto copies = source.duplicate_faces(selected, {2, .25, -.5});
    require(copies == std::vector<std::uint32_t>{21, 22} && source.document["next_id"] == 23 &&
                source.document["vertices"].size() == 14 && source.document["faces"].size() == 8,
            "Group duplication did not copy shared vertices once or allocate canonical IDs");
    const std::map<std::uint32_t, std::uint32_t> mapping{{3, 15}, {4, 16}, {5, 17},
                                                         {6, 18}, {7, 19}, {8, 20}};
    for (unsigned i = 0; i < 8; ++i)
        require(source.document["vertices"][i] == original["vertices"][i],
                "Duplication changed original vertices");
    for (unsigned i = 0; i < 6; ++i)
        require(source.document["faces"][i] == original["faces"][i],
                "Duplication changed original faces");
    for (unsigned i = 0; i < 6; ++i) {
        auto expected = original["vertices"][i + 2];
        expected["id"] = i + 15;
        for (unsigned axis = 0; axis < 3; ++axis)
            expected["position"][axis] =
                expected["position"][axis].get<double>() + std::array<double, 3>{2, .25, -.5}[axis];
        require(source.document["vertices"][i + 8] == expected,
                "Duplication lost vertex colors/opaque data or offset");
    }
    for (unsigned i = 0; i < 2; ++i) {
        auto expected = original["faces"][i ? 4 : 0];
        expected["id"] = copies[i];
        for (auto& corner : expected["corners"])
            corner["vertex"] = mapping.at(corner["vertex"].get<std::uint32_t>());
        require(source.document["faces"][i + 6] == expected,
                "Duplication lost corner UVs/metadata/shared connectivity");
    }
    require(source.connected_faces(21) == copies &&
                source.cook().lods[0].parts[0].indices.size() == 48 &&
                source.document["extension"] == original["extension"],
            "Copies connect to originals or changed source/cooked shape");
    auto permuted = EditableMeshSource{original};
    permuted.duplicate_faces(std::array<std::uint32_t, 2>{13, 9}, {2, .25, -.5});
    require(permuted.document == source.document, "Selection order changed duplicate allocation");
    for (const auto& faces : std::vector<std::vector<std::uint32_t>>{{}, {9, 9}, {0}, {99}}) {
        auto invalid = EditableMeshSource{original};
        rejects([&] { invalid.duplicate_faces(faces, {}); });
        require(invalid.document == original, "Rejected duplicate selection changed source");
    }
    auto invalid = EditableMeshSource{original};
    rejects([&] { invalid.duplicate_faces(selected, {100000, 0, 0}); });
    rejects([&] {
        invalid.duplicate_faces(selected, {std::numeric_limits<double>::infinity(), 0, 0});
    });
    require(invalid.document == original, "Rejected duplicate offset changed source");
    invalid.document["next_id"] = std::numeric_limits<std::uint32_t>::max() - 7;
    const auto exhausted = invalid.document;
    rejects([&] { invalid.duplicate_faces(selected, {}); });
    require(invalid.document == exhausted, "Duplicate identity overflow changed source");
    auto maximum = uv_grid(64);
    const auto full = maximum.document;
    rejects([&] { maximum.duplicate_faces(std::array<std::uint32_t, 1>{4097}, {}); });
    require(maximum.document == full, "Maximum-profile duplicate changed source");

    const std::array<std::uint32_t, 8> vertices{1, 2, 3, 4, 5, 6, 7, 8};
    auto snapped = EditableMeshSource{original};
    snapped.snap_vertices(vertices, 1, {true, false, false});
    auto expected = original;
    for (auto& vertex : expected["vertices"])
        vertex["position"][0] = vertex["position"][0].get<double>() * 2;
    require(snapped.document == expected, "Positive/negative grid half ties or axis scope differ");
    const auto once = snapped.document;
    snapped.snap_vertices(vertices, 1, {true, false, false});
    require(snapped.document == once, "Aligned grid selection is not a no-op");
    auto moved = EditableMeshSource{original};
    moved.translate_vertices(vertices, {.1, .1, .1});
    moved.snap_vertices(vertices, .25);
    require(moved.document == original, "Grid snap did not restore off-grid coordinates");
    const auto safe = moved.document;
    for (const auto spacing : {0., -1., 1e-7, 100001., std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN()}) {
        rejects([&] { moved.snap_vertices(vertices, spacing); });
        require(moved.document == safe, "Rejected grid spacing changed source");
    }
    for (const auto& ids :
         std::vector<std::vector<std::uint32_t>>{{}, {1, 1}, {0}, {99}, {1, 99}}) {
        rejects([&] { moved.snap_vertices(ids, .25); });
        require(moved.document == safe, "Rejected grid selection changed source");
    }
    rejects([&] { moved.snap_vertices(vertices, .25, {false, false, false}); });
    rejects([&] { moved.snap_vertices(vertices, 2); });
    rejects([&] { moved.snap_vertices(std::array<std::uint32_t, 1>{7}, 1); });
    require(moved.document == safe, "Collapsed/nonplanar grid snap changed source");
    // Loose selected vertices do not affect unrelated surfaces or corner data.
    auto loose = EditableMeshSource{original};
    loose.document["vertices"].push_back(
        {{"id", 15}, {"position", {.12, -.38, .26}}, {"extra", 42}});
    loose.document["next_id"] = 16;
    auto isolated = loose.document;
    isolated["vertices"][8]["position"] = {0., -.5, .25};
    loose.snap_vertices(std::array<std::uint32_t, 1>{15}, .25);
    require(loose.document == isolated, "Grid snap changed unselected source records");
    std::vector<std::uint32_t> all;
    for (const auto& vertex : full["vertices"])
        all.push_back(vertex["id"]);
    maximum.snap_vertices(all, 1);
    require(maximum.document == full, "Maximum aligned profile changed through snap");
}
} // namespace
