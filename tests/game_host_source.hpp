#pragma once
#include <forge/engine_assets.hpp>
#include <forge/game_settings.hpp>
#include <forge/project.hpp>
#include <forge/render_components.hpp>
#include <forge/scene.hpp>
#include <forge/ui_assets.hpp>
namespace forge::test {
inline std::filesystem::path make_game_host_project(const std::filesystem::path& output) {
    const auto project = output / ("project-" + AssetId::generate().str());
    std::filesystem::create_directories(project);
    WorldContext world;
    Scene scene(world);
    scene.replace(
        {{"version", 1},
         {"entities",
          Json::array({{{"id", "camera"}, {"name", "Camera"}, {"components", Json::object()}},
                       {{"id", "cube"},
                        {"name", "Cube"},
                        {"components", {{"forge.position", {{"x", 0}, {"y", 0}, {"z", 3}}}}}},
                       {{"id", "light"}, {"name", "Light"}, {"components", Json::object()}},
                       {{"id", "hud"}, {"name", "HUD"}, {"components", Json::object()}}})}});
    Camera camera;
    camera.background_r = .025f;
    camera.background_g = .035f;
    camera.background_b = .055f;
    scene.entity("camera").set(camera);
    scene.entity("camera").set<LocalTranslation>({});
    scene.entity("camera").set<Primitive>({no_primitive});
    scene.entity("light").set<LocalTranslation>({0, 3, 0});
    scene.entity("light").set<Primitive>({no_primitive});
    scene.entity("light").set<Light>({});
    scene.entity("cube").set<MeshRenderer>({engine_primitive(0), {{"surface", engine_material()}}});
    const std::string markup = R"rml(<rml><head><style>
body { width:100%; height:100%; font-family:Lato; font-size:20px; color:#eeeeee; pointer-events:none; }
button { position:absolute; left:24px; width:160px; height:40px; line-height:40px; text-align:center; background-color:#305070; pointer-events:auto; }
#pause { top:20px; } #resume { top:72px; } h1 { position:absolute; left:24px; right:24px; top:130px; font-size:24px; }
#status { position:absolute; left:24px; right:24px; top:190px; }
</style></head><body><button id="pause" data-event-click="command('Pause')">Pause</button>
<button id="resume" data-event-click="command('Resume')">Resume</button>
<h1>FORGE standalone</h1><div id="status">Paused: {{paused}} | Tick: {{tick}}</div></body></rml>)rml";
    atomic_write(project / "hud.rml", markup);
    const auto ui = register_ui_document(project, "hud.rml");
    scene.entity("hud").set<UiDocument>({{ui.id}});
    scene.save(project / "main.scene.json");
    auto catalog = AssetCatalog::open_project(project);
    const auto asset = catalog.add_scene("main.scene.json");
    catalog.save(AssetCatalog::project_index(project));
    auto settings = ProjectSettings::defaults("Standalone acceptance");
    settings["startup_scene"] = {{"asset", asset.id}, {"source", "main.scene.json"}};
    settings["game"] = default_game_settings("org.forge.fixture-" + AssetId::generate().str(),
                                             "FORGE standalone acceptance");
    settings["game"]["display"]["width"] = 960;
    settings["game"]["display"]["height"] = 540;
    atomic_write(project / "forge.project.json", settings.dump(2));
    return project;
}
} // namespace forge::test
