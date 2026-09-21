#pragma once
#include <forge/authoring.hpp>
#include <forge/render_scene.hpp>
inline void test_render_scene() {
    using namespace forge;
    auto require = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    auto rejects = [&](auto fn) {
        bool caught = false;
        try {
            fn();
        } catch (const std::exception&) {
            caught = true;
        }
        require(caught, "Invalid render snapshot accepted");
    };
    EngineContext engine;
    Scene scene(engine.world());
    const auto scene_id = AssetId::generate();
    const auto first = EntityId::parse("10000000-0000-4000-8000-000000000001");
    const auto second = EntityId::parse("10000000-0000-4000-8000-000000000002");
    const auto third = EntityId::parse("10000000-0000-4000-8000-000000000003");
    Json components = Json::object();
    const auto schema = scene.schema();
    for (const auto& c : schema.at("components"))
        if (c.at("id") == "forge.camera" || c.at("id") == "forge.light" ||
            c.at("id") == "forge.mesh_renderer" || c.at("id") == "forge.local_translation" ||
            c.at("id") == "forge.local_rotation" || c.at("id") == "forge.local_scale") {
            Json fields = Json::object();
            for (const auto& f : c.at("fields"))
                fields[f.at("id").get<std::string>()] = f.at("default");
            components[c.at("id").get<std::string>()] = std::move(fields);
        }
    const auto mesh = AssetId::generate();
    components["forge.mesh_renderer"]["mesh"] = mesh;
    Json doc{{"version", 3}, {"asset_id", scene_id}, {"entities", Json::array()}};
    for (auto id : {third, second, first})
        doc["entities"].push_back({{"id", id}, {"name", "View"}, {"components", components}});
    doc["entities"][0]["components"]["forge.camera"]["order"] = -2;
    scene.reset(doc);
    const auto environment = AssetId::generate();
    authoring_command(
        scene, "scene.rendering.set",
        {{"exposure", 2},
         {"environment",
          {{"texture", environment}, {"intensity", .5}, {"rotation", -1.25}, {"sky", false}}}});
    const auto settings = scene_render_settings(scene.document());
    require(settings.environment.texture.id == environment && settings.exposure == 2 &&
                settings.environment.intensity == .5f && settings.environment.rotation == -1.25 &&
                !settings.environment.sky,
            "Scene rendering settings lost authored values");
    require(scene.undo() && !scene.document().contains("rendering") && scene.redo() &&
                scene_render_settings(scene.document()) == settings,
            "Scene rendering history did not restore settings");
    auto unknown = scene.document();
    unknown["rendering"]["plugin"] = {{"unrecognized", 42}};
    unknown["rendering"]["environment"]["plugin"] = "retained";
    scene.reset(unknown);
    authoring_command(scene, "scene.rendering.set", {{"environment", {{"intensity", 3}}}});
    require(scene.document()["rendering"]["plugin"] == unknown["rendering"]["plugin"] &&
                scene.document()["rendering"]["environment"]["plugin"] == "retained" &&
                scene_render_settings(scene.document()).environment.texture.id == environment,
            "Environment patch rewrote unrelated authored metadata");
    const auto good_settings = scene.document();
    for (const auto& patch :
         {Json{{"exposure", 21}}, Json{{"environment", {{"intensity", -1}}}},
          Json{{"environment", {{"texture", "wrong-id"}}}}, Json{{"environment", {{"sky", 1}}}}}) {
        rejects([&] { authoring_command(scene, "scene.rendering.set", patch); });
        require(scene.document() == good_settings, "Rejected environment edit changed scene");
    }
    auto bad_settings = good_settings;
    bad_settings["rendering"]["version"] = 2u;
    rejects([&] { scene.edit(bad_settings); });
    require(extract_render_scene(bad_settings).diagnostics.at(0).category ==
                "render.settings.invalid",
            "Malformed producer settings lacked a structured diagnostic");
    require(scene_render_settings(scene.effective_document()) ==
                scene_render_settings(scene.document()),
            "Effective scene dropped global render settings");
    const auto original = scene.document();
    auto source = scene.effective_document();
    auto snapshot = extract_render_scene(source);
    require(snapshot.scene == scene_id && snapshot.cameras.size() == 3 &&
                snapshot.lights.size() == 3 && snapshot.meshes.size() == 3 &&
                snapshot.diagnostics.empty(),
            "Render extraction lost native copied components");
    require(snapshot.cameras[0].entity == third && snapshot.cameras[1].entity == first &&
                snapshot.cameras[2].entity == second,
            "Camera composition is not deterministic by order/identity");
    source["entities"][0]["components"]["forge.camera"]["order"] = 10;
    require(snapshot.cameras[0].camera.order == -2,
            "Detached render snapshot retained mutable source alias");
    const auto views = prepare_game_cameras(snapshot, 800, 600);
    require(views.cameras.size() == 3 && views.diagnostics.empty() &&
                views.cameras[0].entity == third,
            "Game camera preparation lost selection");
    auto broken = scene.effective_document();
    broken["entities"][0]["components"]["forge.camera"]["near_plane"] = 0;
    broken["entities"][1]["components"]["forge.light"]["intensity"] = -1;
    broken["entities"][2]["components"]["forge.mesh_renderer"]["layers"] = -1;
    auto partial = extract_render_scene(broken);
    require(partial.cameras.size() == 2 && partial.lights.size() == 2 &&
                partial.meshes.size() == 2 && partial.diagnostics.size() == 3 &&
                partial.diagnostics[0].context.entity == third &&
                partial.diagnostics[0].context.asset == scene_id &&
                partial.diagnostics[0].context.property == "forge.camera",
            "Invalid producer data did not create bounded contextual diagnostics");
    require(scene.document() == original, "Render admission changed authored scene or history");
    broken = scene.effective_document();
    broken["entities"][0]["world_affine"][0] = 0;
    partial = extract_render_scene(broken);
    const auto valid = prepare_game_cameras(partial, 800, 600);
    require(valid.cameras.size() == 2 && valid.diagnostics.size() == 1 &&
                valid.diagnostics[0].context.entity == third,
            "Collapsed camera passed projection admission");
    broken = scene.effective_document();
    for (auto& row : broken["entities"]) {
        row["components"]["forge.camera"]["enabled"] = false;
        row["components"]["forge.light"]["enabled"] = false;
        row["components"]["forge.mesh_renderer"]["visible"] = false;
    }
    partial = extract_render_scene(broken);
    const auto empty = prepare_game_cameras(partial, 800, 600);
    require(partial.cameras.empty() && partial.lights.empty() && partial.meshes.empty() &&
                empty.cameras.empty() && empty.diagnostics.size() == 1 &&
                empty.diagnostics[0].category == "render.camera.missing",
            "Disabled views were rendered or missing game camera silently substituted");
    broken = scene.effective_document();
    broken["entities"][0]["prefab"] = true;
    require(extract_render_scene(broken).cameras.size() == 2,
            "Prefab declaration became a live camera");
    broken = scene.effective_document();
    broken["entities"][0]["id"] = first;
    rejects([&] { extract_render_scene(broken); });
    broken = scene.effective_document();
    broken["asset_id"] = "bad-id";
    rejects([&] { extract_render_scene(broken); });
    // Match the shared diagnostics service's 256-item budget and retain an
    // explicit omitted count when a native producer supplies many bad values.
    broken = scene.effective_document();
    auto row = broken["entities"][0];
    row["spatial_resolved"] = false;
    broken["entities"] = Json::array();
    for (unsigned i = 0; i < 100; ++i) {
        row["id"] = EntityId::generate();
        broken["entities"].push_back(row);
    }
    partial = extract_render_scene(broken);
    require(partial.diagnostics.size() == 256 && partial.omitted_diagnostics == 44,
            "Render diagnostics escaped bounded retention");
}
