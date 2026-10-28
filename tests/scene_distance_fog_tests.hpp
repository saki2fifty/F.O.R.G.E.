#pragma once
#include "asset_reference_impact.hpp"
#include <forge/authoring.hpp>
#include <forge/scene_render_settings.hpp>
#include <limits>

inline void test_scene_distance_fog_settings() {
    using namespace forge;
    const auto require = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    const auto rejects = [&](auto run) {
        bool rejected = false;
        try {
            run();
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "Invalid distance fog was admitted");
    };
    EngineContext engine;
    Scene scene(engine.world());
    require(scene_render_settings(scene.document()).distance_fog == SceneDistanceFog{},
            "Old Scene enabled fog or changed defaults");
    auto doc = scene.document();
    doc["rendering"] = {{"version", 1u}, {"distance_fog", {{"future", "retain"}}}};
    scene.reset(doc);
    authoring_command(scene, "scene.rendering.set",
                      {{"distance_fog",
                        {{"enabled", true}, {"color", {.25, .5, 2}}, {"start", 5}, {"end", 30}}}});
    const auto first = scene.document();
    require(scene_render_settings(first).distance_fog ==
                    SceneDistanceFog{true, {.25f, .5f, 2}, 5, 30} &&
                scene.undo() && scene.document() == doc && scene.redo() &&
                scene.document() == first,
            "Fog settings lost values or atomic history");
    authoring_command(scene, "scene.rendering.set", {{"distance_fog", {{"start", 10}}}});
    const auto good = scene.document();
    require(good["rendering"]["distance_fog"]["future"] == "retain" &&
                scene_render_settings(good).distance_fog ==
                    SceneDistanceFog{true, {.25f, .5f, 2}, 10, 30},
            "Fog partial edit replaced unrelated state");
    auto known = good;
    known["rendering"]["distance_fog"].erase("future");
    const std::array known_docs{AssetReferenceDocument{"Fog.scene.json", known}};
    const std::array extended_docs{AssetReferenceDocument{"Fog.scene.json", good}};
    const auto impact = collect_asset_references(scene.schema(), extended_docs);
    require(collect_asset_references(scene.schema(), known_docs).uninspected.empty() &&
                impact.uninspected.size() == 1 &&
                impact.uninspected.front().find("rendering.distance_fog.future") !=
                    std::string::npos,
            "Fog reference inspection concealed extensions or rejected known fields");
    for (const Json& bad :
         {Json(1), Json(nullptr), Json{{"enabled", 1}}, Json{{"start", -.00000001}},
          Json{{"end", .000999999999}}, Json{{"end", 1000000.000001}}, Json{{"start", 31}},
          Json{{"end", 10}}, Json{{"start", 1000001}}, Json{{"start", 29.9999999}},
          Json{{"color", {0, 0}}}, Json{{"color", {0, 0, -1e-20}}},
          Json{{"color", {0, 65504.000001, 0}}}, Json{{"color", {"red", 0, 0}}}}) {
        const auto revision = scene.revision();
        rejects([&] { authoring_command(scene, "scene.rendering.set", {{"distance_fog", bad}}); });
        require(scene.document() == good && scene.revision() == revision,
                "Rejected fog edit mutated Scene or revision");
        auto raw = good;
        if (bad.is_object())
            raw["rendering"]["distance_fog"].update(bad);
        else
            raw["rendering"]["distance_fog"] = bad;
        rejects([&] { (void)scene_render_settings(raw); });
    }
    for (const float bad :
         {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
          -std::numeric_limits<float>::infinity()}) {
        auto value = SceneDistanceFog{};
        value.color[1] = bad;
        rejects([&] { validate_scene_distance_fog(value); });
        value = {};
        value.start = bad;
        rejects([&] { validate_scene_distance_fog(value); });
        value = {};
        value.end = bad;
        rejects([&] { validate_scene_distance_fog(value); });
    }
    validate_scene_distance_fog({true, {0, 65504, 2}, 0, .001f});
    validate_scene_distance_fog({true, {0, 65504, 2}, 999999.9375f, 1000000});
    Scene restored(engine.world());
    restored.reset(good);
    require(restored.document() == good &&
                scene_render_settings(restored.document()).distance_fog.enabled,
            "Scene snapshot restoration lost fog intent");
}
