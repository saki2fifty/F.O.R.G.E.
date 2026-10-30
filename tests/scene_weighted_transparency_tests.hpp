#pragma once
#include "asset_reference_impact.hpp"
#include <forge/authoring.hpp>
#include <forge/scene_render_settings.hpp>
inline void test_scene_weighted_transparency() {
    using namespace forge;
    const auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    EngineContext engine;
    Scene scene(engine.world());
    check(!scene_render_settings(scene.document()).weighted_transparency,
          "Legacy scenes no longer default to sorted transparency");
    auto original = scene.document();
    original["rendering"] = {{"version", 1u}, {"extension", {{"retain", 17}}}};
    scene.reset(original);
    authoring_command(scene, "scene.rendering.set", {{"weighted_transparency", true}});
    const auto good = scene.document();
    check(scene_render_settings(good).weighted_transparency &&
              good["rendering"]["extension"] == original["rendering"]["extension"] &&
              scene.undo() && scene.document() == original && scene.redo() &&
              scene.document() == good,
          "Weighted transparency bypassed source history or lost extensions");
    for (const auto& bad :
         {Json(0), Json(nullptr), Json("weighted"), Json::object(), Json::array()}) {
        const auto revision = scene.revision();
        bool rejected = false;
        try {
            authoring_command(scene, "scene.rendering.set", {{"weighted_transparency", bad}});
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && scene.revision() == revision && scene.document() == good,
              "Rejected transparency setting changed source or revision");
        auto document = good;
        document["rendering"]["weighted_transparency"] = bad;
        rejected = false;
        try {
            (void)scene_render_settings(document);
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, "Native transparency extraction accepted a nonboolean setting");
    }
    auto known = good;
    known["rendering"].erase("extension");
    const std::array inspected{AssetReferenceDocument{"Weighted.scene.json", known}};
    check(collect_asset_references(scene.schema(), inspected).uninspected.empty(),
          "Known transparency value became an uninspected export dependency");
    bool discovered = false;
    for (const auto& command : authoring_commands())
        if (command.at("id") == "scene.rendering.set")
            discovered = command.at("input_schema")
                             .at("properties")
                             .at("weighted_transparency")
                             .at("type") == "boolean";
    check(discovered, "Transparency absent from shared automation/MCP discovery");
    Scene reopened(engine.world());
    reopened.reset(good);
    check(reopened.document() == good &&
              scene_render_settings(reopened.document()).weighted_transparency,
          "Transparency setting failed source reopen");
}
