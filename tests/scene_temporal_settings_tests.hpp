#pragma once
#include "asset_reference_impact.hpp"
#include <forge/authoring.hpp>
#include <forge/scene_render_settings.hpp>
#include <limits>

inline void test_scene_temporal_settings() {
    using namespace forge;
    const auto check = [](bool ok, const char* text) {
        if (!ok)
            throw std::runtime_error(text);
    };
    EngineContext engine;
    Scene scene(engine.world());
    check(scene_render_settings(scene.document()).temporal_anti_aliasing ==
              SceneTemporalAntiAliasing{},
          "Legacy Scene changed temporal defaults");
    auto original = scene.document();
    original["rendering"] = {{"version", 1u},
                             {"temporal_anti_aliasing", {{"extension", "retain"}}}};
    scene.reset(original);
    authoring_command(scene, "scene.rendering.set",
                      {{"temporal_anti_aliasing", {{"enabled", true}, {"stability", .85}}}});
    const auto enabled = scene.document();
    check(scene_render_settings(enabled).temporal_anti_aliasing ==
                  SceneTemporalAntiAliasing{true, .85f} &&
              scene.undo() && scene.document() == original && scene.redo() &&
              scene.document() == enabled,
          "Temporal settings lost values or atomic history");
    authoring_command(scene, "scene.rendering.set",
                      {{"temporal_anti_aliasing", {{"stability", 0}}}});
    const auto good = scene.document();
    check(scene_render_settings(good).temporal_anti_aliasing ==
                  SceneTemporalAntiAliasing{true, 0} &&
              good["rendering"]["temporal_anti_aliasing"]["extension"] == "retain",
          "Partial temporal edit replaced unrelated settings");
    for (const Json& bad :
         {Json(1), Json(nullptr), Json{{"enabled", 1}}, Json{{"stability", -.00001}},
          Json{{"stability", .95000000001}}, Json{{"stability", "high"}}}) {
        bool rejected = false;
        const auto revision = scene.revision();
        try {
            authoring_command(scene, "scene.rendering.set", {{"temporal_anti_aliasing", bad}});
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && scene.document() == good && scene.revision() == revision,
              "Rejected temporal edit mutated Scene or revision");
    }
    for (float bad : {std::numeric_limits<float>::quiet_NaN(),
                      std::numeric_limits<float>::infinity(), -1.f, .951f}) {
        bool rejected = false;
        try {
            validate_scene_temporal_anti_aliasing({true, bad});
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, "Invalid extracted temporal settings were admitted");
    }
    auto known = good;
    known["rendering"]["temporal_anti_aliasing"].erase("extension");
    const std::array known_docs{AssetReferenceDocument{"Temporal.scene.json", known}};
    const std::array extended_docs{AssetReferenceDocument{"Temporal.scene.json", good}};
    const auto impact = collect_asset_references(scene.schema(), extended_docs);
    check(collect_asset_references(scene.schema(), known_docs).uninspected.empty() &&
              impact.uninspected.size() == 1 &&
              impact.uninspected[0].find("temporal_anti_aliasing.extension") != std::string::npos,
          "Temporal reference inspection hid extensions or rejected known fields");
    Scene reopened(engine.world());
    reopened.reset(good);
    check(reopened.document() == good &&
              scene_render_settings(reopened.document()).temporal_anti_aliasing.enabled,
          "Reopening lost temporal settings");
    bool discovered = false;
    for (const auto& operation : authoring_commands())
        if (operation.at("id") == "scene.rendering.set")
            discovered =
                operation.at("input_schema").at("properties").contains("temporal_anti_aliasing");
    check(discovered, "Temporal settings were omitted from authoring/MCP discovery");
}
