#pragma once
#include <limits>
#include <numbers>
namespace {
EditableMeshSource uv_strip() {
    auto source = EditableMeshSource::create_cube(AssetId::generate());
    source.document["vertices"] = nlohmann::json::array();
    const std::array<std::array<double, 3>, 6> positions{
        {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {2, 0, 0}, {2, 1, 0}}};
    for (unsigned i = 0; i < positions.size(); ++i)
        source.document["vertices"].push_back(
            {{"id", i + 1}, {"position", positions[i]}, {"color", {.2, .4, .6, 1.}}});
    source.document["faces"] = nlohmann::json::array();
    const std::array<std::array<unsigned, 4>, 2> loops{{{1, 2, 3, 4}, {2, 5, 6, 3}}};
    for (unsigned i = 0; i < 2; ++i) {
        auto corners = nlohmann::json::array();
        for (const auto vertex : loops[i])
            corners.push_back(
                {{"vertex", vertex}, {"uv", {positions[vertex - 1][0], positions[vertex - 1][1]}}});
        source.document["faces"].push_back(
            {{"id", i + 7}, {"corners", corners}, {"extension", {{"keep", 42}}}});
    }
    source.document["next_id"] = 9;
    source.validate();
    return source;
}
void uv_near(double a, double b) { require(std::abs(a - b) < 1e-10, "Analytic UV value differs"); }
EditableMeshSource uv_grid(unsigned side) {
    auto source = EditableMeshSource::create_cube(AssetId::generate());
    source.document["vertices"] = nlohmann::json::array();
    source.document["faces"] = nlohmann::json::array();
    for (unsigned y = 0; y < side; ++y)
        for (unsigned x = 0; x < side; ++x)
            source.document["vertices"].push_back(
                {{"id", y * side + x + 1}, {"position", {double(x), double(y), 0.}}});
    unsigned id = side * side + 1;
    for (unsigned y = 0; y + 1 < side; ++y)
        for (unsigned x = 0; x + 1 < side; ++x) {
            auto corners = nlohmann::json::array();
            for (auto [cx, cy] : std::array<std::array<unsigned, 2>, 4>{
                     {{x, y}, {x + 1, y}, {x + 1, y + 1}, {x, y + 1}}})
                corners.push_back({{"vertex", cy * side + cx + 1},
                                   {"uv", {double(cx) / (side - 1), double(cy) / (side - 1)}}});
            source.document["faces"].push_back({{"id", id++}, {"corners", corners}});
        }
    source.document["next_id"] = id;
    source.validate();
    return source;
}
void uv_max_profile_tests() {
    auto source = uv_grid(64);
    const auto original = source.document;
    const auto island = source.uv_island(4097);
    const auto bytes = source.document.dump();
    require(EditableMeshSource::parse(std::as_bytes(std::span(bytes))).document == original,
            "Maximum-profile Mesh cannot reopen through its bounded source parser");
    require(island.size() == 15876 && source.document.at("vertices").size() == 4096 &&
                source.document.dump().size() < 2 * 1024 * 1024,
            "Maximum-vertex UV fixture exceeds the supported source profile");
    source.transform_uv(island, {.2, -.1}, .3, {-2, .5});
    source.fit_uv(island, .02);
    require(source.uv_island(4097) == island, "Large UV operations broke shared endpoint seams");
    auto preserved = source.document;
    for (unsigned f = 0; f < 3969; ++f)
        for (unsigned c = 0; c < 4; ++c)
            preserved["faces"][f]["corners"][c]["uv"] = original["faces"][f]["corners"][c]["uv"];
    require(preserved == original, "Large UV operations changed source identities or geometry");
    auto over_budget = EditableMeshSource::create_cube(AssetId::generate()).document.dump();
    over_budget.pop_back();
    over_budget += ",\"extension\":[";
    for (unsigned i = 0; i < 500001; ++i)
        over_budget += i ? ",0" : "0";
    over_budget += "]}";
    require(over_budget.size() < editable_mesh_source_byte_limit,
            "Parser fixture exceeded byte bound");
    rejects([&] { EditableMeshSource::parse(std::as_bytes(std::span(over_budget))); });
    const auto safe = source.document;
    auto oversized = island;
    oversized.resize(16385);
    rejects([&] { source.transform_uv(oversized, {}, 0, {1, 1}); });
    rejects([&] { source.fit_uv(oversized); });
    require(source.document == safe, "Oversized UV selection mutated source");
}
void mesh_uv_tests() {
    uv_max_profile_tests();
    auto source = uv_strip();
    const auto original = source.document;
    const auto all = source.uv_island(7);
    require(all ==
                std::vector<MeshUvCorner>{
                    {7, 0}, {7, 1}, {7, 2}, {7, 3}, {8, 0}, {8, 1}, {8, 2}, {8, 3}},
            "Connected UV island differs");
    require(source.uv_island(8) == all && source.document == original,
            "UV discovery mutates source or depends on seed order");
    auto split = source;
    split.document["faces"][1]["corners"][0]["uv"][0] = 1. + 1e-10;
    require(split.uv_island(7).size() == 4, "Exact authored UV seam was implicitly welded");
    auto disjoint = source;
    // An overlapping UV triangle with disconnected geometric vertex identities.
    for (unsigned i = 0; i < 3; ++i) {
        auto v = source.document["vertices"][i];
        v["id"] = i + 9;
        disjoint.document["vertices"].push_back(v);
    }
    auto disconnected = nlohmann::json::array();
    for (unsigned i = 0; i < 3; ++i) {
        auto corner = source.document["faces"][0]["corners"][i];
        corner["vertex"] = i + 9;
        disconnected.push_back(corner);
    }
    disjoint.document["faces"].push_back({{"id", 12}, {"corners", disconnected}});
    disjoint.document["next_id"] = 13;
    disjoint.validate();
    require(disjoint.uv_island(7) == all, "Overlapping UV positions connected separate geometry");
    auto moved = source;
    moved.transform_uv(all, {.2, -.4}, std::numbers::pi / 2, {-2, .5});
    uv_near(moved.document["faces"][0]["corners"][0]["uv"][0], 1.45);
    uv_near(moved.document["faces"][0]["corners"][0]["uv"][1], 2.1);
    require(moved.document["faces"][0]["corners"][1]["uv"] ==
                    moved.document["faces"][1]["corners"][0]["uv"] &&
                moved.uv_island(7) == all,
            "Common transform split a UV seam");
    auto preserved = moved.document;
    for (unsigned face = 0; face < 2; ++face)
        for (unsigned corner = 0; corner < 4; ++corner)
            preserved["faces"][face]["corners"][corner]["uv"] =
                original["faces"][face]["corners"][corner]["uv"];
    require(preserved == original,
            "UV transform changed topology/position/color/identity/extension data");
    auto selected = source;
    selected.transform_uv(std::array{MeshUvCorner{7, 0}}, {2, 3}, 0, {1, 1});
    uv_near(selected.document["faces"][0]["corners"][0]["uv"][0], 2);
    auto remaining = selected.document;
    remaining["faces"][0]["corners"][0]["uv"] = original["faces"][0]["corners"][0]["uv"];
    require(remaining == original, "UV edit changed unselected corners");
    auto mirrored = source;
    mirrored.transform_uv(all, {}, 0, {-1, 1});
    uv_near(mirrored.document["faces"][0]["corners"][0]["uv"][0], 2);
    require(mirrored.uv_island(7) == all, "UV mirror changed island topology");
    auto fit = source;
    for (auto& f : fit.document["faces"])
        for (auto& c : f["corners"]) {
            c["uv"][0] = 2 * c["uv"][0].get<double>() - 2;
            c["uv"][1] = 2 * c["uv"][1].get<double>() - 1;
        }
    fit.fit_uv(all, .1);
    uv_near(fit.document["faces"][0]["corners"][0]["uv"][0], .1);
    uv_near(fit.document["faces"][0]["corners"][0]["uv"][1], .3);
    uv_near(fit.document["faces"][1]["corners"][2]["uv"][0], .9);
    uv_near(fit.document["faces"][1]["corners"][2]["uv"][1], .7);
    require(fit.uv_island(7) == all, "UV fit split shared endpoints");
    for (double extent : {1e-300, 1e-12, 1., 100000.}) {
        auto tiny = source;
        for (auto& f : tiny.document["faces"])
            for (auto& c : f["corners"]) {
                c["uv"][0] = c["uv"][0].get<double>() * extent / 2;
                c["uv"][1] = c["uv"][1].get<double>() * extent / 2;
            }
        tiny.fit_uv(all, .1);
        uv_near(tiny.document["faces"][0]["corners"][0]["uv"][0], .1);
        uv_near(tiny.document["faces"][0]["corners"][0]["uv"][1], .3);
    }
    auto line = source;
    for (auto& f : line.document["faces"])
        for (auto& c : f["corners"])
            c["uv"][0] = 4.;
    line.fit_uv(all, 0);
    uv_near(line.document["faces"][0]["corners"][0]["uv"][0], .5);
    auto point = source;
    for (auto& f : point.document["faces"])
        for (auto& c : f["corners"])
            c["uv"] = {0, 0};
    const auto before = point.document;
    rejects([&] { point.fit_uv(all, 0); });
    require(point.document == before, "Zero-extent fit mutated source");
    const std::array<MeshUvCorner, 2> duplicate{{{7, 0}, {7, 0}}};
    for (auto bad : {std::vector<MeshUvCorner>{}, std::vector<MeshUvCorner>{{999, 0}},
                     std::vector<MeshUvCorner>{{7, 4}},
                     std::vector<MeshUvCorner>{duplicate.begin(), duplicate.end()}}) {
        rejects([&] { source.transform_uv(bad, {}, 0, {1, 1}); });
        rejects([&] { source.fit_uv(bad); });
        require(source.document == original, "Rejected selection mutated source");
    }
    for (double invalid : {0., 1e-6, 10001., std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()}) {
        rejects([&] { source.transform_uv(all, {}, 0, {invalid, 1}); });
        require(source.document == original, "Bad scale mutated source");
    }
    for (double invalid : {-.01, .5, std::numeric_limits<double>::quiet_NaN()})
        rejects([&] { source.fit_uv(all, invalid); });
    rejects([&] { source.transform_uv(all, {1000000, 0}, 0, {1, 1}); });
    rejects([&] { source.uv_island(999); });
    rejects([&] { source.transform_uv(7, std::array<std::size_t, 1>{0}, {}, 0, -1); });
    require(source.document == original, "Rejected UV operation changed source");
    auto legacy = source;
    legacy.transform_uv(7, std::array<std::size_t, 4>{3, 1, 0, 2}, {.2, -.1}, .4, 2);
    auto bulk = source;
    bulk.transform_uv(
        std::array{MeshUvCorner{7, 0}, MeshUvCorner{7, 1}, MeshUvCorner{7, 2}, MeshUvCorner{7, 3}},
        {.2, -.1}, .4, {2, 2});
    require(legacy.document == bulk.document,
            "Legacy single-face UV transform differs from common operation");
    const auto bytes = moved.document.dump();
    require(EditableMeshSource::parse(std::as_bytes(std::span(bytes))).document == moved.document,
            "UV operations do not survive source reopen");
}
} // namespace
