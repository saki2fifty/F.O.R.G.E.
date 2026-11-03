#include "mesh_scatter_authoring.hpp"
#include "spatial_document.hpp"
#include <chrono>
#include <cmath>
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <forge/render_scene.hpp>
#include <iostream>
#include <set>
using namespace forge;
namespace {
void check(bool v, const char* why) {
    if (!v)
        throw std::runtime_error(why);
}
template <class F> void rejected(F f) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, "Invalid placement was accepted");
}
const Json& row(const Json& doc, const std::string& id) {
    for (const auto& r : doc.at("entities"))
        if (r.at("id") == id)
            return r;
    throw std::runtime_error("Missing placement entity");
}
void layout() {
    detail::MeshScatterSettings s;
    s.count = 512;
    s.radius = 20;
    s.spacing = .4;
    s.seed = UINT32_MAX;
    const auto values = detail::mesh_scatter_layout(s);
    check(values.size() == 512 && values == detail::mesh_scatter_layout(s),
          "Seeded layout count or deterministic replay changed");
    s.seed = 0;
    check(values != detail::mesh_scatter_layout(s), "Seed did not affect placement");
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto& p = values[i];
        check(p.offset[0] * p.offset[0] + p.offset[1] * p.offset[1] <= 400 && p.scale >= .8 &&
                  p.scale <= 1.2 && p.yaw >= 0 && p.yaw <= 360,
              "Placement escaped its disk or variation bounds");
        for (std::size_t j = 0; j < i; ++j) {
            const auto& q = values[j];
            check(std::hypot(p.offset[0] - q.offset[0], p.offset[1] - q.offset[1]) >= .4,
                  "Placement spacing failed independent all-pairs check");
        }
    }
    s.count = 2;
    s.radius = .01;
    s.spacing = 100;
    rejected([&] { detail::mesh_scatter_layout(s); });
    s.count = 1;
    check(detail::mesh_scatter_layout(s).size() == 1, "Single placement needs no neighbor space");
    s.spacing = 0;
    s.count = 512;
    check(detail::mesh_scatter_layout(s).size() == 512, "Zero spacing rejected legal placement");
}
void distant_layout() {
    detail::MeshScatterSettings s;
    s.center = {1e12, 0, -1e12};
    s.radius = .01;
    s.count = 512;
    s.seed = 3;
    for (double spacing : {1e-320, .0002}) {
        s.spacing = spacing;
        const auto values = detail::mesh_scatter_layout(s);
        std::vector<Double2> positions;
        for (const auto& p : values) {
            const Double2 world{s.center[0] + p.offset[0], s.center[2] + p.offset[1]};
            check(std::hypot(world[0] - s.center[0], world[1] - s.center[2]) <= s.radius,
                  "Rounded world origin escaped the authored disk");
            for (const auto& q : positions)
                check(std::hypot(world[0] - q[0], world[1] - q[1]) >= spacing,
                      "World-coordinate rounding or squared-distance underflow violated spacing");
            positions.push_back(world);
        }
    }
    EngineContext engine;
    Scene scene(engine.world());
    const auto source = authoring_command(scene, "entity.create", {{"recipe", "primitive.0"}})
                            .at("selected")
                            .get<std::string>();
    s.count = 128;
    s.spacing = .0002;
    const auto result = authoring_command(
        scene, "mesh.scatter", detail::mesh_scatter_arguments(source, s, "Distant patch"));
    const auto view = scene.effective_document();
    std::vector<Double2> positions;
    for (const auto& id : result.at("created")) {
        const auto& affine = row(view, id.get<std::string>()).at("world_affine");
        const Double2 world{affine.at(3), affine.at(11)};
        for (const auto& q : positions)
            check(std::hypot(world[0] - q[0], world[1] - q[1]) >= s.spacing,
                  "Scene world propagation violated checked placement spacing");
        positions.push_back(world);
    }
}
void authoring() {
    EngineContext engine;
    Scene scene(engine.world());
    const auto source = authoring_command(scene, "entity.create", {{"recipe", "primitive.0"}})
                            .at("selected")
                            .get<std::string>();
    authoring_command(scene, "transform.local",
                      {{"entity", source},
                       {"translation", {{"x", 9}, {"y", 11}, {"z", 15}}},
                       {"rotation", detail::encode(rotation_from_euler({25, 15, 8}))},
                       {"scale", {{"x", -2}, {"y", 3}, {"z", 1}}}});
    // Gameplay and descendants must not silently be cloned into foliage.
    const auto child = authoring_command(scene, "entity.create", {{"recipe", "primitive.0"}})
                           .at("selected")
                           .get<std::string>();
    authoring_command(scene, "entity.reparent",
                      {{"entity", child}, {"parent", source}, {"mode", "keep_local"}});
    const auto before = scene.document();
    detail::MeshScatterSettings s;
    s.center = {20, 2, -10};
    s.count = 32;
    const auto args = detail::mesh_scatter_arguments(source, s, "Fern patch");
    const auto revision = scene.revision();
    const auto result = authoring_command(scene, "mesh.scatter", args);
    const auto after = scene.document();
    const auto group = result.at("selected").get<std::string>();
    check(result.at("created").size() == 32 && after.at("entities").size() == 35 &&
              row(after, source) == row(before, source) && row(after, child) == row(before, child),
          "Placement copied hierarchy or changed its template");
    const auto render = extract_render_scene(scene.effective_document());
    if (render.meshes.size() != 34) {
        std::cerr << "Scene render count=" << render.meshes.size()
                  << " group=" << row(scene.snapshot(), group).dump() << '\n';
        for (const auto& d : render.diagnostics)
            std::cerr << d.text << '\n';
    }
    check(render.meshes.size() == 34,
          "Nonrendering placement group emitted a compatibility primitive");
    const auto effective = scene.effective_document();
    const auto local = detail::read_local(row(effective, source).at("components"));
    const auto placements = detail::mesh_scatter_layout(s);
    std::set<std::string> ids;
    for (std::size_t i = 0; i < placements.size(); ++i) {
        const auto id = result.at("created")[i].get<std::string>();
        check(ids.insert(id).second && id != source && id != child && id != group,
              "Placement reused an existing identity");
        const auto& e = row(after, id);
        check(e.at("parent") == group && e.at("components").size() == 4 &&
                  e.at("components").at("forge.mesh_renderer") ==
                      row(before, source).at("components").at("forge.mesh_renderer"),
              "Placement did not preserve renderer values exclusively");
        const auto& world = row(effective, id).at("world_affine");
        check(std::abs(world[3].get<double>() - (20 + placements[i].offset[0])) < 1e-12 &&
                  world[7] == 2 &&
                  std::abs(world[11].get<double>() - (-10 + placements[i].offset[1])) < 1e-12,
              "Group center or local translation did not produce world placement");
        const auto actual = detail::read_local(e.at("components"));
        const auto wanted =
            affine_transform({{}, rotation_about_axis({0, 1, 0}, placements[i].yaw), {}}) *
            affine_transform({{}, local.rotation, {}});
        const auto rotated = affine_transform({{}, actual.rotation, {}});
        for (unsigned index : {0u, 1u, 2u, 4u, 5u, 6u, 8u, 9u, 10u})
            check(std::abs(wanted.m[index] - rotated.m[index]) < 2e-6,
                  "Yaw and template orientation compose incorrectly");
        check(std::abs(actual.scale.x - local.scale.x * placements[i].scale) < 1e-5,
              "Signed template scale or variation was lost");
    }
    check(scene.undo() && scene.document() == before && scene.redo() && scene.document() == after,
          "Placement was not exactly one undo/redo step");
    Scene reopened(engine.world());
    reopened.reset(after);
    check(reopened.document() == after, "Reopening changed ordinary placement entities");
    const auto stable_revision = scene.revision();
    for (const auto& patch : std::vector<Json>{{{"radius", 0}},
                                               {{"radius", 1001}},
                                               {{"count", 513}},
                                               {{"count", 1.0}},
                                               {{"count", true}},
                                               {{"seed", -1}},
                                               {{"seed", 4294967296ull}},
                                               {{"spacing", -1}},
                                               {{"scale_min", 2}, {"scale_max", 1}},
                                               {{"yaw_min", 50}, {"yaw_max", 10}},
                                               {{"name", "   "}},
                                               {{"count", 2}, {"radius", .01}, {"spacing", 100}},
                                               {{"unexpected", true}}}) {
        auto bad = args;
        bad.update(patch);
        rejected([&] { authoring_command(scene, "mesh.scatter", bad); });
        check(scene.document() == after && scene.revision() == stable_revision,
              "Rejected placement changed data or history");
    }
    rejected([&] {
        apply_authoring(scene, Json::array({{{"operation", "mesh.scatter"}, {"arguments", args}}}),
                        revision);
    });
    const auto no_renderer = authoring_command(scene, "entity.create", {{"recipe", "empty"}})
                                 .at("selected")
                                 .get<std::string>();
    auto bad = args;
    bad["entity"] = no_renderer;
    rejected([&] { authoring_command(scene, "mesh.scatter", bad); });
    bool discovery = false;
    for (const auto& c : authoring_commands())
        if (c.at("id") == "mesh.scatter")
            discovery = c.at("input_schema").at("properties").at("count").at("maximum") == 512;
    check(discovery, "Scatter is missing from shared command discovery");
    for (unsigned count : {32u, 128u, 512u}) {
        scene.reset(before);
        s.count = count;
        s.radius = 20;
        const auto begin = std::chrono::steady_clock::now();
        authoring_command(scene, "mesh.scatter",
                          detail::mesh_scatter_arguments(source, s, "Measured"));
        std::cout << "placement_count=" << count << " elapsed_ms="
                  << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                               begin)
                         .count()
                  << '\n';
    }
}
} // namespace
int main() {
    try {
        layout();
        distant_layout();
        authoring();
        std::cout << "Mesh scatter checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
