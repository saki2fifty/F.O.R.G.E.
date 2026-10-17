#pragma once
#include "asset_reference_impact.hpp"
#include <forge/authoring.hpp>
#include <forge/scene_render_settings.hpp>
#include <limits>

inline void test_scene_color_settings() {
    using namespace forge;
    const auto require = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    EngineContext engine;
    Scene scene(engine.world());
    require(scene_render_settings(scene.document()).color_adjustments == SceneColorAdjustments{},
            "Old Scene unexpectedly enabled color adjustments");
    auto extended = scene.document();
    extended["rendering"] = {{"version", 1u},
                             {"exposure", 1},
                             {"plugin", "preserve"},
                             {"color_adjustments", {{"plugin", {{"opaque", 9}}}}}};
    scene.reset(extended);
    const auto before = scene.document();
    authoring_command(
        scene, "scene.rendering.set",
        {{"color_adjustments",
          {{"enabled", true}, {"filter", {1, .5, .25}}, {"saturation", .6}, {"contrast", 1.5}}}});
    const auto adjusted = scene.document();
    require(scene_render_settings(adjusted).color_adjustments ==
                    SceneColorAdjustments{true, {1, .5, .25}, .6f, 1.5f} &&
                adjusted["rendering"]["plugin"] == "preserve" &&
                adjusted["rendering"]["color_adjustments"]["plugin"] ==
                    before["rendering"]["color_adjustments"]["plugin"] &&
                adjusted["rendering"]["exposure"] == 1,
            "Color edits lost settings, unrelated exposure or opaque data");
    require(scene.undo() && scene.document() == before && !scene.undo() && scene.redo() &&
                scene.document() == adjusted,
            "Scene color settings did not use one Undo/Redo");
    authoring_command(scene, "scene.rendering.set", {{"color_adjustments", {{"contrast", 0}}}});
    const auto good = scene.document();
    require(scene_render_settings(good).color_adjustments ==
                SceneColorAdjustments{true, {1, .5, .25}, .6f, 0},
            "Partial color edit reset existing values");
    auto inspected = good;
    inspected["rendering"].erase("plugin");
    inspected["rendering"]["color_adjustments"].erase("plugin");
    const std::array known{AssetReferenceDocument{"Color.scene.json", inspected}};
    require(collect_asset_references(scene.schema(), known).uninspected.empty(),
            "Known color values were incorrectly opaque to export");
    const std::array opaque{AssetReferenceDocument{"Color.scene.json", good}};
    require(collect_asset_references(scene.schema(), opaque).uninspected.size() == 2,
            "Unknown rendering/color extensions were silently marked inspected");
    for (const Json& bad :
         {Json(nullptr), Json(1), Json{{"enabled", 1}}, Json{{"saturation", -.001}},
          Json{{"saturation", 2.00000001}}, Json{{"contrast", "one"}},
          Json{{"contrast", 2.00000001}}, Json{{"filter", Json::array()}}, Json{{"filter", {1, 1}}},
          Json{{"filter", {1, 1, 1, 1}}}, Json{{"filter", {1, -0.001, 1}}},
          Json{{"filter", {1, 1, 4.00000001}}}, Json{{"filter", {1, true, 1}}},
          Json{{"filter", {{"r", 1}, {"g", 1}, {"b", 1}}}}}) {
        const auto revision = scene.revision();
        bool rejected = false;
        try {
            authoring_command(scene, "scene.rendering.set", {{"color_adjustments", bad}});
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected && scene.document() == good && scene.revision() == revision,
                "Invalid color command partially changed Scene/history");
        auto invalid = good;
        invalid["rendering"]["color_adjustments"] = bad;
        rejected = false;
        try {
            (void)scene_render_settings(invalid);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "Saved malformed color settings bypassed parser admission");
    }
    for (const float bad : {-1.f, 4.001f, std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::quiet_NaN()}) {
        for (unsigned field = 0; field < 5; ++field) {
            SceneColorAdjustments value;
            if (field < 3)
                value.filter[field] = bad;
            else
                (field == 3 ? value.saturation : value.contrast) = bad;
            bool rejected = false;
            try {
                validate_scene_color_adjustments(value);
            } catch (const std::exception&) {
                rejected = true;
            }
            require(rejected, "Native color settings accepted a nonfinite/out-of-range field");
        }
    }
    validate_scene_color_adjustments({true, {0, 4, 1}, 0, 2});
    const auto path =
        std::filesystem::current_path() / ("color-" + AssetId::generate().str() + ".scene.json");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } cleanup{path};
    scene.save(path);
    Scene reopened(engine.world());
    reopened.load(path);
    require(reopened.document() == good &&
                scene_render_settings(reopened.document()).color_adjustments ==
                    scene_render_settings(good).color_adjustments,
            "Scene color adjustments did not survive ordinary Save/load");
}
