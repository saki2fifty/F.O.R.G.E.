#pragma once
#include <forge/authoring.hpp>
void check(bool value, const char* why);
inline void test_property_batch() {
    using namespace forge;
    EngineContext engine;
    Scene scene(engine.world());
    const auto id = authoring_command(scene, "entity.create", {{"recipe", "animation.camera_shot"}})
                        .at("selected");
    auto initial = scene.document();
    initial["entities"][0]["components"]["forge.camera_shot"]["plugin"] = {{"opaque", 17}};
    scene.reset(initial);
    const auto before = scene.document();
    bool rejected = false;
    try {
        authoring_command(scene, "property.set",
                          {{"entity", id},
                           {"component", "forge.camera_shot"},
                           {"field", "start_seconds"},
                           {"value", 2}});
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected && scene.document() == before, "Single-field interval oracle did not reject");
    const auto arguments = [&](Json values) {
        return Json{{"entity", id}, {"component", "forge.camera_shot"}, {"values", values}};
    };
    const Json commands =
        Json::array({{{"operation", "property.set_many"},
                      {"arguments", arguments({{"start_seconds", 2}, {"end_seconds", 3}})}}});
    const auto candidate = preview_authoring(scene, commands);
    check(scene.document() == before &&
              candidate["entities"][0]["components"]["forge.camera_shot"]["start_seconds"] == 2 &&
              candidate["entities"][0]["components"]["forge.camera_shot"]["end_seconds"] == 3,
          "Multi-field preview did not validate the final interval without publishing");
    AuthoringSession api(scene);
    const auto revision = scene.revision();
    const auto result = api.handle({{"api", 1},
                                    {"method", "scene.apply"},
                                    {"target", api.target()},
                                    {"expected_revision", revision},
                                    {"commands", commands}});
    check(result.at("ok") && scene.document() == candidate && scene.revision() == revision + 1 &&
              scene.document()["entities"][0]["components"]["forge.camera_shot"]["plugin"] ==
                  Json{{"opaque", 17}},
          "Headless multi-field publication lost atomicity or opaque component data");
    check(scene.undo() && scene.document() == before && !scene.undo(),
          "Multi-field publication did not create exactly one Undo");
    check(scene.redo() && scene.document() == candidate, "Multi-field Redo lost authored intent");
    Json oversized = Json::object();
    for (unsigned i = 0; i < 129; ++i)
        oversized["field" + std::to_string(i)] = i;
    for (const auto& invalid :
         {Json::array({1}), Json::object(), oversized, Json{{"future_field", 1}},
          Json{{"start_seconds", "invalid"}}, Json{{"start_seconds", 5}, {"end_seconds", 3}}}) {
        rejected = false;
        const auto last_revision = scene.revision();
        try {
            authoring_command(scene, "property.set_many", arguments(invalid));
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && scene.document() == candidate && scene.revision() == last_revision,
              "Invalid multi-field candidate partially changed the Scene");
    }
}
