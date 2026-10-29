#pragma once
#include "asset_reference_impact.hpp"
#include <forge/authoring.hpp>
#include <forge/scene_render_settings.hpp>
#include <limits>

inline void test_scene_motion_blur() {
    using namespace forge;
    const auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    EngineContext engine;
    Scene scene(engine.world());
    check(scene_render_settings(scene.document()).motion_blur == SceneMotionBlur{},
          "Legacy Scene did not retain default-off motion blur");
    auto original = scene.document();
    original["rendering"] = {{"version", 1u}, {"motion_blur", {{"extension", 7}}}};
    scene.reset(original);
    authoring_command(scene, "scene.rendering.set",
                      {{"motion_blur", {{"enabled", true}, {"shutter", .75}, {"max_pixels", 24}}}});
    const auto enabled = scene.document();
    check(scene_render_settings(enabled).motion_blur == SceneMotionBlur{true, .75f, 24} &&
              scene.undo() && scene.document() == original && scene.redo() &&
              scene.document() == enabled,
          "Motion blur settings bypassed the Scene history owner");
    authoring_command(scene, "scene.rendering.set", {{"motion_blur", {{"shutter", .5}}}});
    const auto good = scene.document();
    check(scene_render_settings(good).motion_blur == SceneMotionBlur{true, .5f, 24} &&
              good["rendering"]["motion_blur"]["extension"] == 7,
          "Partial motion edit lost omitted fields or opaque metadata");
    for (const auto& bad : {Json(0), Json(nullptr), Json{{"enabled", 1}}, Json{{"shutter", -.001}},
                            Json{{"shutter", 1.000000001}}, Json{{"max_pixels", 32.000000001}},
                            Json{{"max_pixels", -1}}, Json{{"shutter", "half"}}}) {
        const auto revision = scene.revision();
        bool rejected = false;
        try {
            authoring_command(scene, "scene.rendering.set", {{"motion_blur", bad}});
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && scene.revision() == revision && scene.document() == good,
              "Rejected motion settings mutated values or revision");
    }
    for (float bad :
         {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), -1.f})
        for (bool shutter : {false, true}) {
            SceneMotionBlur value;
            if (shutter)
                value.shutter = bad;
            else
                value.max_pixels = bad;
            bool rejected = false;
            try {
                validate_scene_motion_blur(value);
            } catch (const std::exception&) {
                rejected = true;
            }
            check(rejected, "Nonfinite/negative extracted motion settings were admitted");
        }
    auto known = good;
    known["rendering"]["motion_blur"].erase("extension");
    const std::array plain{AssetReferenceDocument{"Motion.scene.json", known}};
    const std::array extended{AssetReferenceDocument{"Motion.scene.json", good}};
    check(collect_asset_references(scene.schema(), plain).uninspected.empty() &&
              collect_asset_references(scene.schema(), extended).uninspected.size() == 1,
          "Motion reference inspection lost known values or hid extensions");
    Scene reopened(engine.world());
    reopened.reset(good);
    check(reopened.document() == good, "Reopening lost authored motion settings");
    bool discovered = false;
    for (const auto& row : authoring_commands())
        if (row.at("id") == "scene.rendering.set")
            discovered = row.at("input_schema").at("properties").contains("motion_blur");
    check(discovered, "Motion setting absent from automation/MCP discovery");
}
