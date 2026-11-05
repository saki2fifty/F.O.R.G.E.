#pragma once
#include "asset_reference_impact.hpp"
#include <forge/authoring.hpp>
#include <forge/scene_render_settings.hpp>
#include <limits>
inline void test_scene_morphological_aa_settings() {
    using namespace forge;
    const auto check = [](bool value, const char* message) {
        if (!value)
            throw std::runtime_error(message);
    };
    EngineContext engine;
    Scene scene(engine.world());
    const auto legacy = scene.document();
    check(scene_render_settings(legacy).morphological_anti_aliasing ==
                  SceneMorphologicalAntiAliasing{} &&
              scene.document() == legacy,
          "Legacy Scene read materialized morphological defaults");
    auto initial = legacy;
    initial["rendering"] = {{"version", 1u},
                            {"morphological_anti_aliasing", {{"extension", {{"future", true}}}}}};
    scene.reset(initial);
    authoring_command(scene, "scene.rendering.set",
                      {{"morphological_anti_aliasing",
                        {{"enabled", true},
                         {"edge_threshold", .25},
                         {"search_steps", 24},
                         {"corner_rounding", .5}}}});
    const auto active = scene.document();
    check(scene_render_settings(active).morphological_anti_aliasing ==
                  SceneMorphologicalAntiAliasing{true, .25f, 24, .5f} &&
              scene.undo() && scene.document() == initial && scene.redo() &&
              scene.document() == active,
          "Morphological settings lost values or atomic Undo/Redo");
    authoring_command(scene, "scene.rendering.set",
                      {{"morphological_anti_aliasing", {{"search_steps", 32}}}});
    const auto good = scene.document();
    check(good["rendering"]["morphological_anti_aliasing"]["extension"] ==
                  initial["rendering"]["morphological_anti_aliasing"]["extension"] &&
              scene_render_settings(good).morphological_anti_aliasing.search_steps == 32,
          "Partial morphological patch lost unrelated metadata");
    for (const Json& bad :
         {Json(nullptr), Json(2), Json{{"enabled", 1}}, Json{{"edge_threshold", .009999999999}},
          Json{{"edge_threshold", .50000000001}}, Json{{"corner_rounding", -1e-10}},
          Json{{"corner_rounding", 1.00000000001}}, Json{{"search_steps", 0}},
          Json{{"search_steps", 33}}, Json{{"search_steps", 1.5}}, Json{{"search_steps", true}}}) {
        bool rejected = false;
        const auto revision = scene.revision();
        try {
            authoring_command(scene, "scene.rendering.set", {{"morphological_anti_aliasing", bad}});
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && scene.document() == good && scene.revision() == revision,
              "Invalid morphological patch mutated Scene or revision");
    }
    bool rejected = false;
    try {
        authoring_command(scene, "scene.rendering.set",
                          {{"fast_approximate_anti_aliasing", {{"enabled", true}}}});
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected && scene.document() == good,
          "Competing spatial AA methods were silently combined");
    authoring_command(scene, "scene.rendering.set",
                      {{"fast_approximate_anti_aliasing", {{"enabled", true}}},
                       {"morphological_anti_aliasing", {{"enabled", false}}}});
    check(scene_render_settings(scene.document()).fast_approximate_anti_aliasing.enabled &&
              scene.undo() && scene.document() == good,
          "Explicit spatial method switch was not one reversible Scene edit");
    for (const auto bad :
         {SceneMorphologicalAntiAliasing{true, std::numeric_limits<float>::quiet_NaN(), 16, 0},
          SceneMorphologicalAntiAliasing{true, .1f, 0, 0},
          SceneMorphologicalAntiAliasing{false, .1f, 33, 0},
          SceneMorphologicalAntiAliasing{false, .1f, 16, std::numeric_limits<float>::infinity()}}) {
        bool invalid = false;
        try {
            validate_scene_morphological_anti_aliasing(bad);
        } catch (const std::exception&) {
            invalid = true;
        }
        check(invalid, "Native morphological settings bypassed validation");
    }
    auto known = good;
    known["rendering"]["morphological_anti_aliasing"].erase("extension");
    const std::array plain{AssetReferenceDocument{"Morph.scene.json", known}},
        extended{AssetReferenceDocument{"Morph.scene.json", good}};
    check(collect_asset_references(scene.schema(), plain).uninspected.empty() &&
              collect_asset_references(scene.schema(), extended).uninspected.size() == 1,
          "Morphological known fields or opaque extensions bypassed reference inspection");
    Scene reopened(engine.world());
    reopened.reset(good);
    check(reopened.document() == good, "Scene reopen lost morphological settings");
    bool discovered = false;
    for (const auto& operation : authoring_commands())
        if (operation.at("id") == "scene.rendering.set") {
            const auto& field =
                operation.at("input_schema").at("properties").at("morphological_anti_aliasing");
            discovered = field.at("properties").at("search_steps").at("maximum") == 32 &&
                         field.at("properties").at("edge_threshold").at("minimum") == .01;
        }
    check(discovered, "Morphological settings are missing from shared authoring/MCP discovery");
}
