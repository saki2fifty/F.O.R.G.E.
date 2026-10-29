#pragma once
#include "asset_reference_impact.hpp"
#include <forge/authoring.hpp>
#include <forge/scene_render_settings.hpp>
#include <limits>

inline void test_scene_reflection_settings() {
    using namespace forge;
    const auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    EngineContext engine;
    Scene scene(engine.world());
    check(scene_render_settings(scene.document()).screen_space_reflections ==
              SceneScreenSpaceReflections{},
          "Legacy Scene did not retain default-off screen-space reflections");
    auto original = scene.document();
    original["rendering"] = {{"version", 1u}, {"screen_space_reflections", {{"extension", 7}}}};
    scene.reset(original);
    authoring_command(scene, "scene.rendering.set",
                      {{"screen_space_reflections",
                        {{"enabled", true},
                         {"max_distance", 20},
                         {"thickness", .05},
                         {"strength", .75},
                         {"max_roughness", .4}}}});
    const auto enabled = scene.document();
    check(scene_render_settings(enabled).screen_space_reflections ==
                  SceneScreenSpaceReflections{true, 20, .05f, .75f, .4f} &&
              scene.undo() && scene.document() == original && scene.redo() &&
              scene.document() == enabled,
          "Reflection settings bypassed atomic Scene history");
    authoring_command(scene, "scene.rendering.set",
                      {{"screen_space_reflections", {{"strength", .5}}}});
    const auto good = scene.document();
    check(scene_render_settings(good).screen_space_reflections ==
                  SceneScreenSpaceReflections{true, 20, .05f, .5f, .4f} &&
              good["rendering"]["screen_space_reflections"]["extension"] == 7,
          "Partial reflection edit lost omitted values or opaque metadata");
    for (const auto& bad :
         {Json(1), Json(nullptr), Json{{"enabled", 1}}, Json{{"max_distance", 100.000000001}},
          Json{{"thickness", 1.000000001}}, Json{{"thickness", .000099999999}},
          Json{{"strength", 1.000000001}}, Json{{"max_roughness", 1.000000001}},
          Json{{"max_distance", -.001}}, Json{{"strength", -1}}, Json{{"max_roughness", -.001}},
          Json{{"thickness", "thin"}}}) {
        const auto revision = scene.revision();
        bool rejected = false;
        try {
            authoring_command(scene, "scene.rendering.set", {{"screen_space_reflections", bad}});
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && scene.document() == good && scene.revision() == revision,
              "Rejected reflection values changed Scene values or revision");
    }
    for (auto member :
         {&SceneScreenSpaceReflections::max_distance, &SceneScreenSpaceReflections::thickness,
          &SceneScreenSpaceReflections::strength, &SceneScreenSpaceReflections::max_roughness})
        for (float bad : {std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN(), -1.f}) {
            SceneScreenSpaceReflections value;
            value.*member = bad;
            bool rejected = false;
            try {
                validate_scene_screen_space_reflections(value);
            } catch (const std::exception&) {
                rejected = true;
            }
            check(rejected, "Invalid extracted reflection settings were admitted while disabled");
        }
    auto known = good;
    known["rendering"]["screen_space_reflections"].erase("extension");
    const std::array plain{AssetReferenceDocument{"Reflection.scene.json", known}};
    const std::array extended{AssetReferenceDocument{"Reflection.scene.json", good}};
    check(collect_asset_references(scene.schema(), plain).uninspected.empty() &&
              collect_asset_references(scene.schema(), extended).uninspected.size() == 1,
          "Reflection reference inspection hid metadata or misidentified supported fields");
    Scene reopened(engine.world());
    reopened.reset(good);
    check(reopened.document() == good, "Reopening lost reflection settings");
    bool discovered = false;
    for (const auto& row : authoring_commands())
        if (row.at("id") == "scene.rendering.set") {
            const auto& schema =
                row.at("input_schema").at("properties").at("screen_space_reflections");
            discovered = schema.at("properties").size() == 5 &&
                         schema.at("properties").at("thickness").at("minimum") == .0001;
        }
    check(discovered, "Reflection settings missing from shared automation/MCP discovery");
}
