#pragma once
#include "asset_reference_impact.hpp"
#include <forge/authoring.hpp>
#include <forge/scene_render_settings.hpp>
#include <limits>

inline void test_scene_ambient_occlusion() {
    using namespace forge;
    const auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    EngineContext engine;
    Scene scene(engine.world());
    check(scene_render_settings(scene.document()).ambient_occlusion == SceneAmbientOcclusion{},
          "Legacy Scene did not retain default-off ambient occlusion");
    auto original = scene.document();
    original["rendering"] = {{"version", 1u}, {"ambient_occlusion", {{"extension", 7}}}};
    scene.reset(original);
    authoring_command(scene, "scene.rendering.set",
                      {{"ambient_occlusion",
                        {{"enabled", true}, {"radius", 2}, {"strength", .75}, {"bias", .02}}}});
    const auto enabled = scene.document();
    check(scene_render_settings(enabled).ambient_occlusion ==
                  SceneAmbientOcclusion{true, 2, .75f, .02f} &&
              scene.undo() && scene.document() == original && scene.redo() &&
              scene.document() == enabled,
          "Ambient occlusion bypassed atomic Scene history");
    authoring_command(scene, "scene.rendering.set", {{"ambient_occlusion", {{"strength", .5}}}});
    const auto geometric = scene.document();
    check(scene_render_settings(geometric).ambient_occlusion.normal_influence == 0,
          "Legacy geometric occlusion gained material normal influence");
    authoring_command(scene, "scene.rendering.set",
                      {{"ambient_occlusion", {{"normal_influence", .8}}}});
    check(scene_render_settings(scene.document()).ambient_occlusion.normal_influence == .8f &&
              scene.undo() && scene.document() == geometric && scene.redo(),
          "Normal influence bypassed partial Scene history");
    const auto good = scene.document();
    check(scene_render_settings(good).ambient_occlusion ==
                  SceneAmbientOcclusion{true, 2, .5f, .02f, .8f} &&
              good["rendering"]["ambient_occlusion"]["extension"] == 7,
          "Partial ambient edit lost omitted values or opaque metadata");
    for (const auto& bad :
         {Json(1), Json(nullptr), Json{{"enabled", 1}}, Json{{"radius", 10.000000001}},
          Json{{"strength", 1.000000001}}, Json{{"bias", 1.000000001}}, Json{{"radius", -.001}},
          Json{{"strength", -1}}, Json{{"bias", -.001}}, Json{{"radius", "near"}},
          Json{{"normal_influence", 1.0000000001}}, Json{{"normal_influence", -.000000001}},
          Json{{"normal_influence", true}}, Json{{"normal_influence", nullptr}},
          Json{{"normal_influence", "material"}}}) {
        const auto revision = scene.revision();
        bool rejected = false;
        try {
            authoring_command(scene, "scene.rendering.set", {{"ambient_occlusion", bad}});
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && scene.document() == good && scene.revision() == revision,
              "Rejected ambient values mutated Scene values or revision");
    }
    for (auto member : {&SceneAmbientOcclusion::radius, &SceneAmbientOcclusion::strength,
                        &SceneAmbientOcclusion::bias, &SceneAmbientOcclusion::normal_influence})
        for (float bad : {std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN(), -1.f}) {
            SceneAmbientOcclusion value;
            value.*member = bad;
            bool rejected = false;
            try {
                validate_scene_ambient_occlusion(value);
            } catch (const std::exception&) {
                rejected = true;
            }
            check(rejected, "Invalid extracted ambient settings were admitted while disabled");
        }
    auto known = good;
    known["rendering"]["ambient_occlusion"].erase("extension");
    const std::array plain{AssetReferenceDocument{"Ambient.scene.json", known}};
    const std::array extended{AssetReferenceDocument{"Ambient.scene.json", good}};
    check(collect_asset_references(scene.schema(), plain).uninspected.empty() &&
              collect_asset_references(scene.schema(), extended).uninspected.size() == 1,
          "Ambient reference inspection hid extension or misunderstood supported fields");
    Scene reopened(engine.world());
    reopened.reset(good);
    check(reopened.document() == good, "Reopening lost ambient values");
    bool discovered = false;
    for (const auto& row : authoring_commands())
        if (row.at("id") == "scene.rendering.set")
            discovered = row.at("input_schema")
                             .at("properties")
                             .at("ambient_occlusion")
                             .at("properties")
                             .at("normal_influence") ==
                         Json{{"type", "number"}, {"minimum", 0}, {"maximum", 1}};
    check(discovered, "Ambient values missing from shared automation/MCP discovery");
}
