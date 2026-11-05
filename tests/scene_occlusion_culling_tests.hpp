#pragma once
#include "asset_reference_impact.hpp"
#include <forge/authoring.hpp>
#include <forge/scene_render_settings.hpp>
#include <limits>
inline void test_scene_occlusion_culling() {
    using namespace forge;
    const auto check = [](bool ok, const char* message) {
        if (!ok)
            throw std::runtime_error(message);
    };
    EngineContext engine;
    Scene scene(engine.world());
    auto initial = scene.document();
    check(scene_render_settings(initial).occlusion_culling == SceneOcclusionCulling{} &&
              scene.document() == initial,
          "Legacy Scene read materialized occlusion settings");
    initial["rendering"] = {{"version", 1u}, {"occlusion_culling", {{"extension", "retain"}}}};
    scene.reset(initial);
    authoring_command(scene, "scene.rendering.set",
                      {{"occlusion_culling", {{"enabled", true}, {"depth_bias", .002}}}});
    const auto active = scene.document();
    check(scene_render_settings(active).occlusion_culling == SceneOcclusionCulling{true, .002f} &&
              scene.undo() && scene.document() == initial && scene.redo() &&
              scene.document() == active,
          "Occlusion settings lost values or atomic Undo/Redo");
    authoring_command(scene, "scene.rendering.set", {{"occlusion_culling", {{"depth_bias", 0}}}});
    const auto retained = scene.document();
    check(retained["rendering"]["occlusion_culling"]["extension"] == "retain" &&
              scene_render_settings(retained).occlusion_culling.enabled &&
              scene_render_settings(retained).occlusion_culling.depth_bias == 0,
          "Partial occlusion patch lost extension metadata or minimum bias");
    for (const Json& bad : {Json(nullptr), Json(1), Json{{"enabled", 1}},
                            Json{{"depth_bias", true}}, Json{{"depth_bias", "0"}},
                            Json{{"depth_bias", -1e-30}}, Json{{"depth_bias", .01000000000001}}}) {
        const auto revision = scene.revision();
        bool rejected = false;
        try {
            authoring_command(scene, "scene.rendering.set", {{"occlusion_culling", bad}});
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && scene.document() == retained && scene.revision() == revision,
              "Invalid occlusion patch changed Scene/revision");
    }
    authoring_command(scene, "scene.rendering.set", {{"occlusion_culling", {{"depth_bias", .01}}}});
    check(scene_render_settings(scene.document()).occlusion_culling.depth_bias == .01f,
          "Valid occlusion maximum rejected");
    for (const auto bias : {-.001f, .011f, std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::quiet_NaN()}) {
        bool rejected = false;
        try {
            validate_scene_occlusion_culling({false, bias});
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, "Native occlusion settings bypassed validation");
    }
    auto known = retained;
    known["rendering"]["occlusion_culling"].erase("extension");
    const std::array plain{AssetReferenceDocument{"Cull.scene.json", known}},
        extended{AssetReferenceDocument{"Cull.scene.json", retained}};
    check(collect_asset_references(scene.schema(), plain).uninspected.empty() &&
              collect_asset_references(scene.schema(), extended).uninspected.size() == 1,
          "Occlusion known fields/extensions bypassed reference inspection");
    Scene reopened(engine.world());
    reopened.reset(retained);
    check(reopened.document() == retained, "Reopen lost occlusion settings");
    bool discovered = false;
    for (const auto& operation : authoring_commands())
        if (operation.at("id") == "scene.rendering.set") {
            const auto& schema =
                operation.at("input_schema").at("properties").at("occlusion_culling");
            discovered = schema.at("properties").size() == 2 &&
                         schema.at("properties").at("depth_bias").at("minimum") == 0 &&
                         schema.at("properties").at("depth_bias").at("maximum") == .01;
        }
    check(discovered, "Occlusion controls missing from shared authoring discovery");
}
