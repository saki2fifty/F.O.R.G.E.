#pragma once
#include "asset_reference_impact.hpp"
#include <forge/authoring.hpp>
#include <forge/scene_render_settings.hpp>
#include <limits>

inline void test_scene_auto_exposure_settings() {
    using namespace forge;
    const auto require = [](bool value, const char* message) {
        if (!value)
            throw std::runtime_error(message);
    };
    const auto rejects = [&](auto run) {
        bool rejected = false;
        try {
            run();
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "Invalid automatic exposure was admitted");
    };
    EngineContext engine;
    Scene scene(engine.world());
    require(scene_render_settings(scene.document()).auto_exposure == SceneAutoExposure{},
            "Old scenes enabled automatic exposure or changed its defaults");
    auto source = scene.document();
    source["rendering"] = {
        {"version", 1u}, {"exposure", 2}, {"auto_exposure", {{"future", "retain"}}}};
    scene.reset(source);
    const auto before = scene.document();
    authoring_command(scene, "scene.rendering.set",
                      {{"auto_exposure",
                        {{"enabled", true},
                         {"min_ev", -6},
                         {"max_ev", 5},
                         {"low_percentile", .05},
                         {"high_percentile", .9},
                         {"target_gray", .2},
                         {"speed_up", 2},
                         {"speed_down", 4}}}});
    const auto enabled = scene.document();
    require(scene_render_settings(enabled).auto_exposure ==
                    SceneAutoExposure{true, -6, 5, .05f, .9f, .2f, 2, 4} &&
                scene_render_settings(enabled).exposure == 2 && scene.undo() &&
                scene.document() == before && scene.redo() && scene.document() == enabled,
            "Automatic exposure lost values, manual compensation or atomic history");
    authoring_command(scene, "scene.rendering.set", {{"auto_exposure", {{"max_ev", 4}}}});
    const auto good = scene.document();
    require(good["rendering"]["auto_exposure"]["future"] == "retain" &&
                scene_render_settings(good).auto_exposure ==
                    SceneAutoExposure{true, -6, 4, .05f, .9f, .2f, 2, 4},
            "Partial exposure edit replaced unrelated or opaque settings");
    auto known = good;
    known["rendering"]["auto_exposure"].erase("future");
    const std::array known_docs{AssetReferenceDocument{"Exposure.scene.json", known}};
    const std::array extended_docs{AssetReferenceDocument{"Exposure.scene.json", good}};
    const auto extended = collect_asset_references(scene.schema(), extended_docs);
    require(collect_asset_references(scene.schema(), known_docs).uninspected.empty() &&
                extended.uninspected.size() == 1 &&
                extended.uninspected.front().find("rendering.auto_exposure.future") !=
                    std::string::npos,
            "Exposure reference traversal concealed extensions or failed known scalar fields");
    for (const Json& bad :
         {Json(1), Json(nullptr), Json{{"enabled", 1}}, Json{{"min_ev", -20.00000001}},
          Json{{"max_ev", 20.00000001}}, Json{{"min_ev", 7}}, Json{{"max_ev", -7}},
          Json{{"low_percentile", -.00001}}, Json{{"high_percentile", 1.00001}},
          Json{{"low_percentile", .9}}, Json{{"high_percentile", .05}},
          Json{{"target_gray", .0009999999}}, Json{{"target_gray", 1.000000001}},
          Json{{"speed_up", 0}}, Json{{"speed_down", 20.000000001}},
          Json{{"min_ev", 1.00000002}, {"max_ev", 1.00000001}},
          Json{{"low_percentile", .5000000001}, {"high_percentile", .5000000002}}}) {
        const auto revision = scene.revision();
        rejects([&] { authoring_command(scene, "scene.rendering.set", {{"auto_exposure", bad}}); });
        require(scene.document() == good && scene.revision() == revision,
                "Rejected exposure edit mutated Scene or revision");
        auto raw = good;
        if (bad.is_object())
            raw["rendering"]["auto_exposure"].update(bad);
        else
            raw["rendering"]["auto_exposure"] = bad;
        rejects([&] { (void)scene_render_settings(raw); });
    }
    float SceneAutoExposure::* const members[]{
        &SceneAutoExposure::min_ev,         &SceneAutoExposure::max_ev,
        &SceneAutoExposure::low_percentile, &SceneAutoExposure::high_percentile,
        &SceneAutoExposure::target_gray,    &SceneAutoExposure::speed_up,
        &SceneAutoExposure::speed_down};
    for (const auto member : members)
        for (const auto invalid :
             {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
            auto value = SceneAutoExposure{};
            value.*member = invalid;
            rejects([&] { validate_scene_auto_exposure(value); });
        }
    validate_scene_auto_exposure({true, -20, 20, 0, 1, .001f, .01f, 20});
    Scene restored(engine.world());
    restored.reset(good);
    require(restored.document() == good &&
                scene_render_settings(restored.document()).auto_exposure.enabled,
            "Scene snapshot restoration lost exposure intent");
}
