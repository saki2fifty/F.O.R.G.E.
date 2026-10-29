#pragma once
#include "asset_reference_impact.hpp"
#include "depth_of_field_plan.hpp"
#include <forge/authoring.hpp>
#include <limits>

inline void test_scene_depth_of_field() {
    using namespace forge;
    const auto check = [](bool value, const char* why) {
        if (!value)
            throw std::runtime_error(why);
    };
    const auto rejects = [&](auto action) {
        bool rejected = false;
        try {
            action();
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, "Invalid depth of field input admitted");
    };
    EngineContext engine;
    Scene scene(engine.world());
    check(scene_render_settings(scene.document()).depth_of_field == SceneDepthOfField{},
          "Legacy scene does not retain default-off focus settings");
    auto original = scene.document();
    original["rendering"] = {{"version", 1u}, {"depth_of_field", {{"extension", 47}}}};
    scene.reset(original);
    authoring_command(
        scene, "scene.rendering.set",
        {{"depth_of_field",
          {{"enabled", true}, {"focus_distance", 14}, {"focus_range", 3}, {"blur_radius", 6}}}});
    const auto enabled = scene.document();
    check(scene_render_settings(enabled).depth_of_field == SceneDepthOfField{true, 14, 3, 6} &&
              scene.undo() && scene.document() == original && scene.redo() &&
              scene.document() == enabled,
          "Focus values/history did not use the existing scene owner");
    authoring_command(scene, "scene.rendering.set", {{"depth_of_field", {{"focus_distance", 10}}}});
    const auto good = scene.document();
    check(scene_render_settings(good).depth_of_field == SceneDepthOfField{true, 10, 3, 6} &&
              good["rendering"]["depth_of_field"]["extension"] == 47,
          "Partial focus edit lost existing fields or extension");
    for (const auto& bad :
         {Json(1), Json(nullptr), Json{{"enabled", 1}}, Json{{"focus_distance", 0}},
          Json{{"focus_range", .00099999999}}, Json{{"focus_distance", 1000000.00001}},
          Json{{"blur_radius", 12.00000001}}, Json{{"blur_radius", -1}},
          Json{{"focus_range", "wide"}}}) {
        const auto revision = scene.revision();
        rejects(
            [&] { authoring_command(scene, "scene.rendering.set", {{"depth_of_field", bad}}); });
        check(scene.document() == good && scene.revision() == revision,
              "Rejected focus edit mutated authored values/revision");
    }
    for (unsigned field = 0; field < 3; ++field)
        for (auto bad :
             {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
            SceneDepthOfField value;
            if (field == 0)
                value.focus_distance = bad;
            if (field == 1)
                value.focus_range = bad;
            if (field == 2)
                value.blur_radius = bad;
            rejects([&] { validate_scene_depth_of_field(value); });
        }
    auto known = good;
    known["rendering"]["depth_of_field"].erase("extension");
    const std::array known_docs{AssetReferenceDocument{"Focus.scene.json", known}};
    const std::array extended_docs{AssetReferenceDocument{"Focus.scene.json", good}};
    check(collect_asset_references(scene.schema(), known_docs).uninspected.empty() &&
              collect_asset_references(scene.schema(), extended_docs).uninspected.size() == 1,
          "Focus reference inspection hid extension or misunderstood known numeric fields");
    Scene reopened(engine.world());
    reopened.reset(good);
    check(reopened.document() == good, "Reopening lost Scene focus values");
    bool discovered = false;
    for (const auto& operation : authoring_commands())
        if (operation.at("id") == "scene.rendering.set")
            discovered = operation.at("input_schema").at("properties").contains("depth_of_field");
    check(discovered, "Focus workflow missing from shared automation/MCP discovery");
    CameraView view{};
    view.viewport = {2, 3, 7, 5};
    view.projection[10] = .01f;
    view.projection[11] = -.01f;
    view.projection[15] = 1;
    const auto plan = depth_of_field_plan(11, 11, view, 16, 560);
    check(plan.bytes == 560 && plan.area == view.viewport,
          "Focus budget/rectangle calculation changed");
    rejects([&] { depth_of_field_plan(11, 11, view, 16, 559); });
    rejects([&] { depth_of_field_plan(8, 11, view, 16, 1000); });
    rejects([&] { depth_of_field_plan(11, 11, view, 8, 1000); });
    view.projection[11] = 0;
    rejects([&] { depth_of_field_plan(11, 11, view, 16, 1000); });
    view.projection[10] = 1;
    view.projection[11] = -.1f;
    view.projection[14] = 1;
    view.projection[15] = 0;
    check(depth_of_field_plan(11, 11, view, 16, 1000).bytes == 560,
          "Infinite-far projection rejected");
    view.viewport = {std::numeric_limits<unsigned>::max(), 0, 7, 5};
    rejects([&] { depth_of_field_plan(11, 11, view, 16, 1000); });
}
