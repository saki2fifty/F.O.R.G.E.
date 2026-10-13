#pragma once
#include "editable_mesh_uv_tests.hpp"
namespace {
void uv_packing_tests() {
    auto source = EditableMeshSource::create_cube(AssetId::generate());
    auto& first = source.document["faces"][0];
    first["extension"] = {{"retain", 42}};
    first["corners"][0]["extension"] = "retain corner";
    source.document["vertices"][0]["color"] = {.2, .4, .6, 1.};
    const auto original = source.document;
    const auto islands = source.uv_islands();
    require(islands.size() == 6, "Cube UV seams did not make six canonical islands");
    std::vector<std::uint32_t> faces;
    for (const auto& face : original.at("faces"))
        faces.push_back(face.at("id").get<std::uint32_t>());
    source.pack_uv_islands(faces, .02);
    const auto packed = source.document;
    require(source.uv_islands() == islands, "UV packing joined or split authored islands");
    auto preserved = packed;
    for (unsigned f = 0; f < 6; ++f)
        for (unsigned c = 0; c < 4; ++c)
            preserved["faces"][f]["corners"][c]["uv"] = original["faces"][f]["corners"][c]["uv"];
    require(preserved == original, "Packing changed geometry, IDs, color or opaque fields");
    struct Box {
        double min_x, min_y, max_x, max_y;
    };
    std::vector<Box> boxes;
    for (const auto& face : packed.at("faces")) {
        Box box{1, 1, 0, 0};
        for (const auto& corner : face.at("corners")) {
            const auto x = corner.at("uv")[0].get<double>(), y = corner.at("uv")[1].get<double>();
            require(x >= .02 - 1e-12 && x <= .98 + 1e-12 && y >= .02 - 1e-12 && y <= .98 + 1e-12,
                    "Packed island crossed padded tile border");
            box.min_x = std::min(box.min_x, x);
            box.max_x = std::max(box.max_x, x);
            box.min_y = std::min(box.min_y, y);
            box.max_y = std::max(box.max_y, y);
        }
        uv_near(box.max_x - box.min_x, box.max_y - box.min_y);
        if (!boxes.empty())
            uv_near(box.max_x - box.min_x, boxes[0].max_x - boxes[0].min_x);
        for (const auto& other : boxes)
            require(box.min_x >= other.max_x + .02 - 1e-12 ||
                        other.min_x >= box.max_x + .02 - 1e-12 ||
                        box.min_y >= other.max_y + .02 - 1e-12 ||
                        other.min_y >= box.max_y + .02 - 1e-12,
                    "Packed UV rectangles overlap or omit padding");
        boxes.push_back(box);
    }
    auto permuted = EditableMeshSource{original};
    std::reverse(faces.begin(), faces.end());
    std::reverse(permuted.document["faces"].begin(), permuted.document["faces"].end());
    permuted.pack_uv_islands(faces, .02);
    std::reverse(permuted.document["faces"].begin(), permuted.document["faces"].end());
    require(permuted.document == packed, "Packing depends on seed/source array order");
    auto connected = uv_strip();
    const auto strip = connected.document;
    connected.pack_uv_islands(std::array<std::uint32_t, 1>{8}, .1);
    require(connected.uv_island(7).size() == 8, "Seed packing broke its connected island");
    uv_near(connected.document["faces"][0]["corners"][0]["uv"][0], .1);
    uv_near(connected.document["faces"][1]["corners"][1]["uv"][0], .9);
    uv_near(connected.document["faces"][0]["corners"][2]["uv"][0],
            connected.document["faces"][1]["corners"][3]["uv"][0]);
    require(connected.document["vertices"] == strip["vertices"],
            "Island expansion changed geometry");
    auto selected = EditableMeshSource{original};
    selected.pack_uv_islands(std::array<std::uint32_t, 1>{faces[0]}, .01);
    for (const auto& face : selected.document["faces"])
        if (face.at("id") != faces[0])
            for (const auto& prior : original.at("faces"))
                if (prior.at("id") == face.at("id"))
                    require(prior == face, "Selected packing moved unrelated island");
    auto tall = uv_strip();
    for (auto& face : tall.document["faces"])
        for (auto& corner : face["corners"]) {
            corner["uv"][0] = corner["uv"][0].get<double>() * .1;
            corner["uv"][1] = corner["uv"][1].get<double>() * 2;
        }
    const auto tall_original = tall.document;
    tall.pack_uv_islands(std::array<std::uint32_t, 1>{7}, .1, true);
    const auto& a = tall.document["faces"][0]["corners"];
    uv_near(a[0]["uv"][0], .9);
    uv_near(a[0]["uv"][1], .1);
    uv_near(a[1]["uv"][0], .9);
    uv_near(a[1]["uv"][1], .14);
    uv_near(a[3]["uv"][0], .1);
    uv_near(a[3]["uv"][1], .1);
    require(tall.uv_island(7).size() == 8, "Quarter-turn packing changed island seams");
    auto tiny = EditableMeshSource{tall_original};
    for (auto& face : tiny.document["faces"])
        for (auto& corner : face["corners"])
            for (auto& value : corner["uv"])
                value = value.get<double>() * 1e-300;
    tiny.pack_uv_islands(std::array<std::uint32_t, 1>{7}, .1, true);
    for (unsigned f = 0; f < 2; ++f)
        for (unsigned c = 0; c < 4; ++c)
            for (unsigned axis = 0; axis < 2; ++axis)
                uv_near(tiny.document["faces"][f]["corners"][c]["uv"][axis],
                        tall.document["faces"][f]["corners"][c]["uv"][axis]);
    auto invalid = EditableMeshSource{original};
    for (const auto padding : {-1., 0., .251, std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN()})
        rejects([&] { invalid.pack_uv_islands(faces, padding); });
    rejects([&] { invalid.pack_uv_islands(std::span<const std::uint32_t>{}); });
    rejects([&] { invalid.pack_uv_islands(std::array<std::uint32_t, 1>{999}); });
    rejects([&] { invalid.pack_uv_islands(std::array<std::uint32_t, 2>{faces[0], faces[0]}); });
    require(invalid.document == original, "Rejected packing mutated source");
    for (auto& corner : invalid.document["faces"][0]["corners"])
        corner["uv"] = {0, 0};
    const auto degenerate = invalid.document;
    rejects([&] { invalid.pack_uv_islands(faces); });
    require(invalid.document == degenerate, "Degenerate packing partially committed");
    auto collapsed = EditableMeshSource{original};
    for (auto& corner : collapsed.document["faces"][0]["corners"])
        for (auto& value : corner["uv"])
            value = value.get<double>() * 1e-300;
    const auto collapse_before = collapsed.document;
    rejects([&] { collapsed.pack_uv_islands(faces); });
    require(collapsed.document == collapse_before,
            "Unrepresentable relative island scale committed");
    auto grid = uv_grid(64);
    for (auto& face : grid.document["faces"])
        for (unsigned c = 0; c < 4; ++c)
            face["corners"][c]["uv"] =
                std::array<std::array<double, 2>, 4>{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}}[c];
    faces.clear();
    for (const auto& face : grid.document["faces"])
        faces.push_back(face.at("id").get<std::uint32_t>());
    require(grid.uv_islands().size() == 3969, "Maximum profile did not retain per-face islands");
    const auto grid_original = grid.document;
    rejects([&] { grid.pack_uv_islands(faces, .25); });
    require(grid.document == grid_original, "Impossible padding changed source");
    grid.pack_uv_islands(faces, .001);
    require(grid.uv_islands().size() == 3969 &&
                grid.document["vertices"] == grid_original["vertices"] &&
                grid.document["next_id"] == grid_original["next_id"],
            "Maximum profile packing changed topology or seams");
    const auto cooked = decode_mesh(encode_mesh(source.cook()));
    const auto& stream =
        std::get<std::vector<float>>(cooked.lods[0].parts[0].find("TEXCOORD_0")->values);
    require(std::all_of(stream.begin(), stream.end(),
                        [](float value) { return value >= .02f - 1e-6f && value <= .98f + 1e-6f; }),
            "Packed UVs did not survive cooked mesh encoding");
}
} // namespace
