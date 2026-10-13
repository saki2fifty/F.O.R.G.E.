#pragma once
#include "editable_mesh_uv_tests.hpp"
namespace {
void uv_arrangement_tests() {
    auto source = uv_strip();
    source.document["faces"][0]["corners"][0]["extension"] = "keep";
    source.document["vertices"][0]["color"] = {.1, .2, .3, 1.};
    const auto original = source.document;
    const auto corners = source.uv_island(7);
    for (unsigned axis = 0; axis < 2; ++axis)
        for (const auto alignment :
             {MeshUvAlignment::Minimum, MeshUvAlignment::Center, MeshUvAlignment::Maximum}) {
            auto aligned = source;
            aligned.align_uv(corners, axis, alignment);
            const auto expected = axis == 0 ? (alignment == MeshUvAlignment::Minimum  ? 0.
                                               : alignment == MeshUvAlignment::Center ? 1.
                                                                                      : 2.)
                                            : (alignment == MeshUvAlignment::Minimum  ? 0.
                                               : alignment == MeshUvAlignment::Center ? .5
                                                                                      : 1.);
            for (unsigned f = 0; f < 2; ++f)
                for (unsigned c = 0; c < 4; ++c) {
                    uv_near(aligned.document["faces"][f]["corners"][c]["uv"][axis], expected);
                    require(aligned.document["faces"][f]["corners"][c]["uv"][1 - axis] ==
                                original["faces"][f]["corners"][c]["uv"][1 - axis],
                            "Alignment changed the other UV coordinate");
                    aligned.document["faces"][f]["corners"][c]["uv"][axis] =
                        original["faces"][f]["corners"][c]["uv"][axis];
                }
            require(aligned.document == original, "Alignment changed geometry/color/opaque fields");
        }
    source.document["faces"][0]["corners"][1]["uv"][0] = .2;
    source.document["faces"][0]["corners"][2]["uv"][0] = .2;
    source.document["faces"][1]["corners"][0]["uv"][0] = .2;
    source.document["faces"][1]["corners"][3]["uv"][0] = .2;
    const auto irregular = source.document;
    source.distribute_uv(corners, 0);
    for (const auto corner : std::array<MeshUvCorner, 4>{{{7, 1}, {7, 2}, {8, 0}, {8, 3}}})
        for (const auto& face : source.document["faces"])
            if (face.at("id") == corner.face)
                uv_near(face.at("corners")[corner.corner].at("uv")[0], 1.);
    require(source.uv_island(7).size() == 8, "Spacing split shared endpoint levels");
    auto permuted = EditableMeshSource{irregular};
    auto reversed = corners;
    std::reverse(reversed.begin(), reversed.end());
    permuted.distribute_uv(reversed, 0);
    require(permuted.document == source.document, "Spacing depends on selection order");
    auto subset = EditableMeshSource{irregular};
    subset.align_uv(std::array<MeshUvCorner, 2>{{{7, 0}, {7, 1}}}, 0, MeshUvAlignment::Maximum);
    require(subset.document["faces"][1] == irregular["faces"][1],
            "Selected alignment edited another face");
    auto no_op = EditableMeshSource{original};
    no_op.distribute_uv(std::array<MeshUvCorner, 1>{{{7, 0}}}, 0);
    no_op.distribute_uv(std::array<MeshUvCorner, 2>{{{7, 0}, {7, 1}}}, 0);
    require(no_op.document == original, "One/two level spacing is not a no-op");
    auto invalid = EditableMeshSource{original};
    rejects([&] { invalid.align_uv(corners, 2, MeshUvAlignment::Center); });
    rejects([&] { invalid.align_uv(corners, 0, MeshUvAlignment(255)); });
    rejects([&] { invalid.distribute_uv(corners, 2); });
    for (const auto selection :
         {std::vector<MeshUvCorner>{}, std::vector<MeshUvCorner>{{7, 0}, {7, 0}},
          std::vector<MeshUvCorner>{{999, 0}}, std::vector<MeshUvCorner>{{7, 4}}}) {
        rejects([&] { invalid.align_uv(selection, 0, MeshUvAlignment::Center); });
        rejects([&] { invalid.distribute_uv(selection, 0); });
    }
    require(invalid.document == original, "Rejected arrangement partially replaced source");
    auto vertical = EditableMeshSource{irregular};
    for (auto& face : vertical.document["faces"])
        for (auto& corner : face["corners"])
            std::swap(corner["uv"][0], corner["uv"][1]);
    vertical.distribute_uv(corners, 1);
    for (unsigned f = 0; f < 2; ++f)
        for (unsigned c = 0; c < 4; ++c)
            uv_near(vertical.document["faces"][f]["corners"][c]["uv"][1],
                    source.document["faces"][f]["corners"][c]["uv"][0]);
    auto grid = uv_grid(64);
    std::vector<MeshUvCorner> all;
    for (const auto& face : grid.document["faces"])
        for (std::size_t i = 0; i < face.at("corners").size(); ++i)
            all.push_back({face.at("id").get<std::uint32_t>(), i});
    const auto geometry = grid.document["vertices"];
    grid.distribute_uv(all, 0);
    grid.align_uv(all, 1, MeshUvAlignment::Center);
    require(grid.document["vertices"] == geometry && all.size() == 15876,
            "Maximum profile arrangement changed geometry");
    grid.cook();
}
} // namespace
