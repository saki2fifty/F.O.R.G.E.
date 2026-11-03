#pragma once
#include "asset_reference_impact.hpp"
#include <forge/authoring.hpp>
#include <forge/scene_render_settings.hpp>
#include <limits>

inline void test_scene_fast_aa_settings() {
    using namespace forge;
    const auto check = [](bool value, const char* why) {
        if (!value)
            throw std::runtime_error(why);
    };
    EngineContext engine;
    Scene scene(engine.world());
    const auto legacy = scene.document();
    check(scene_render_settings(legacy).fast_approximate_anti_aliasing ==
                  SceneFastApproximateAntiAliasing{} &&
              scene.document() == legacy,
          "Reading legacy Scene settings authored spatial AA defaults");
    auto original = legacy;
    original["rendering"] = {{"version", 1u},
                             {"fast_approximate_anti_aliasing", {{"extension", "retain"}}}};
    scene.reset(original);
    authoring_command(scene, "scene.rendering.set",
                      {{"fast_approximate_anti_aliasing",
                        {{"enabled", true}, {"subpixel_strength", .75}, {"search_steps", 8}}}});
    const auto enabled = scene.document();
    check(scene_render_settings(enabled).fast_approximate_anti_aliasing ==
                  SceneFastApproximateAntiAliasing{true, .75f, 8} &&
              scene.undo() && scene.document() == original && scene.redo() &&
              scene.document() == enabled,
          "Spatial AA values or atomic Scene history changed");
    authoring_command(
        scene, "scene.rendering.set",
        {{"fast_approximate_anti_aliasing", {{"subpixel_strength", 0}, {"search_steps", 16}}}});
    const auto good = scene.document();
    check(scene_render_settings(good).fast_approximate_anti_aliasing ==
                  SceneFastApproximateAntiAliasing{true, 0, 16} &&
              good.at("rendering").at("fast_approximate_anti_aliasing").at("extension") == "retain",
          "Partial spatial AA edit replaced unrelated authored metadata");
    for (const Json& bad :
         {Json(1), Json(nullptr), Json{{"enabled", 1}}, Json{{"subpixel_strength", -.00001}},
          Json{{"subpixel_strength", 1.00000000001}}, Json{{"subpixel_strength", "high"}},
          Json{{"search_steps", 0}}, Json{{"search_steps", 17}}, Json{{"search_steps", 1.5}},
          Json{{"search_steps", true}}}) {
        const auto revision = scene.revision();
        bool rejected = false;
        try {
            authoring_command(scene, "scene.rendering.set",
                              {{"fast_approximate_anti_aliasing", bad}});
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && scene.document() == good && scene.revision() == revision,
              "Rejected spatial AA edit changed Scene data or revision");
    }
    for (const auto& bad :
         {SceneFastApproximateAntiAliasing{true, std::numeric_limits<float>::quiet_NaN(), 12},
          SceneFastApproximateAntiAliasing{true, std::numeric_limits<float>::infinity(), 12},
          SceneFastApproximateAntiAliasing{false, -1, 12},
          SceneFastApproximateAntiAliasing{true, .5f, 0},
          SceneFastApproximateAntiAliasing{true, .5f, 17}}) {
        bool rejected = false;
        try {
            validate_scene_fast_approximate_anti_aliasing(bad);
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, "Invalid extracted spatial AA settings were admitted");
    }
    auto known = good;
    known["rendering"]["fast_approximate_anti_aliasing"].erase("extension");
    const std::array known_documents{AssetReferenceDocument{"Spatial.scene.json", known}};
    const std::array extended_documents{AssetReferenceDocument{"Spatial.scene.json", good}};
    const auto extended = collect_asset_references(scene.schema(), extended_documents);
    check(collect_asset_references(scene.schema(), known_documents).uninspected.empty() &&
              extended.uninspected.size() == 1 &&
              extended.uninspected[0].find("fast_approximate_anti_aliasing.extension") !=
                  std::string::npos,
          "Reference inspection hid extensions or treated known spatial AA fields as assets");
    Scene reopened(engine.world());
    reopened.reset(good);
    check(reopened.document() == good &&
              scene_render_settings(reopened.document()).fast_approximate_anti_aliasing.enabled,
          "Reopening lost spatial AA settings");
    bool discovered = false;
    for (const auto& operation : authoring_commands())
        if (operation.at("id") == "scene.rendering.set") {
            const auto& field =
                operation.at("input_schema").at("properties").at("fast_approximate_anti_aliasing");
            discovered = field.at("properties").at("search_steps").at("minimum") == 1 &&
                         field.at("properties").at("search_steps").at("maximum") == 16;
        }
    check(discovered, "Spatial AA limits are missing from authoring/MCP discovery");
}
