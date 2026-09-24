#pragma once
#include "ui_probe.hpp"
#include <SDL3/SDL.h>
#include <bit>
#include <cmath>
#include <forge/scene.hpp>
#include <fstream>
#include <functional>
#include <stdexcept>

namespace forge::test {
// Scene state is observed, never edited through commands or model APIs.
// Inputs drive authoring; raw files supply a DCC import/reimport scenario.
class EditorInputWorkflow {
    enum class Kind { Click, Hover, Text, Key, Check, Capture, DropFile, SourceEdit };
    struct Step {
        Kind kind;
        std::string value;
        ImGuiKey key = ImGuiKey_None;
        bool control = false;
    };
    std::vector<Step> steps_;
    std::size_t index_ = 0;
    unsigned frame_ = 0;
    Uint64 since_ = 0;
    ImVec2 pointer_{-FLT_MAX, -FLT_MAX};
    std::string failure_, last_check_, cube_, camera_, light_, scene_;
    std::uint64_t paused_tick_ = 0;
    Json cache_scene_, saved_, before_model_, initial_material_, trace_ = Json::array();
    std::filesystem::path external_source_, project_;
    std::string drop_path_, model_asset_, model_root_, material_asset_;
    std::uint64_t model_generation_ = 0;
    static Json model_source(bool changed = false) {
        auto source = Json::parse(R"({"asset":{"version":"2.0"},
            "extensionsUsed":["KHR_materials_unlit"],
            "buffers":[{"uri":"workflow.bin","byteLength":36}],
            "bufferViews":[{"buffer":0,"byteLength":36}],
            "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3",
                "min":[-1,-1,0],"max":[1,1,0]}],
            "materials":[{"name":"Workflow surface","doubleSided":true,
                "extensions":{"KHR_materials_unlit":{}},
                "pbrMetallicRoughness":{"baseColorFactor":[0.8,0.2,0.04,1]}}],
            "meshes":[{"name":"Workflow triangle","primitives":[{"attributes":{"POSITION":0},"material":0}]}],
            "nodes":[{"name":"Imported triangle","mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})");
        if (changed)
            source["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"] = {.05, .8, .25, 1};
        return source;
    }
    static void write(const std::filesystem::path& path, const std::string& bytes) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(bytes.data(), std::streamsize(bytes.size()));
        file.close();
        if (!file)
            throw std::runtime_error("Cannot write external workflow source");
    }
    void click(std::string target, bool ctrl = false) {
        steps_.push_back({Kind::Click, std::move(target), ImGuiKey_None, ctrl});
    }
    void hover(std::string target) { steps_.push_back({Kind::Hover, std::move(target)}); }
    void check(std::string what) { steps_.push_back({Kind::Check, std::move(what)}); }
    void capture(std::string name) { steps_.push_back({Kind::Capture, std::move(name)}); }
    void key(ImGuiKey key, bool ctrl = false) { steps_.push_back({Kind::Key, {}, key, ctrl}); }
    void text(std::string target, std::string value, bool ctrl = false) {
        click(std::move(target), ctrl);
        steps_.push_back({Kind::Text, std::move(value)});
    }
    void create(const std::string& category, const std::string& recipe) {
        click("menu:Entity");
        hover("menu:Create");
        hover("category:" + category);
        capture("create-" + recipe + "-menu");
        click("action:Create / " + recipe);
    }
    static void require(bool ok, const char* reason) {
        if (!ok)
            throw std::runtime_error(reason);
    }
    void verify(const std::string& what, const Json& state) {
        const auto& doc = state.at("scene");
        const auto& entities = doc.at("entities");
        if (what == "collision-preview") {
            require(state.at("collision_preview_ready").get<bool>(),
                    "Collision preview is not ready");
        } else if (what == "physics-playing") {
            require(state.at("playing").get<bool>() && state.at("control_ready").get<bool>() &&
                        state.at("physics").value("characters", 0) == 1 &&
                        state.at("physics").contains("character_debug") &&
                        state.at("physics").at("character_debug").at("ground") == 0,
                    "Character Play ground observation is not ready");
        } else if (what == "collision-convex-parts" || what == "collision-compound") {
            const auto& source = state.at("collision_source");
            const auto& root = source.at("nodes").at(0);
            if (what == "collision-compound")
                require(root.at("kind") == "compound" && source.at("nodes").size() == 2,
                        "Compound authoring did not create its child");
            else
                require(root.at("kind") == "convex_hull" && source.at("nodes").size() == 1 &&
                            root.at("source").at("parts") == Json::array({0}) &&
                            !root.at("source").at("revision").get<std::string>().empty(),
                        "Explicit convex Mesh-part selection was not preserved");
        } else if (what == "collision-published") {
            require(state.at("collision_document_ready").get<bool>(),
                    "Collision document has not published");
        } else if (what == "dependency-fields-fit") {
            for (const auto* name : {"dependencies:type", "dependencies:reason", "dependencies:add",
                                     "dependencies:save", "dependencies:discard"}) {
                const auto& field = ui_targets.at(name);
                require(field.minimum.x >= field.clip_minimum.x &&
                            field.maximum.x <= field.clip_maximum.x,
                        "Dependency field exceeds the narrow Inspector width");
            }
        } else if (what == "dependency-draft-guard") {
            require(state.at("dependencies_dirty").get<bool>() &&
                        doc.at("asset_id").get<AssetId>() == AssetId::parse(scene_) &&
                        state.at("status").get<std::string>().find("Runtime Dependencies draft") !=
                            std::string::npos,
                    "Scene switch discarded a dependency draft or lacked a useful diagnostic");
        } else if (what == "dependencies-saved") {
            require(!state.at("dependencies_busy").get<bool>() &&
                        !state.at("dependencies_dirty").get<bool>(),
                    "Dependency save still pending");
            auto catalog = AssetCatalog::open_project(project_);
            const auto& edges = catalog.records().at(AssetId::parse(scene_)).dependency_edges;
            require(std::any_of(edges.begin(), edges.end(),
                                [&](const auto& edge) {
                                    return edge.target.str() == material_asset_ &&
                                           edge.role == "declared:Gameplay variants";
                                }),
                    "Typed declaration was not saved");
        } else if (what == "game-exported") {
            require(state.at("export_error").get<std::string>().empty(),
                    state.at("export_error").get<std::string>().c_str());
            require(!state.at("export_output").get<std::string>().empty(), "Export still pending");
            require(std::filesystem::is_regular_file(
                        std::filesystem::u8path(state.at("export_output").get<std::string>()) /
                        "forge.standalone.json"),
                    "Export manifest missing");
        } else if (what == "hierarchy-controls-fit") {
            for (const auto* name : {"button:Expand all", "button:Collapse all"}) {
                const auto& target = ui_targets.at(name);
                require(target.minimum.x >= target.clip_minimum.x &&
                            target.maximum.x <= target.clip_maximum.x,
                        "Hierarchy action is clipped at the current UI scale");
            }
        } else if (what == "material-created") {
            require(state.at("material_document").is_object(), "New material did not open");
            initial_material_ = state.at("material_document");
            material_asset_ = initial_material_.at("asset_id");
            require(doc == saved_, "Creating a material changed the scene");
        } else if (what == "material-edited" || what == "material-saved") {
            const auto& material = state.at("material_document");
            require(std::abs(material.at("overrides")
                                 .at("parameters")
                                 .at("roughnessFactor")
                                 .at("value")
                                 .at(0)
                                 .get<double>() -
                             .4) < .0001,
                    "Material roughness field/history did not commit 0.4");
            require(doc == saved_ && state.at("disk") == saved_,
                    "Material edit/history/save changed scene ownership");
            if (what == "material-saved")
                require(!state.at("material_dirty").get<bool>() &&
                            state.at("material_disk") == material,
                        "Material did not save and publish its own source");
        } else if (what == "material-undone") {
            require(state.at("material_document") == initial_material_ && doc == saved_,
                    "Material Undo did not restore its own source without changing the scene");
        } else if (what == "material-assigned") {
            const auto& materials = state.at("selected_preview")
                                        .at("components")
                                        .at("forge.mesh_renderer")
                                        .at("materials");
            require(materials.size() == 1 && materials.at(0).at("material") == material_asset_,
                    "Typed material picker did not assign the authored material");
            require(state.at("disk") == saved_, "Material assignment silently saved the scene");
        } else if (what == "assignment-undone") {
            require(doc == saved_, "Scene Undo did not restore the mesh's original assignment");
        } else if (what == "source-imported") {
            require(state.at("source_imported").get<bool>(),
                    "Source import did not publish its model");
            project_ = std::filesystem::u8path(state.at("project").get<std::string>());
            before_model_ = doc;
        } else if (what == "model-ready") {
            require(state.at("model_ready").get<bool>() && !state.at("model_asset").is_null(),
                    "Model settings/preview have not loaded the published source");
            model_asset_ = state.at("model_asset");
            model_generation_ = state.at("model_generation");
            require(doc == before_model_, "Import/reimport changed scene history/state");
        } else if (what == "model-placed") {
            if (model_root_.empty())
                model_root_ = state.at("selected");
            require(
                entities.size() == before_model_.at("entities").size() + 2 &&
                    std::any_of(entities.begin(), entities.end(),
                                [&](const auto& entity) {
                                    return entity.at("id") == model_root_ &&
                                           entity.at("components").contains("forge.model_source");
                                }),
                "Place configured model did not create its root and mesh node");
        } else if (what == "model-undone") {
            require(doc == before_model_, "Scene Undo did not remove complete model placement");
        } else if (what == "hot-reimport-rejected") {
            const auto& failures = state.at("failed_imports");
            require(std::find(failures.begin(), failures.end(), model_asset_) != failures.end(),
                    "Corrupt source was not reported as a failed import");
            require(state.at("model_ready").get<bool>() &&
                        state.at("model_asset") == model_asset_ && doc == saved_ &&
                        state.at("disk") == saved_,
                    "Corrupt source discarded the usable model or changed authored state");
            const auto& problems = state.at("problems");
            require(std::any_of(problems.begin(), problems.end(),
                                [&](const auto& problem) {
                                    return problem.at("asset") == model_asset_ &&
                                           problem.at("source") ==
                                               "Assets/Imported/Source-1/workflow.gltf";
                                }),
                    "Automatic reimport error has no navigable asset/source context");
        } else if (what == "failed-asset-selected") {
            require(state.at("selected_asset") == model_asset_ && doc == saved_,
                    "Selecting the reimport diagnostic did not inspect its asset safely");
        } else if (what == "hot-reimported") {
            require(state.at("model_ready").get<bool>() &&
                        state.at("model_asset") == model_asset_ &&
                        state.at("model_generation").get<std::uint64_t>() > model_generation_,
                    "Changed source did not update the open model while retaining AssetId");
            require(doc == saved_ && state.at("disk") == saved_,
                    "Model source publication mutated authored scene or saved scene");
        } else if (what == "before-cache")
            cache_scene_ = doc;
        else if (what == "cache-complete") {
            require(ui_targets.contains("cache:complete"), "Cache maintenance not complete");
            require(doc == cache_scene_, "Cache maintenance changed the authored scene");
        } else if (what == "empty")
            require(entities.empty(), "Workflow must start in an empty scene");
        else if (what == "picked-camera" || what == "picked-light") {
            require(state.at("selected") == (what == "picked-camera" ? camera_ : light_),
                    "Scene helper click did not select its entity");
        } else if (what == "cube") {
            require(entities.size() == 1 && !state.at("selected").get<std::string>().empty(),
                    "Menu did not create and select a cube");
            cube_ = state.at("selected");
        } else if (what == "rename")
            require(entities.at(0).at("name") == "Workflow cube", "Name field did not commit");
        else if (what == "position")
            require(std::abs(state.at("selected_preview")
                                 .at("components")
                                 .at("forge.position")
                                 .at("x")
                                 .get<double>() -
                             1.5) < .0001,
                    "Position field did not commit 1.5");
        else if (what == "scale" || what == "undo-scale") {
            const auto& c = state.at("selected_preview").at("components");
            const double y =
                c.contains("forge.scale") ? c.at("forge.scale").at("y").get<double>() : 1;
            require(std::abs(y - (what == "scale" ? 1.75 : 1)) < .0001,
                    "Scale field/history did not produce the expected value");
        } else if (what == "saved") {
            require(!state.at("dirty").get<bool>(), "Ctrl+S did not save the scene");
            saved_ = doc;
            scene_ = doc.at("asset_id");
            require(state.at("disk") == doc, "Saved disk data differs from authored scene");
        } else if (what == "unsaved" || what == "cancelled-reload") {
            require(state.at("dirty").get<bool>() && doc != saved_ && state.at("disk") == saved_,
                    "Unsaved rename must differ from unchanged disk data");
            require(doc.at("entities").at(0).at("name") == "Unsaved rename",
                    "Cancelled reload must preserve the unsaved name");
            if (what == "cancelled-reload")
                require(!ui_targets.contains("unsaved:discard"), "Cancel did not close the guard");
        } else if (what == "unsaved-guard") {
            require(ui_targets.contains("unsaved:discard") && ui_targets.contains("unsaved:cancel"),
                    "Reload of a dirty scene must ask before discarding it");
        } else if (what == "reload") {
            require(doc == saved_ && !state.at("dirty").get<bool>(),
                    "Reload from disk did not preserve the saved scene");
        } else if (what == "deleted")
            require(entities.empty(), "Delete menu did not delete selected cube");
        else if (what == "restored")
            require(doc == saved_, "Undo did not restore complete cube state");
        else if (what == "camera" || what == "light") {
            if (what == "camera")
                camera_ = state.at("selected");
            else
                light_ = state.at("selected");
            const auto& e = state.at("selected_preview");
            require(e.at("components").contains(what == "camera" ? "forge.camera" : "forge.light"),
                    "Rendering menu did not create/select the requested component");
            require(entities.size() == (what == "camera" ? 2u : 3u),
                    "Rendering menu created an unexpected entity count");
        } else if (what == "playing")
            require(state.at("playing").get<bool>() && state.at("control_ready").get<bool>() &&
                        !state.at("paused").get<bool>() && state.at("cameras").get<unsigned>() == 1,
                    "Play has not produced the authored game camera");
        else if (what == "paused") {
            require(state.at("paused").get<bool>() && state.at("control_ready").get<bool>(),
                    "Pause did not suspend Play");
            paused_tick_ = state.at("tick");
        } else if (what == "stepped") {
            require(state.at("paused").get<bool>() && state.at("control_ready").get<bool>() &&
                        state.at("tick").get<std::uint64_t>() == paused_tick_ + 1,
                    "Step must advance exactly one tick and remain paused");
        } else if (what == "zoom") {
            require(std::abs(state.at("ui_scale").get<double>() - 1.5) < .01,
                    "Ctrl+Plus did not produce 150% UI scale");
        } else if (what == "no-domain-errors") {
            for (const auto& problem : state.at("problems")) {
                const auto severity = problem.at("severity").get<std::string>();
                if (severity == "error" || severity == "fatal" || severity == "Error" ||
                    severity == "Fatal")
                    throw std::runtime_error("Unexpected editor diagnostic: " +
                                             problem.at("text").get<std::string>());
            }
            const auto* panel = ImGui::FindWindowByName("###Problems");
            require(panel && panel->Active && !panel->Hidden && panel->DockTabIsVisible,
                    "Click did not reveal the Problems panel");
        } else if (what == "stopped")
            require(!state.at("playing").get<bool>(), "Stop did not return to authoring");
    }

  public:
    explicit EditorInputWorkflow(bool enabled, const std::filesystem::path& evidence,
                                 const std::filesystem::path& physics_project = {}) {
        observe_ui = enabled;
        if (!enabled)
            return;
        if (!physics_project.empty()) {
            std::ifstream input(physics_project / "main.scene.json");
            const auto level = Json::parse(input);
            key(ImGuiKey_0, true);
            click("scene:view-menu");
            click("scene:fit");
            capture("physics-level-overview");
            click("scene:view-menu");
            click("physics:overlay");
            key(ImGuiKey_Escape);
            for (const auto* name :
                 {"Stair 3", "Too high step", "Gentle slope", "Steep slope", "Moving platform",
                  "Imported static collision", "Convex obstacle", "Asset compound", "Character"}) {
                const auto entity =
                    std::find_if(level.at("entities").begin(), level.at("entities").end(),
                                 [&](const Json& row) { return row.at("name") == name; });
                require(entity != level.at("entities").end(), "Physics fixture entity missing");
                text("hierarchy:search", name);
                click("entity:" + entity->at("id").get<std::string>());
                click("scene:view-menu");
                click("scene:frame");
                check("collision-preview");
                capture(std::string("physics-") + name);
            }
            click("icon:play");
            click("tab:Scene");
            check("physics-playing");
            capture("physics-character-grounded-play");
            click("icon:stop");
            check("stopped");
            return;
        }
        external_source_ = evidence / "external-source" / "workflow.gltf";
        std::filesystem::create_directories(external_source_.parent_path());
        write(external_source_, model_source().dump());
        std::string vertices;
        for (float value : std::array<float, 9>{-1, -1, 0, 1, -1, 0, 0, 1, 0}) {
            const auto bits = std::bit_cast<std::uint32_t>(value);
            for (unsigned byte = 0; byte < 4; ++byte)
                vertices.push_back(char((bits >> (byte * 8)) & 255));
        }
        write(external_source_.parent_path() / "workflow.bin", vertices);
        drop_path_ = external_source_.string();
        key(ImGuiKey_0, true);
        check("empty");
        capture("empty-scene");
        create("3D Primitive", "Cube");
        check("cube");
        capture("created-cube");
        text("inspector:name", "Workflow cube");
        check("rename");
        text("transform:forge.position:0", "1.5", true);
        check("position");
        capture("edited-position");
        text("transform:forge.scale:1", "1.75", true);
        check("scale");
        capture("edited-scale");
        key(ImGuiKey_Z, true);
        check("undo-scale");
        capture("undo-scale");
        key(ImGuiKey_Y, true);
        check("scale");
        key(ImGuiKey_S, true);
        check("saved");
        capture("saved-scene");
        click("menu:Entity");
        click("action:Entity / Delete subtree");
        check("deleted");
        key(ImGuiKey_Z, true);
        check("restored");
        click("saved-cube-row");
        text("inspector:name", "Unsaved rename");
        check("unsaved");
        click("menu:File");
        capture("file-reload-menu");
        click("file:Reload from disk");
        check("unsaved-guard");
        capture("unsaved-reload-guard");
        click("unsaved:cancel");
        check("cancelled-reload");
        click("menu:File");
        click("file:Reload from disk");
        check("unsaved-guard");
        click("unsaved:discard");
        check("reload");
        capture("reloaded-scene");
        create("Rendering", "Camera");
        check("camera");
        text("transform:forge.position:2", "-5", true);
        // At z=-5 the default Scene viewpoint is only one metre away. Keep
        // the camera at its eye height so its marker is inside the image.
        text("transform:forge.position:1", "1", true);
        capture("camera-scene-and-inspector");
        create("Rendering", "Light");
        check("light");
        text("transform:forge.position:1", "3", true);
        capture("light-scene-and-inspector");
        click("camera-marker");
        check("picked-camera");
        capture("camera-picked-in-scene");
        click("light-marker");
        check("picked-light");
        capture("light-picked-in-scene");
        click("preview-light");
        capture("authored-scene-lighting");
        click("preview-light");
        capture("preview-lighting-restored");
        click("menu:Assets");
        capture("asset-action-menu");
        click("action:asset.cache");
        check("before-cache");
        click("cache:statistics");
        check("cache-complete");
        capture("cache-statistics");
        click("cache:verify");
        check("cache-complete");
        capture("cache-verified");
        click("cache:close");

        for (int i = 0; i < 5; ++i)
            key(ImGuiKey_Equal, true);
        check("zoom");
        capture("light-ui-150");
        key(ImGuiKey_0, true);
        click("icon:play");
        check("playing");
        capture("game-camera");
        click("tab:Problems");
        check("no-domain-errors");
        capture("runtime-problems");
        key(ImGuiKey_F6);
        check("paused");
        capture("game-paused");
        key(ImGuiKey_F7);
        check("stepped");
        capture("game-stepped");
        click("icon:stop");
        check("stopped");
        capture("returned-to-edit");
        // File-drop uses SDL's production route. The fixture supplies raw DCC
        // source files only; every import/publication/placement uses real controls.
        click("tab:Content");
        hover("content-results");
        steps_.push_back({Kind::DropFile, "external-glTF"});
        capture("source-import-review");
        click("button:Prepare import");
        click("button:Copy sources");
        check("source-imported");
        capture("source-import-complete");
        click("button:Close");
        text("content:search", "workflow.gltf model");
        click("source:Assets/Imported/Source-1/workflow.gltf");
        click("menu:Assets");
        click("action:asset.open");
        check("model-ready");
        capture("imported-model-preview");
        click("button:Import / Reimport");
        check("model-ready");
        click("button:Place model");
        check("model-placed");
        click("tab:Scene");
        capture("placed-imported-model");
        key(ImGuiKey_Z, true);
        check("model-undone");
        key(ImGuiKey_Y, true);
        check("model-placed");
        click("placed-model-row");
        key(ImGuiKey_S, true);
        check("saved");
        // Simulate an external DCC save, without invoking the importer directly.
        steps_.push_back({Kind::SourceEdit, "corrupt-external-model"});
        check("hot-reimport-rejected");
        click("tab:Problems");
        click("failed-model-problem");
        check("failed-asset-selected");
        capture("rejected-model-source-keeps-last-good");
        click("tab:Scene");
        steps_.push_back({Kind::SourceEdit, "change-external-model-material"});
        check("hot-reimported");
        capture("hot-reimported-model");
        for (int i = 0; i < 5; ++i)
            key(ImGuiKey_Equal, true);
        capture("imported-content-150");
        for (int i = 0; i < 5; ++i)
            key(ImGuiKey_Equal, true);
        check("hierarchy-controls-fit");
        capture("imported-content-200");
        key(ImGuiKey_0, true);
        // Continue through an independently owned material document and the
        // typed Scene assignment picker. No direct authoring API calls.
        click("tab:Content");
        click("button:Actions");
        click("button:Create / Register");
        click("button:New material...");
        text("material:new-path", "Assets/Workflow.material.json");
        click("button:Create");
        check("material-created");
        click("material:parameter:roughnessFactor");
        text("material:value:roughnessFactor", "0.4");
        click("material:document");
        check("material-edited");
        key(ImGuiKey_Z, true);
        check("material-undone");
        key(ImGuiKey_Y, true);
        check("material-edited");
        key(ImGuiKey_S, true);
        check("material-saved");
        capture("authored-material-100");
        hover("material:value:roughnessFactor");
        capture("material-roughness-100");
        for (int i = 0; i < 5; ++i)
            key(ImGuiKey_Equal, true);
        capture("authored-material-150");
        // The stacked layout has its own window-local disclosure state.
        click("material:parameter:roughnessFactor");
        hover("material:value:roughnessFactor");
        capture("material-roughness-150");
        for (int i = 0; i < 5; ++i)
            key(ImGuiKey_Equal, true);
        capture("authored-material-200");
        hover("material:value:roughnessFactor");
        capture("material-roughness-200");
        key(ImGuiKey_0, true);
        click("tab:Scene");
        click("saved-cube-row");
        click("asset-picker:material:##material");
        click("authored-material-option");
        check("material-assigned");
        click("tab:Scene");
        key(ImGuiKey_Z, true);
        check("assignment-undone");
        key(ImGuiKey_Y, true);
        check("material-assigned");
        key(ImGuiKey_S, true);
        check("saved");
        capture("scene-material-assignment");
        click("tab:Content");
        text("content:search", "scene");
        click("saved-scene-asset");
        click("dependencies:section");
        click("dependencies:type");
        click("dependencies:type:material");
        click("asset-picker:material:##dependency-resource");
        click("authored-material-option");
        text("dependencies:reason", "Gameplay variants");
        click("button:Add dependency");
        check("dependency-fields-fit");
        hover("dependencies:reason");
        capture("runtime-dependency-draft");
        for (int i = 0; i < 10; ++i)
            key(ImGuiKey_Equal, true);
        check("dependency-fields-fit");
        hover("dependencies:type");
        capture("runtime-dependency-type-200");
        hover("dependencies:reason");
        capture("runtime-dependency-draft-200");
        hover("dependencies:save");
        capture("runtime-dependency-actions-200");
        key(ImGuiKey_0, true);
        key(ImGuiKey_N, true);
        check("dependency-draft-guard");
        click("dependencies:save");
        check("dependencies-saved");
        capture("runtime-dependencies-saved");
        click("menu:Run");
        capture("export-run-menu");
        click("action:game.export");
        capture("export-task");
        click("button:Project Settings");
        click("button:Use saved current scene as startup");
        click("button:Set up game defaults");
        capture("standalone-game-settings");
        click("button:Save Settings");
        click("button:Close Settings");
        text("export:Destination", path_utf8(evidence / "exported-game"));
        click("export:start");
        check("game-exported");
        hover("export:reveal");
        capture("export-complete");
        click("button:Close Export");
        create("3D Primitive", "Cube");
        key(ImGuiKey_F);
        click("button:+ Add Component");
        text("component-search", "Physics Body");
        click("component-choice:forge.physics_body");
        key(ImGuiKey_Escape);
        click("button:+ Add Component");
        text("component-search", "Box Collider");
        click("component-choice:forge.box_collider");
        key(ImGuiKey_Escape);
        click("scene:view-menu");
        click("physics:overlay");
        key(ImGuiKey_Escape);
        check("collision-preview");
        capture("collision-box-scene");
        create("3D Primitive", "Cube");
        key(ImGuiKey_F);
        click("button:+ Add Component");
        text("component-search", "Character Controller");
        click("component-choice:forge.character_controller");
        key(ImGuiKey_Escape);
        check("collision-preview");
        capture("character-capsule-scene");
        click("tab:Content");
        click("button:Actions");
        click("button:Create / Register");
        click("button:New collision...");
        text("collision:path", "Assets/workflow.collision.json");
        click("button:Create");
        capture("collision-document");
        click("collision:save");
        check("collision-published");
        capture("collision-published");
        click("collision:shape");
        click("collision:kind:convex_hull");
        click("button:Choose mesh geometry...");
        click("collision:all-parts");
        click("collision:part:0");
        capture("collision-mesh-part-selection");
        click("button:Use geometry");
        check("collision-convex-parts");
        click("collision:save");
        check("collision-published");
        capture("collision-convex-published");
        click("collision:shape");
        click("collision:kind:compound");
        check("collision-compound");
        capture("collision-compound-document");
        key(ImGuiKey_Z, true);
        check("collision-convex-parts");
        capture("collision-compound-undo");
    }
    bool done() const { return index_ == steps_.size(); }
    void platform_input(SDL_WindowID window) {
        if (done())
            return;
        const auto& step = steps_[index_];
        if (step.kind == Kind::DropFile && frame_ == 0) {
            const auto it = ui_targets.find("content-results");
            if (it == ui_targets.end())
                return;
            const auto origin = ImGui::GetMainViewport()->Pos;
            const auto& target = it->second;
            for (auto type : {SDL_EVENT_DROP_BEGIN, SDL_EVENT_DROP_POSITION, SDL_EVENT_DROP_FILE,
                              SDL_EVENT_DROP_COMPLETE}) {
                SDL_Event event{};
                event.type = type;
                event.drop.windowID = window;
                event.drop.x = (target.minimum.x + target.maximum.x) * .5f - origin.x;
                event.drop.y = (target.minimum.y + target.maximum.y) * .5f - origin.y;
                event.drop.data = type == SDL_EVENT_DROP_FILE ? drop_path_.c_str() : nullptr;
                if (!SDL_PushEvent(&event))
                    throw std::runtime_error(SDL_GetError());
            }
        }
        if (step.kind == Kind::SourceEdit && frame_ == 0)
            write(project_ / "Assets/Imported/Source-1/workflow.gltf",
                  step.value == "corrupt-external-model" ? "{" : model_source(true).dump());
        // Interface zoom is handled by the production SDL event loop, before ImGui.
        if (step.kind != Kind::Key || !step.control ||
            (step.key != ImGuiKey_0 && step.key != ImGuiKey_Equal) || frame_ > 1)
            return;
        SDL_Event event{};
        event.type = frame_ == 0 ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.windowID = window;
        event.key.key = step.key == ImGuiKey_0 ? SDLK_0 : SDLK_EQUALS;
        event.key.scancode = step.key == ImGuiKey_0 ? SDL_SCANCODE_0 : SDL_SCANCODE_EQUALS;
        event.key.mod = SDL_KMOD_CTRL;
        event.key.down = frame_ == 0;
        if (!SDL_PushEvent(&event))
            throw std::runtime_error(SDL_GetError());
    }
    void input() {
        auto& io = ImGui::GetIO();
        io.ConfigInputTrickleEventQueue = false;
        io.AddFocusEvent(true);
        if (done())
            return;
        if (!since_)
            since_ = SDL_GetTicks();
        const auto& step = steps_[index_];
        if (SDL_GetTicks() - since_ > 12000)
            failure_ = "Timed out at step " + std::to_string(index_) + ": " + step.value + " " +
                       last_check_;
        if ((step.kind == Kind::Click || step.kind == Kind::Hover) && frame_ == 0) {
            const auto target =
                step.value == "saved-cube-row"             ? "entity:" + cube_
                : step.value == "authored-material-option" ? "picker-option:" + material_asset_
                : step.value == "failed-model-problem"     ? "problem:reimport:" + model_asset_
                : step.value == "placed-model-row"         ? "entity:" + model_root_
                : step.value == "camera-marker"            ? "marker:" + camera_
                : step.value == "light-marker"             ? "marker:" + light_
                : step.value == "saved-scene-asset"        ? "asset:" + scene_
                                                           : step.value;
            const auto it = ui_targets.find(target);
            if (it == ui_targets.end() || !it->second.enabled) {
                io.AddMousePosEvent(pointer_.x, pointer_.y);
                ui_targets.clear();
                return;
            }
            const auto& t = it->second;
            pointer_ = {(t.minimum.x + t.maximum.x) * .5f, (t.minimum.y + t.maximum.y) * .5f};
            if (t.minimum.y < t.clip_minimum.y || t.maximum.y > t.clip_maximum.y) {
                const float direction = t.minimum.y < t.clip_minimum.y ? 3.f : -3.f;
                // Use the scrollable window's edge, outside preview images that
                // correctly consume the wheel for their own camera/image zoom.
                pointer_.x = t.clip_maximum.x - 1.f;
                pointer_.y = (t.clip_minimum.y + t.clip_maximum.y) * .5f;
                io.AddMousePosEvent(pointer_.x, pointer_.y);
                io.AddMouseWheelEvent(0, direction);
                ui_targets.clear();
                return;
            }
            trace_.push_back({{"step", index_},
                              {"target", target},
                              {"rect", {t.minimum.x, t.minimum.y, t.maximum.x, t.maximum.y}},
                              {"route", "ImGui queued mouse/key input"}});
        }
        io.AddMousePosEvent(pointer_.x, pointer_.y);
        if (step.kind == Kind::Click) {
            if (frame_ == 2) {
                io.AddKeyEvent(ImGuiMod_Ctrl, step.control);
                io.AddMouseButtonEvent(0, true);
            }
            if (frame_ == 3)
                io.AddMouseButtonEvent(0, false);
            if (frame_ == 4)
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
        } else if (step.kind == Kind::Text) {
            if (frame_ == 0) {
                io.AddKeyEvent(ImGuiMod_Ctrl, true);
                io.AddKeyEvent(ImGuiKey_A, true);
            }
            if (frame_ == 1) {
                io.AddKeyEvent(ImGuiKey_A, false);
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
            }
            if (frame_ == 2)
                io.AddInputCharactersUTF8(step.value.c_str());
            if (frame_ == 3)
                io.AddKeyEvent(ImGuiKey_Enter, true);
            if (frame_ == 4)
                io.AddKeyEvent(ImGuiKey_Enter, false);
        } else if (step.kind == Kind::Key && frame_ < 2) {
            io.AddKeyEvent(ImGuiMod_Ctrl, frame_ == 0 && step.control);
            io.AddKeyEvent(step.key, frame_ == 0);
        }
        ++frame_;
        ui_targets.clear();
    }
    void finish(const Json& state, const std::function<void(const std::string&)>& image,
                const std::function<void(const Json&)>& record) {
        if (done())
            return;
        if (!failure_.empty()) {
            image("FAILED-" + std::to_string(index_));
            Json available = Json::object();
            for (const auto& [name, target] : ui_targets)
                available[name] = {
                    {"enabled", target.enabled},
                    {"rect",
                     {target.minimum.x, target.minimum.y, target.maximum.x, target.maximum.y}}};
            record({{"ok", false},
                    {"error", failure_},
                    {"trace", trace_},
                    {"state", state},
                    {"available_controls", available}});
            throw std::runtime_error(failure_);
        }
        if (frame_ < 8 || SDL_GetTicks() - since_ < 100)
            return;
        const auto& step = steps_[index_];
        if (step.kind == Kind::Check) {
            try {
                verify(step.value, state);
            } catch (const std::exception& e) {
                last_check_ = e.what();
                return;
            }
        }
        if (step.kind == Kind::Capture) {
            const auto name = std::to_string(index_) + "-" + step.value;
            image(name);
            trace_.push_back({{"step", index_},
                              {"image", "editor-" + name + ".ppm"},
                              {"ui_scale", state.at("ui_scale")}});
        }
        constexpr const char* names[] = {
            "click",  "hover",   "type",          "shortcut",
            "assert", "capture", "SDL file drop", "external source edit"};
        trace_.push_back({{"step", index_},
                          {"operation", names[int(step.kind)]},
                          {"value", step.value},
                          {"key", step.key == ImGuiKey_None ? "" : ImGui::GetKeyName(step.key)},
                          {"duration_ms", SDL_GetTicks() - since_},
                          {"control", step.control},
                          {"ok", true}});
        ++index_;
        frame_ = 0;
        since_ = 0;
        last_check_.clear();
        record({{"ok", done()},
                {"completed_steps", index_},
                {"total_steps", steps_.size()},
                {"trace", trace_},
                {"state", state}});
    }
};
} // namespace forge::test
