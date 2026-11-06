#pragma once
#include "editor/document.hpp"
#include "runtime_package.hpp"
#include <forge/authoring.hpp>
#include <forge/ui_assets.hpp>

namespace forge::test {
inline constexpr auto game_recording_scene_path = "Scenes/182-game-recording.scene.json";
inline AssetId author_game_recording_gallery(const std::filesystem::path& root,
                                             const RuntimeUiInspector& inspect) {
    if (std::filesystem::exists(root / game_recording_scene_path))
        throw std::runtime_error("Game recording Gallery scene already exists");
    const auto before = AssetCatalog::open_project(root).document().at("assets");
    const auto ui_path = root / "Assets/GameRecording/hud.rml";
    std::filesystem::create_directories(ui_path.parent_path());
    asset_storage::replace(ui_path, R"rml(<rml><head><style>
body {width:100%;height:100%;margin:0;font-family:Lato;color:#f0f5fa;pointer-events:none;}
body,div,h1,p {display:block;}
#panel {position:absolute;right:4%;top:4%;width:38%;min-width:210px;max-width:320px;box-sizing:border-box;padding:12px;background-color:#132c42e8;}
h1 {font-size:16px;margin:0 0 6px;} p {font-size:12px;margin:5px 0;}
#pulse {width:32px;height:10px;background-color:#f4c353;animation:1s linear-in infinite alternate pulse;}
@keyframes pulse {from {width:32px;} to {width:160px;}}
</style></head><body><div id="panel"><h1>FORGE / GAME RECORDING</h1>
<p>Motion, gravity and timed tones.</p>
<p>This pulse follows recorded time.</p><div id="pulse"></div>
</div></body></rml>)rml");
    const auto ui = register_ui_document(root, "Assets/GameRecording/hud.rml");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument document(scene);
    document.open_project(root);
    document.open_scene("Scenes/70-sequence-audio-cues.scene.json");
    const auto original = scene.asset_id();
    const auto hud =
        authoring_command(scene, "entity.create",
                          {{"recipe", "empty"}, {"name", "HUD - recorded presentation time"}})
            .at("selected")
            .get<std::string>();
    auto candidate = scene.document();
    unsigned bodies = 0, huds = 0;
    for (auto& entity : candidate.at("entities")) {
        auto& c = entity.at("components");
        const auto name = entity.at("name").get<std::string>();
        if (name.starts_with("Red -") || name == "Floor") {
            const bool moving = name.starts_with("Red -");
            c["forge.box_collider"] = {{"x", 1.}, {"y", 1.}, {"z", 1.}};
            c["forge.physics_body"] = {{"density", 1000.},
                                       {"enabled", true},
                                       {"friction", .5},
                                       {"gravity_factor", 1.},
                                       {"layer", 0},
                                       {"mask", UINT32_MAX},
                                       {"mass", moving ? 1. : 0.},
                                       {"motion", moving ? 2 : 0},
                                       {"restitution", .2},
                                       {"sensor", false}};
            if (moving) {
                entity["spatial"] = {{"mode", "world"}};
                entity["name"] = "Red cube - gravity and first tone";
                c["forge.local_translation"]["y"] = 4.;
                c["forge.local_scale"] = {{"x", 1.}, {"y", 1.}, {"z", 1.}};
            }
            ++bodies;
        }
        if (entity.at("id") == hud) {
            c["forge.ui_document"] = {
                {"document", ui.id}, {"enabled", true}, {"layer", 0}, {"visible", true}};
            ++huds;
        }
        if (c.contains("forge.animation_sequence"))
            entity["name"] = "Six-second game sequence - motion, cameras and tones";
    }
    if (bodies != 2 || huds != 1)
        throw std::runtime_error("Recording Gallery needs its floor, falling cube and HUD");
    scene.edit(candidate);
    document.save_as(game_recording_scene_path);
    if (scene.asset_id() == original ||
        scene.document().dump().find(original.str()) != std::string::npos)
        throw std::runtime_error(
            "Recording Gallery retained the source Scene's local reference scope");
    auto catalog = AssetCatalog::open_project(root);
    const auto record = catalog.add_scene(game_recording_scene_path);
    catalog.save(AssetCatalog::project_index(root));
    const std::array roots{record.id};
    prepare_runtime_content_catalog(*document.writer_guard(), roots, inspect, scene.schema());
    const auto after = AssetCatalog::open_project(root).document().at("assets");
    for (const auto& row : before)
        if (std::find(after.begin(), after.end(), row) == after.end())
            throw std::runtime_error("Recording Gallery changed a prior asset record");
    return record.id;
}
} // namespace forge::test
