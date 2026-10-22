#include "builtins.hpp"
#include "physics_debug.hpp"
#include "tile_map_cook.hpp"
#include <cmath>
#include <future>
#include <iostream>
#include <limits>

using namespace forge;
namespace {
void require(bool condition, const char* text) {
    if (!condition)
        throw std::runtime_error(text);
}
template <class F> void rejects(F&& operation) {
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid planar preview accepted");
}
Json defaults(const char* name) {
    for (const auto& type : detail::builtins())
        if (std::string_view(type.name) == name)
            return type.defaults;
    throw std::runtime_error("Missing preview component schema");
}
void bounds(const PhysicsDebugGeometry& geometry, double x, double y, unsigned count) {
    require(geometry.triangles.empty() && geometry.lines.size() == count && !geometry.truncated,
            "Planar preview emitted wrong geometry domain/count");
    double minx = 1e9, maxx = -1e9, miny = 1e9, maxy = -1e9;
    for (std::size_t i = 0; i < geometry.lines.size(); ++i) {
        const auto& line = geometry.lines[i];
        require(line[1] == geometry.lines[(i + 1) % count][0],
                "Planar outline is not closed/continuous");
        for (const auto& point : line) {
            require(std::isfinite(point[0]) && std::isfinite(point[1]) && point[2] == 0,
                    "Planar outline contains depth/nonfinite geometry");
            minx = std::min(minx, double(point[0]));
            maxx = std::max(maxx, double(point[0]));
            miny = std::min(miny, double(point[1]));
            maxy = std::max(maxy, double(point[1]));
        }
    }
    require(std::abs(minx + x) < 1e-6 && std::abs(maxx - x) < 1e-6 && std::abs(miny + y) < 1e-6 &&
                std::abs(maxy - y) < 1e-6,
            "Planar guide differs from physical effective shape bounds");
}
void outlines() {
    for (float z : {-2.f, 0.f, 3.f})
        for (float x : {-2.f, 2.f})
            for (float y : {-2.f, 2.f}) {
                Json body{{"forge.physics_body_2d", Json::object()},
                          {"forge.box_collider_2d", {{"x", 2}, {"y", 4}}}};
                bounds(prepare_physics_debug(body, {x, y, z}), 2, 4, 4);
                body.erase("forge.box_collider_2d");
                body["forge.circle_collider_2d"] = {{"radius", .5}};
                bounds(prepare_physics_debug(body, {x, y, z}), 1, 1, 64);
                body.erase("forge.circle_collider_2d");
                body["forge.capsule_collider_2d"] = {{"radius", .5}, {"height", 2}};
                bounds(prepare_physics_debug(body, {x, y, z}), 1, 3, 66);
                const auto reduced = prepare_physics_debug(body, {x, y, z}, {}, false, 3);
                require(reduced.truncated && reduced.lines.size() == 3,
                        "Planar preview ignored copied geometry budget");
            }
    const Json input{{"forge.physics_body_2d", Json::object()},
                     {"forge.circle_collider_2d", {{"radius", .5}}}};
    auto worker = std::async(std::launch::async,
                             [input] { return prepare_physics_debug(input, {-2, 2, 0}); });
    bounds(worker.get(), 1, 1, 64); // No Flecs world/service/native body enters the worker.
    rejects([&] { prepare_physics_debug(input, {1, 2, 0}); });
    rejects([&] { prepare_physics_debug(input, {0, 0, 0}); });
    rejects([&] { prepare_physics_debug(input, {1, 1, 0}, {}, false, 0); });
    auto invalid = input;
    invalid["forge.capsule_collider_2d"] = {{"radius", .5}, {"height", 1}};
    rejects([&] { prepare_physics_debug(invalid, {1, 1, 0}); });
    invalid = input;
    invalid["forge.physics_body_2d"]["density"] = 0;
    rejects([&] { prepare_physics_debug(invalid, {1, 1, 0}); });
}
void domain_selection() {
    Json components{{"forge.physics_body_2d", Json::object()},
                    {"forge.circle_collider_2d", {{"radius", .5}}},
                    {"forge.character_controller", defaults("forge.character_controller")}};
    components["forge.character_controller"]["enabled"] = false;
    require(physics_debug_uses_planar(components) && !physics_debug_uses_character(components),
            "Disabled Character hid enabled2D preview");
    bounds(prepare_physics_debug(components, {1, 1, 0}), .5, .5, 64);
    components["forge.character_controller"]["enabled"] = true;
    rejects([&] { prepare_physics_debug(components, {1, 1, 0}); });
    components.erase("forge.character_controller");
    components["forge.physics_body"] = defaults("forge.physics_body");
    components["forge.box_collider"] = defaults("forge.box_collider");
    rejects([&] { prepare_physics_debug(components, {1, 1, 1}); });
    components["forge.physics_body_2d"]["enabled"] = false;
    require(!physics_debug_uses_planar(components), "Disabled2D hid enabled3D body");
    const auto spatial = prepare_physics_debug(components, {1, 1, 1});
    require(!spatial.triangles.empty() && spatial.lines.empty(), "3D preview was converted to2D");
    components.erase("forge.physics_body");
    components.erase("forge.box_collider");
    bounds(prepare_physics_debug(components, {1, 1, 0}), .5, .5, 64);
}
void tile_map_outlines() {
    constexpr std::uint64_t solid = UINT64_MAX - 1;
    auto source = TileMapValue::create(AssetId::generate(), AssetId::generate());
    auto metadata = source.metadata();
    metadata["next_id"] = UINT64_MAX;
    metadata["layers"][0]["name"] = "Decoration";
    metadata["layers"].push_back(
        {{"id", solid}, {"name", "Solid"}, {"visible", false}, {"order", 1}});
    source = source.with_metadata(std::move(metadata));
    const std::array<TileMapCellEdit, 4> writes{{{solid, {3, -2}, {17, 7}},
                                                 {solid, {4, -2}, {18, 0}},
                                                 {solid, {3, -1}, {19, 3}},
                                                 {1, {INT32_MAX, INT32_MIN}, {20, 0}}}};
    source = source.paint(writes);
    const auto cells = decode_cooked_tile_map(cook_tile_map(source), source.asset());
    Json components{{"forge.physics_body_2d", Json::object()},
                    {"forge.tile_map_renderer", defaults("forge.tile_map_renderer")},
                    {"forge.tile_map_collider_2d", {{"layer", solid}}}};
    components["forge.tile_map_renderer"]["map"] = source.asset();
    components["forge.tile_map_renderer"]["enabled"] = false;
    const auto preview = [&](const Json& values, LocalScale scale, const TileMapData* map,
                             std::size_t limit = 8192) {
        return prepare_physics_debug(values, scale, {}, false, limit, map);
    };
    const auto raw = preview(components, {1, 1, 1}, &cells);
    require(raw.triangles.empty() && raw.lines.size() == 8 && !raw.truncated,
            "Painted L-shaped layer did not retain its two rectangles/hole");
    for (unsigned rectangle = 0; rectangle < 2; ++rectangle)
        for (unsigned edge = 0; edge < 4; ++edge)
            require(raw.lines[4 * rectangle + edge][1] ==
                        raw.lines[4 * rectangle + (edge + 1) % 4][0],
                    "Painted rectangle outline is not closed");
    for (float px : {-2.f, 2.f})
        for (float py : {-3.f, 3.f})
            for (float pz : {-1.f, 0.f, 1.f}) {
                const LocalTransform parent{
                    {999900, -999900, 17}, rotation_about_axis({0, 0, 1}, 37), {px, py, pz}};
                const LocalTransform child{{1, -2, 3}, {}, {-.5f, .5f, -1}};
                const auto world = affine_transform(parent) * affine_transform(child);
                auto frame = physics_debug_pose(components, world, child);
                const auto scaled = preview(components, frame.scale, &cells);
                require(scaled.lines.size() == raw.lines.size(), "Reflection changed Map cover");
                frame.scale = {1, 1, 1};
                const auto posed = affine_transform(frame);
                for (const auto& line : raw.lines) {
                    const auto& point = line[0];
                    const auto expected = world.point({point[0], point[1], point[2]});
                    double nearest = std::numeric_limits<double>::infinity();
                    for (const auto& other : scaled.lines) {
                        const auto actual = posed.point({other[0][0], other[0][1], other[0][2]});
                        nearest = std::min(nearest, std::hypot(actual[0] - expected[0],
                                                               actual[1] - expected[1],
                                                               actual[2] - expected[2]));
                    }
                    require(nearest < 1e-5, "Offset Map outline differs from full signed affine");
                }
            }
    const auto limited = preview(components, {1, 1, 0}, &cells, 5);
    require(limited.truncated && limited.lines.size() == 5, "Map preview ignored line budget");
    auto worker = std::async(std::launch::async, [components, cells, preview] {
        return preview(components, {-1, 2, 0}, &cells);
    });
    require(worker.get().lines.size() == 8, "Detached Map preview failed");
    const std::array<TileMapCellEdit, 3> erase{
        {{solid, {3, -2}, {}}, {solid, {4, -2}, {}}, {solid, {3, -1}, {}}}};
    const auto empty = cells.with_erased_cells(erase);
    require(preview(components, {1, 1, 0}, &empty).lines.empty(),
            "Empty physical layer drew decorative cells or a placeholder shape");
    rejects([&] { preview(components, {0, 1, 0}, &empty); });
    rejects([&] { preview(components, {1, 1, 0}, nullptr); });
    rejects([&] { preview(components, {0, 1, 0}, &cells); });
    rejects([&] { preview(components, {1000, 1, 0}, &cells); });
    auto invalid = components;
    invalid["forge.tile_map_collider_2d"]["layer"] = 0;
    rejects([&] { preview(invalid, {1, 1, 0}, &cells); });
    invalid = components;
    invalid["forge.tile_map_renderer"]["map"] = AssetId::generate();
    rejects([&] { preview(invalid, {1, 1, 0}, &cells); });
    invalid = components;
    invalid["forge.physics_body_2d"]["motion"] = 2;
    rejects([&] { preview(invalid, {1, 1, 0}, &cells); });
    invalid = components;
    invalid["forge.physics_body_2d"]["sensor"] = true;
    rejects([&] { preview(invalid, {1, 1, 0}, &cells); });
    invalid = components;
    invalid["forge.box_collider_2d"] = {{"x", 1}, {"y", 1}};
    rejects([&] { preview(invalid, {1, 1, 0}, &cells); });
}
void parent_frames() {
    unsigned cases = 0;
    for (unsigned kind = 0; kind < 3; ++kind)
        for (float px : {-2.f, 2.f})
            for (float py : {-2.f, 2.f})
                for (float pz : {-3.f, 0.f, 3.f})
                    for (float cx : {-.5f, .5f})
                        for (float cy : {-.5f, .5f}) {
                            const LocalTransform parent{{999980, -999980, 10},
                                                        rotation_about_axis({0, 0, 1}, 37),
                                                        {px, py, pz}};
                            const LocalTransform child{
                                {1, -2, 3}, rotation_about_axis({0, 0, 1}, 15), {cx, cy, -1}};
                            const auto source = affine_transform(parent) * affine_transform(child);
                            Json components{{"forge.physics_body_2d", Json::object()}};
                            if (kind == 0)
                                components["forge.box_collider_2d"] = {{"x", 1}, {"y", 2}};
                            else if (kind == 1)
                                components["forge.circle_collider_2d"] = {{"radius", .5}};
                            else
                                components["forge.capsule_collider_2d"] = {{"radius", .25},
                                                                           {"height", 1}};
                            const auto raw = prepare_physics_debug(components, {1, 1, 1});
                            auto frame = physics_debug_pose(components, source, child);
                            const auto preview = prepare_physics_debug(components, frame.scale);
                            require(frame.rotation.x == 0 && frame.rotation.y == 0 &&
                                        frame.translation.z == source.m[11],
                                    "Planar guide lost depth/pure-Z frame");
                            frame.scale = {1, 1, 1};
                            const auto posed = affine_transform(frame);
                            for (const auto& line : raw.lines) {
                                const auto& point = line[0];
                                const auto expected = source.point({point[0], point[1], point[2]});
                                double nearest = std::numeric_limits<double>::infinity();
                                for (const auto& other : preview.lines) {
                                    const auto actual =
                                        posed.point({other[0][0], other[0][1], other[0][2]});
                                    nearest =
                                        std::min(nearest, std::hypot(actual[0] - expected[0],
                                                                     actual[1] - expected[1],
                                                                     actual[2] - expected[2]));
                                }
                                require(nearest < 1e-5,
                                        "Derived guide contour differs from full source affine");
                            }
                            ++cases;
                        }
    require(cases == 144, "Missing signed/flat parent frame cases");
    const Json components{{"forge.physics_body_2d", Json::object()}};
    auto tilted = affine_transform({{}, rotation_about_axis({1, 0, 0}, 20), {1, 1, 1}});
    rejects([&] { physics_debug_pose(components, tilted, {}); });
    AffineTransform shear;
    shear.m[1] = .5;
    rejects([&] { physics_debug_pose(components, shear, {}); });
    AffineTransform flat;
    flat.m[0] = 0;
    rejects([&] { physics_debug_pose(components, flat, {}); });
}
} // namespace
int main() {
    try {
        outlines();
        domain_selection();
        tile_map_outlines();
        parent_frames();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "Copied planar box/circle/capsule outlines, physical scale/profile, budgets, "
                 "worker and domain selection passed\n";
}
