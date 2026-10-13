#pragma once
#include "material_graph_layout.hpp"
#include "material_graph_threshold_tests.hpp"
namespace forge::test {
inline void material_graph_layout_tests() {
    auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    auto graph = graph_tests::threshold_graph("smoothstep", "vector3",
                                              {{0, 0, 0}, {1, 1, 1}, {.25, .5, .75}});
    std::set<GraphNodeId> ids;
    for (std::size_t i = 0; i < graph["nodes"].size(); ++i) {
        graph["nodes"][i]["position"] = {double(i) * 70 - 33, double(i * i) * 17 + 9};
        graph["nodes"][i]["opaque"] = {{"position", {1, 2}}, {"keep", "layout"}};
        ids.insert(graph["nodes"][i]["id"].get<GraphNodeId>());
    }
    const auto original = graph;
    const auto program = compile_material_graph(graph);
    for (unsigned axis : {0u, 1u})
        for (const auto action : {GraphLayout::Start, GraphLayout::Center, GraphLayout::End,
                                  GraphLayout::Distribute, GraphLayout::Grid}) {
            auto next = original;
            arrange_material_graph(next, ids, action, axis, 16);
            check(material_graph_layout_equivalent(original, next),
                  "Arrangement changed more than positions");
            const auto projection = compile_material_graph(next);
            check(projection.source == program.source && projection.surface == program.surface,
                  "Layout changed actual shader projection/interface");
            double low = 1e9, high = -1e9;
            for (const auto& n : original["nodes"]) {
                const auto p = n["position"][axis].get<double>();
                low = std::min(low, p);
                high = std::max(high, p + material_graph_node_size(n)[axis]);
            }
            std::vector<double> centers;
            for (std::size_t i = 0; i < next["nodes"].size(); ++i) {
                const auto& n = next["nodes"][i];
                const auto p = n["position"][axis].get<double>();
                const auto size = material_graph_node_size(n)[axis];
                if (action == GraphLayout::Start)
                    check(p == low, "Start alignment differs");
                if (action == GraphLayout::Center)
                    check(std::abs(p + size / 2 - (low + high) / 2) < 1e-8,
                          "Center alignment differs");
                if (action == GraphLayout::End)
                    check(p + size == high, "End alignment differs");
                if (action == GraphLayout::Grid)
                    for (unsigned lane : {0u, 1u}) {
                        const double value = n["position"][lane];
                        check(std::abs(std::remainder(value, 16)) < 1e-8,
                              "Graph origin grid differs");
                    }
                if (action == GraphLayout::Distribute) {
                    centers.push_back(p + size / 2);
                }
            }
            if (!centers.empty()) {
                std::sort(centers.begin(), centers.end());
                const double delta = centers[1] - centers[0];
                for (std::size_t i = 2; i < centers.size(); ++i)
                    check(std::abs(centers[i] - centers[i - 1] - delta) < 1e-8,
                          "Node centers are not evenly spaced");
            }
        }
    std::set<GraphNodeId> pure = ids;
    pure.erase(graph["nodes"][0]["id"].get<GraphNodeId>());
    auto function_graph = original;
    extract_material_graph_function(function_graph, pure, "Layout function");
    const auto function_before = function_graph;
    auto& body = function_graph["functions"][0]["graph"];
    std::set<GraphNodeId> members;
    for (const auto& n : body["nodes"])
        members.insert(n["id"].get<GraphNodeId>());
    arrange_material_graph(body, members, GraphLayout::Grid, 0, 16);
    check(material_graph_layout_equivalent(function_before, function_graph),
          "Function layout classified as program change");
    const auto old_function = compile_material_graph(function_before),
               new_function = compile_material_graph(function_graph);
    check(old_function.source == new_function.source &&
              old_function.surface == new_function.surface,
          "Function layout changed actual projection");
    for (unsigned mutation = 0; mutation < 5; ++mutation) {
        auto changed = function_before;
        if (mutation == 0)
            changed["nodes"][0]["opaque"]["position"] = {3, 4};
        if (mutation == 1)
            changed["functions"][0]["label"] = "Renamed";
        if (mutation == 2)
            changed["functions"][0]["graph"]["nodes"][0]["data"]["opaque"] = 1;
        if (mutation == 3)
            changed["nodes"][0]["id"] = GraphNodeId::generate();
        if (mutation == 4)
            changed["edges"][0]["opaque"] = true;
        check(!material_graph_layout_equivalent(function_before, changed),
              "Non-position field incorrectly ignored by preview classifier");
    }
    auto grid = original;
    const auto one = grid["nodes"][0]["id"].get<GraphNodeId>();
    grid["nodes"][0]["position"] = {-8, 8};
    const auto grid_before = grid;
    arrange_material_graph(grid, {one}, GraphLayout::Grid, 0, 16);
    check(grid["nodes"][0]["position"] == nlohmann::json::array({-16, 16}),
          "Positive/negative graph grid half ties differ");
    for (std::size_t i = 1; i < grid["nodes"].size(); ++i)
        check(grid["nodes"][i] == grid_before["nodes"][i], "Grid touched unselected nodes");
    check(!arrange_material_graph(grid, {one}, GraphLayout::Grid, 0, 16),
          "Already aligned graph grid is not a no-op");
    auto reject = [&](auto op) {
        const auto before = grid;
        bool failed = false;
        try {
            op(grid);
        } catch (const std::exception&) {
            failed = true;
        }
        check(failed && grid == before, "Rejected graph layout partially committed");
    };
    reject([&](auto& g) { arrange_material_graph(g, {}, GraphLayout::Grid); });
    reject(
        [&](auto& g) { arrange_material_graph(g, {GraphNodeId::generate()}, GraphLayout::Start); });
    reject([&](auto& g) { arrange_material_graph(g, {one}, GraphLayout::Start, 2); });
    reject([&](auto& g) { arrange_material_graph(g, {one}, GraphLayout::Distribute); });
    for (double spacing : {0., .5, 1025., std::numeric_limits<double>::infinity()})
        reject([&](auto& g) { arrange_material_graph(g, {one}, GraphLayout::Grid, 0, spacing); });
    grid["nodes"][0]["position"] = {1000000, 0};
    reject([&](auto& g) { arrange_material_graph(g, {one}, GraphLayout::Grid, 0, 1024); });
    auto max = create_material_graph();
    while (max["nodes"].size() < 1024) {
        auto node = create_material_graph_node("constant", {9, -9});
        node["type"] = "unavailable.layout.test";
        node["opaque"] = {{"keep", 1}};
        max["nodes"].push_back(node);
    }
    std::set<GraphNodeId> all;
    for (const auto& n : max["nodes"])
        all.insert(n["id"].get<GraphNodeId>());
    const auto max_before = max;
    arrange_material_graph(max, all, GraphLayout::Grid);
    check(material_graph_layout_equivalent(max_before, max) && max["nodes"].size() == 1024,
          "Maximum-profile layout lost unknown data/identities");
}
} // namespace forge::test
