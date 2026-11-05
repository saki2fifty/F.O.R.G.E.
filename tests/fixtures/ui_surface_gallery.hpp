#pragma once
#include "asset_storage.hpp"
#include "runtime_package.hpp"
#include <forge/authoring.hpp>
#include <forge/project_lease.hpp>
#include <forge/render_components.hpp>
#include <forge/ui_assets.hpp>
#include <forge/ui_components.hpp>
#include <fstream>

namespace forge::test {
inline AssetId author_ui_surface_gallery(const std::filesystem::path& root,
                                         const RuntimeUiInspector& inspect) {
    const auto directory = root / "Assets/UI/SceneSurface";
    std::filesystem::create_directories(directory);
    asset_storage::replace(directory / "Panel.rml", R"rml(<rml><head><style>
body,div,h1,p {display:block;}
body {margin:0;width:100%;height:100%;background-color:#102438f5;font-family:Lato;font-size:18px;color:#e7f4ff;}
#panel {box-sizing:border-box;padding:20px;width:100%;height:100%;border:3px #36e5a4;}
h1 {font-size:28px;color:#67f1b9;margin:0 0 10px;}p {margin:9px 0;}
button {display:inline-block;padding:9px 12px;margin:0 8px 0 0;background-color:#30536e;tab-index:auto;}
button:hover,button:focus {background-color:#467c99;}
input {display:block;width:90%;height:27px;margin-top:12px;background-color:#203c54;color:white;}
#hint {font-size:16px;color:#adc7db;}
</style></head><body><div id="panel"><h1>Interactive scene surface</h1>
<p>Tick: {{tick}} | Paused: {{paused}}</p>
<button data-event-click="command('Pause')">Pause</button><button data-event-click="command('Resume')">Resume</button><button data-event-click="command('Step')">Step</button>
<input type="text" value="Click here and type"/>
<p id="hint">Tab selects controls after focusing this panel.</p></div></body></rml>)rml");
    asset_storage::replace(directory / "Guide.rml", R"rml(<rml><head><style>
body,div {display:block;pointer-events:none;font-family:Lato;color:#e7f4ff;}
body {margin:0;width:100%;height:100%;}
#title {position:absolute;left:3%;top:5%;font-size:24px;color:#67f1b9;}
#guide {position:absolute;left:3%;bottom:4%;width:94%;padding:8px;box-sizing:border-box;font-size:16px;background-color:#102438e8;}
</style></head><body><div id="title">UI on a scene surface</div><div id="guide">LEFT: interactive Quad. Click Pause, Step, Resume or the text field.<br/>RIGHT: display-only relay of the same output.<br/>Capture Game input first. Save the Scene and publish resources before exporting.</div></body></rml>)rml");
    const auto panel = register_ui_document(root, "Assets/UI/SceneSurface/Panel.rml");
    const auto guide = register_ui_document(root, "Assets/UI/SceneSurface/Guide.rml");
    Json source;
    std::ifstream(root / "Scenes/154-camera-output-ui.scene.json") >> source;
    EngineContext engine;
    Scene scene(engine.world());
    source["asset_id"] = scene.asset_id();
    auto entities = Json::array();
    for (auto row : source.at("entities"))
        if (!row.at("components").contains("forge.ui_document")) {
            row["id"] = EntityId::generate();
            entities.push_back(std::move(row));
        }
    source["entities"] = std::move(entities);
    scene.restore_snapshot(source);
    AssetId output;
    for (const auto& row : source.at("entities")) {
        auto actor = scene.entity(row.at("id").get<std::string>());
        if (actor.has<Camera>() && actor.get<Camera>().layers == 1)
            output = actor.get<Camera>().render_texture.id;
    }
    if (!output)
        throw std::runtime_error("Surface Gallery source camera is unavailable");
    for (const auto& row : source.at("entities")) {
        auto actor = scene.entity(row.at("id").get<std::string>());
        if (actor.has<MeshRenderer>() && actor.get<MeshRenderer>().layers == 4)
            actor.set<UiSurfaceInput>({{output}, true});
    }
    const auto create = [&](const char* name) {
        return authoring_command(scene, "entity.create", {{"recipe", "empty"}, {"name", name}})
            .at("selected")
            .get<std::string>();
    };
    scene.entity(create("Controls - interactive Split output"))
        .set<UiDocument>({{panel.id}, true, true, 0, {output}});
    scene.entity(create("Guide - main Game view")).set<UiDocument>({{guide.id}, true, true, 0});
    const std::filesystem::path path = "Scenes/174-interactive-ui-surfaces.scene.json";
    scene.save(root / path);
    auto catalog = AssetCatalog::open_project(root);
    const auto record = catalog.add_scene(path);
    catalog.save(AssetCatalog::project_index(root));
    const std::array roots{record.id};
    prepare_runtime_content_catalog(ProjectLease(root), roots, inspect, scene.schema());
    return record.id;
}
} // namespace forge::test
