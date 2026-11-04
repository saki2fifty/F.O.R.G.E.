#include "mesh_scatter_authoring.hpp"
#include "spatial_document.hpp"
#include <chrono>
#include <cmath>
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <forge/render_scene.hpp>
#include <iostream>
#include <limits>
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
void resolved_heights() {
    EngineContext engine;
    Scene scene(engine.world());
    const auto source = authoring_command(scene, "entity.create", {{"recipe", "primitive.0"}})
                            .at("selected")
                            .get<std::string>();
    detail::MeshScatterSettings settings;
    settings.center = {20, 7, -10};
    settings.count = 16;
    const auto layout = detail::mesh_scatter_layout(settings);
    auto arguments = detail::mesh_scatter_arguments(source, settings, "Projected patch");
    Json heights = Json::array();
    for (const auto& placement : layout)
        heights.push_back(.5 * (settings.center[0] + placement.offset[0]) +
                          .25 * (settings.center[2] + placement.offset[1]) + 3);
    arguments["world_heights"] = heights;
    const auto before = scene.document();
    const auto result = authoring_command(scene, "mesh.scatter", arguments);
    const auto after = scene.document();
    const auto effective = scene.effective_document();
    for (std::size_t i = 0; i < layout.size(); ++i) {
        const auto& world = row(effective, result.at("created")[i]).at("world_affine");
        check(std::abs(world[7].get<double>() -
                       (.5 * world[3].get<double>() + .25 * world[11].get<double>() + 3)) < 1e-10,
              "Resolved placement origins do not lie on the expected world plane");
    }
    check(scene.undo() && scene.document() == before && scene.redo() && scene.document() == after,
          "Resolved height placement did not use one Scene history entry");
    for (const auto& bad : std::vector<Json>{Json::array({0}), Json::array(), nullptr, "invalid"}) {
        auto request = arguments;
        request["world_heights"] = bad;
        const auto revision = scene.revision();
        rejected([&] { authoring_command(scene, "mesh.scatter", request); });
        check(scene.revision() == revision && scene.document() == after,
              "Malformed resolved heights changed the Scene");
    }
    for (const auto& bad : {Json(true), Json("height"), Json(1e12 + 1),
                            Json(std::numeric_limits<double>::infinity())}) {
        auto request = arguments;
        request["world_heights"][8] = bad;
        const auto revision = scene.revision();
        rejected([&] { authoring_command(scene, "mesh.scatter", request); });
        check(scene.revision() == revision && scene.document() == after,
              "Invalid resolved height partially changed placement/history");
    }
}
void selection_masks() {
    EngineContext engine;
    Scene scene(engine.world());
    const auto source = authoring_command(scene, "entity.create", {{"recipe", "primitive.0"}})
                            .at("selected")
                            .get<std::string>();
    detail::MeshScatterSettings settings;
    settings.count = 16;
    auto arguments = detail::mesh_scatter_arguments(source, settings, "Selected patch");
    arguments["world_heights"] = Json::array();
    for (unsigned i = 0; i < settings.count; ++i)
        arguments["world_heights"].push_back(i * .5);
    const auto before = scene.document();
    const auto full = authoring_command(scene, "mesh.scatter", arguments);
    const auto full_doc = scene.document();
    check(scene.undo() && scene.document() == before, "Full candidate Undo failed");
    arguments["placement_mask"] = Json::array();
    for (unsigned i = 0; i < settings.count; ++i)
        arguments["placement_mask"].push_back(i % 3 == 0);
    const auto result = authoring_command(scene, "mesh.scatter", arguments);
    const auto after = scene.document();
    check(result.at("candidate_count") == 16 && result.at("placed_count") == 6 &&
              result.at("created").size() == 6 &&
              after.at("entities").size() == before.at("entities").size() + 7,
          "Mask did not select the nonempty subset of the original pool");
    for (unsigned index = 0; index < 6; ++index) {
        const auto& expected = row(full_doc, full.at("created")[index * 3]);
        const auto& selected = row(after, result.at("created")[index]);
        check(selected.at("components") == expected.at("components") &&
                  selected.at("name") == expected.at("name"),
              "Filtering changed original candidate height/yaw/scale/name correspondence");
    }
    check(scene.undo() && scene.document() == before && scene.redo() && scene.document() == after,
          "Selected placement did not use one ordinary history entry");
    for (const Json& invalid :
         {Json::array(), Json::array({true}), Json(false), Json(std::vector<bool>(16, false)),
          Json(std::vector<int>(16, 1))}) {
        auto bad = arguments;
        bad["placement_mask"] = invalid;
        const auto revision = scene.revision();
        rejected([&] { authoring_command(scene, "mesh.scatter", bad); });
        check(scene.document() == after && scene.revision() == revision,
              "Invalid/empty selection partially changed Scene/history");
    }
}
void normal_alignment() {
    EngineContext engine;
    Scene scene(engine.world());
    const auto source = authoring_command(scene, "entity.create", {{"recipe", "primitive.0"}})
                            .at("selected")
                            .get<std::string>();
    const auto template_rotation = rotation_about_axis({1, 0, 0}, 20);
    authoring_command(scene, "transform.local",
                      {{"entity", source},
                       {"rotation",
                        {{"x", template_rotation.x},
                         {"y", template_rotation.y},
                         {"z", template_rotation.z},
                         {"w", template_rotation.w}}},
                       {"scale", {{"x", -1}, {"y", 2}, {"z", .5}}}});
    detail::MeshScatterSettings settings;
    settings.count = 5;
    auto arguments = detail::mesh_scatter_arguments(source, settings, "Oriented patch");
    const auto before = scene.document();
    const auto upright = authoring_command(scene, "mesh.scatter", arguments);
    const auto upright_doc = scene.document();
    check(!upright.at("aligned_to_slope").get<bool>() && scene.undo(),
          "Default placement orientation changed");
    const double tiny = std::numeric_limits<double>::denorm_min();
    const std::array<Double3, 5> normals{
        {{0, 1, 0}, {1, 1, 0}, {0, 1, 2}, {-3, 1, -2}, {tiny, tiny, tiny}}};
    arguments["world_normals"] = Json::array();
    for (const auto& n : normals)
        arguments["world_normals"].push_back({{"x", n[0]}, {"y", n[1]}, {"z", n[2]}});
    const auto aligned = authoring_command(scene, "mesh.scatter", arguments);
    const auto aligned_doc = scene.document();
    check(aligned.at("aligned_to_slope").get<bool>(), "Alignment provenance missing");
    double maximum_error = 0;
    // Authored LocalRotation is float; matrix evaluation uses those quantized samples.
    for (unsigned i = 0; i < normals.size(); ++i) {
        const auto base =
            detail::read_local(row(upright_doc, upright.at("created")[i]).at("components"));
        const auto actual =
            detail::read_local(row(aligned_doc, aligned.at("created")[i]).at("components"));
        auto n = i == 4 ? Double3{1, 1, 1} : normals[i];
        const auto length = std::hypot(n[0], n[1], n[2]);
        LocalTransform expected_tilt;
        if (n[0] || n[2])
            expected_tilt.rotation = rotation_about_axis(
                {n[2], 0, -n[0]}, std::acos(n[1] / length) * 180 / std::acos(-1.));
        const auto expected = affine_transform(expected_tilt);
        const auto base_affine = affine_transform(base), actual_affine = affine_transform(actual);
        for (const auto axis : {Double3{1, 0, 0}, Double3{0, 1, 0}, Double3{0, 0, 1}}) {
            const auto want = expected.vector(base_affine.vector(axis)),
                       got = actual_affine.vector(axis);
            for (unsigned k = 0; k < 3; ++k) {
                const auto error = std::abs(want[k] - got[k]);
                maximum_error = std::max(maximum_error, error);
                if (error >= 2e-6)
                    throw std::runtime_error(
                        "Alignment differs from independent axis-angle frame: " +
                        Json{{"candidate", i},
                             {"expected", want},
                             {"actual", got},
                             {"component_error", error}}
                            .dump());
            }
        }
        check(actual.translation == base.translation && actual.scale == base.scale,
              "Alignment changed candidate origin or scale");
    }
    std::cout << "Float-authored alignment maximum basis error=" << maximum_error << '\n';
    check(scene.undo() && scene.document() == before && scene.redo() &&
              scene.document() == aligned_doc,
          "Alignment did not use one ordinary Scene history entry");
    for (const auto& bad_normal :
         {Json{{"x", 0}, {"y", 0}, {"z", 0}}, Json{{"x", 0}, {"y", -1}, {"z", 0}},
          Json{{"x", true}, {"y", 1}, {"z", 0}}, Json{{"x", 1e12 + 1}, {"y", 1}, {"z", 0}},
          Json::array({0, 1, 0})}) {
        auto bad = arguments;
        bad["world_normals"][3] = bad_normal;
        bad["placement_mask"] = {true, true, true, false, true};
        const auto revision = scene.revision();
        rejected([&] { authoring_command(scene, "mesh.scatter", bad); });
        check(scene.document() == aligned_doc && scene.revision() == revision,
              "Invalid excluded normal partially changed Scene/history");
    }
}
} // namespace
int main() {
    try {
        layout();
        distant_layout();
        authoring();
        resolved_heights();
        selection_masks();
        normal_alignment();
        std::cout << "Mesh scatter checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
