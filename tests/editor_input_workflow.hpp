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
    enum class Kind {
        Click,
        Drag,
        Hover,
        Text,
        Key,
        Check,
        Capture,
        DropFile,
        SourceEdit,
        SourceText
    };
    struct Step {
        Kind kind;
        std::string value;
        ImGuiKey key = ImGuiKey_None;
        bool control = false;
    };
    std::vector<Step> steps_;
    std::size_t index_ = 0;
    unsigned frame_ = 0, play_click_retries_ = 0;
    Uint64 since_ = 0, checkpoint_ = 0;
    ImVec2 pointer_{-FLT_MAX, -FLT_MAX};
    std::string failure_, last_check_, cube_, camera_, light_, scene_;
    std::uint64_t paused_tick_ = 0;
    Json multi_before_, cache_scene_, saved_, before_model_, initial_material_,
        trace_ = Json::array();
    std::filesystem::path external_source_, project_;
    std::string drop_path_, model_asset_, model_root_, model_mesh_, material_asset_,
        collision_asset_;
    std::uint64_t model_generation_ = 0;
    std::string graph_asset_, graph_parameter_, graph_output_, graph_binding_, graph_added_,
        graph_function_, graph_call_;
    Json graph_before_;
    Json starter_modules_, apply_before_, apply_after_;
    std::string starter_source_, generated_input_;
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
    void drag(std::string from, std::string to) {
        steps_.push_back({Kind::Drag, std::move(from) + "|" + std::move(to)});
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
        if (what == "graph-created") {
            const auto& g = state.at("graph_document");
            require(g.is_object(), "Graph creation did not open a source document");
            graph_before_ = g;
            graph_asset_ = g.at("asset_id");
            graph_parameter_ = g.at("graph").at("nodes")[0].at("id");
            graph_output_ = g.at("graph").at("nodes")[1].at("id");
            graph_binding_ = g.at("graph").at("nodes")[0].at("data").at("key");
        } else if (what == "graph-added") {
            const auto& nodes = state.at("graph_document").at("graph").at("nodes");
            require(nodes.size() == 3 && nodes[2].at("type") == "constant",
                    "Node search/add did not create a constant");
            graph_added_ = nodes[2].at("id");
        } else if (what == "graph-function-extracted") {
            const auto& graph = state.at("graph_document").at("graph");
            require(graph.at("functions").size() == 1,
                    "Extraction did not create one typed function");
            graph_function_ = graph.at("functions")[0].at("id");
            for (const auto& node : graph.at("nodes"))
                if (node.at("type") == "function")
                    graph_call_ = node.at("id");
            require(!graph_call_.empty(),
                    "Extraction did not replace the selected node with a function call");
        } else if (what == "graph-function-edited") {
            const auto& body =
                state.at("graph_document").at("graph").at("functions")[0].at("graph");
            bool found = false;
            for (const auto& node : body.at("nodes"))
                if (node.at("id") == graph_added_) {
                    found = node.at("data").at("value")[0].get<double>() > .39 &&
                            node.at("data").at("value")[0].get<double>() < .41;
                }
            require(found, "Function source editing failed to retain its member value");
        } else if (what == "graph-add-removed") {
            require(state.at("graph_document").at("graph").at("nodes").size() == 2,
                    "Delete did not remove the added node");
        } else if (what == "graph-connected") {
            const auto& edge = state.at("graph_document").at("graph").at("edges").at(0);
            require(edge.at("id") != graph_before_.at("graph").at("edges").at(0).at("id") &&
                        edge.at("from").at("node") == graph_parameter_ &&
                        edge.at("to").at("node") == graph_output_,
                    "Mouse port drag did not connect typed nodes");
        } else if (what == "graph-ready") {
            require(!state.at("graph_dirty").get<bool>() &&
                        state.at("graph_preview_current").get<bool>() &&
                        state.at("graph_gpu_ready").get<bool>() && state.at("graph_error") == "",
                    "Graph publication/preview is not ready");
        } else if (what == "graph-renamed" || what == "graph-undo") {
            const auto& g = state.at("graph_document");
            require(g.at("asset_id") == graph_asset_ &&
                        g.at("graph").at("nodes")[0].at("data").at("key") == graph_binding_,
                    "Graph label edit changed binding/asset identity");
            require(g.at("graph").at("nodes")[0].at("data").at("label") ==
                        (what == "graph-undo" ? "Color" : "Surface Tint"),
                    "Graph label history failed");
        } else if (what == "graph-rejected") {
            require(!state.at("graph_error").get<std::string>().empty() &&
                        state.at("graph_compiled").get<bool>() &&
                        state.at("graph_gpu_ready").get<bool>(),
                    "Failed graph compilation removed the usable preview");
            require(state.at("graph_document").at("graph").at("nodes").size() == 1,
                    "Graph Delete did not affect the canvas");
            require(doc == saved_, "Graph Delete changed scene entities");
        } else if (what == "graph-closed") {
            require(state.at("graph_document").is_null(), "Graph did not close");
        } else if (what == "graph-material-created") {
            require(state.at("material_document").is_object(), "Graph material did not open");
            material_asset_ = state.at("material_document").at("asset_id");
        } else if (what == "graph-material-override" || what == "graph-material-reverted") {
            const auto& overrides = state.at("material_document").at("overrides");
            const bool owns = overrides.contains("parameters") &&
                              overrides.at("parameters").contains(graph_binding_);
            require(owns == (what == "graph-material-override"),
                    "Graph material parameter override intent is wrong");
            if (owns)
                require(
                    std::abs(
                        overrides.at("parameters").at(graph_binding_).at("value")[0].get<double>() -
                        .2) < 1e-5,
                    "Graph material parameter widget did not edit its first channel");
        } else if (what == "graph-material-saved") {
            require(!state.at("material_dirty").get<bool>() &&
                        state.at("material_document").at("overrides").at("shader") == graph_asset_,
                    "Graph material did not publish its Shader reference");
        } else if (what == "graph-reopened") {
            require(state.at("graph_document").at("asset_id") == graph_asset_ &&
                        state.at("graph_document").at("graph").at("nodes").size() == 2,
                    "Graph did not reopen intact");
        } else if (what == "cpp-source-empty") {
            require(state.at("cpp_source_text").get<std::string>().size() < 2,
                    "Delete did not clear C++ source");
            require(state.at("cpp_source_dirty").get<bool>() && entities.size() == 3,
                    "Source Delete changed scene or failed to record source history");
        } else if (what == "cpp-diagnostic-source") {
            require(state.at("cpp_source") == "Native/gameplay.cpp",
                    "Compiler diagnostic opened wrong source");
        } else if (what == "compiler-ready") {
            require(!state.at("sdk_build_busy").get<bool>() &&
                        state.at("compiler_ready").get<bool>(),
                    "Compiler test did not complete successfully");
            require(state.at("project_settings").value("modules", Json::array()).empty(),
                    "Compiler test published gameplay into the project");
        } else if (what == "cpp-source-dirty" || what == "cpp-source-saved") {
            require(state.at("cpp_source") == "Native/extra.cpp", "Wrong source tab active");
            require(state.at("cpp_source_text").get<std::string>().find("forge_extra_source") !=
                        std::string::npos,
                    "Actual C++ source input missing");
            require(state.at("cpp_source_dirty").get<bool>() == (what == "cpp-source-dirty"),
                    "C++ source save/dirty state incorrect");
        } else if (what == "cpp-component-created") {
            const auto source = project_ / "Native/Components/Rotator.hpp";
            require(std::filesystem::is_regular_file(source),
                    "C++ component wizard did not create source");
            require(state.at("cpp_source") == "Native/Components/Rotator.hpp",
                    "C++ component source was not opened");
        } else if (what == "cpp-system-created") {
            require(
                std::filesystem::is_regular_file(project_ / "Native/Systems/RotationSystem.cpp"),
                "C++ system wizard did not create source");
            require(state.at("cpp_source") == "Native/Systems/RotationSystem.cpp",
                    "C++ system source was not opened");
        } else if (what == "cpp-component-opened" || what == "cpp-system-opened") {
            require(state.at("cpp_source") == (what == "cpp-component-opened"
                                                   ? "Native/Components/Rotator.hpp"
                                                   : "Native/Systems/RotationSystem.cpp"),
                    "C++ source browser opened the wrong file");
        } else if (what == "cpp-component-dirty" || what == "cpp-system-dirty" ||
                   what == "cpp-component-saved") {
            const bool component = what != "cpp-system-dirty";
            require(state.at("cpp_source") == (component ? "Native/Components/Rotator.hpp"
                                                         : "Native/Systems/RotationSystem.cpp") &&
                        state.at("cpp_source_dirty").get<bool>() == (what != "cpp-component-saved"),
                    "C++ source editor did not preserve expected file/dirty state");
            require(state.at("cpp_source_text")
                            .get<std::string>()
                            .find(component
                                      ? "// Edited and saved through the FORGE C++ source editor."
                                      : "-0.008726646259971648") != std::string::npos,
                    "Edited C++ source text is missing from the built-in editor");
        } else if (what == "starter-created") {
            require(state.at("sdk_build_managed"), "Gameplay source was not created");
            project_ = std::filesystem::u8path(state.at("project").get<std::string>());
        } else if (what == "starter-built" || what == "starter-rebuilt") {
            require(!state.at("sdk_build_busy").get<bool>(), "SDK build still running");
            require(state.at("sdk_build_error").get<std::string>().empty(),
                    state.at("sdk_build_error").get<std::string>().c_str());
            require(state.at("gameplay_current").get<bool>(),
                    "Published gameplay does not match current saved source");
            const auto modules = state.at("project_settings").at("modules");
            require(!modules.empty() && modules.back().at("id") == "project.gameplay",
                    "SDK module not registered");
            if (what == "starter-rebuilt")
                require(modules != starter_modules_, "Rebuild did not publish a fresh deployment");
            starter_modules_ = modules;
            project_ = std::filesystem::u8path(state.at("project").get<std::string>());
        } else if (what == "build-required") {
            require(!state.at("playing").get<bool>() && !state.at("gameplay_current").get<bool>() &&
                        !state.at("sdk_build_busy").get<bool>() &&
                        state.at("sdk_build_error").get<std::string>().empty(),
                    "Changed saved gameplay source did not require a build");
        } else if (what == "stale-play-offered") {
            const auto* popup = ImGui::FindWindowByName("Build C++ gameplay before Play?");
            require(!state.at("playing").get<bool>() && popup && popup->Active && !popup->Hidden,
                    "Play did not offer Save, Build & Play for changed source");
        } else if (what == "starter-rejected") {
            require(!state.at("sdk_build_busy").get<bool>() &&
                        !state.at("sdk_build_error").get<std::string>().empty(),
                    "Invalid C++ should fail compilation");
            require(state.at("project_settings").at("modules") == starter_modules_,
                    "Failed compilation replaced the good module");
        } else if (what == "starter-admitted") {
            bool admitted = false;
            for (const auto& c : state.at("component_schema").at("components"))
                admitted |= c.at("id") == "project.counter";
            bool rotator = false;
            for (const auto& c : state.at("component_schema").at("components"))
                rotator |= c.at("id") == "project.rotator";
            require(admitted && rotator, "C++ gameplay components were not admitted");
        } else if (what == "live-rotator-tuned") {
            bool tuned = false, authored_unchanged = false;
            for (const auto& row : state.at("runtime_snapshot").value("entities", Json::array()))
                if (row.at("components").contains("project.rotator"))
                    tuned |=
                        row.at("components").at("project.rotator").value("speed", 0.0) == 360.0;
            for (const auto& row : entities)
                if (row.at("components").contains("project.rotator"))
                    authored_unchanged |=
                        row.at("components").at("project.rotator").value("speed", 0.0) == 90.0;
            require(tuned && authored_unchanged,
                    "Runtime tuning did not apply or changed authored component data");
        } else if (what == "authored-rotator") {
            bool found = false;
            for (const auto& row : entities)
                if (row.at("components").contains("project.rotator"))
                    found |= row.at("components").at("project.rotator").value("speed", 0.0) == 90.0;
            require(found, "Stop did not preserve authored Rotator Speed = 90");
        } else if (what == "system-reversed") {
            require(state.at("playing").get<bool>() && state.at("runtime_snapshot").is_object(),
                    "Changed C++ System has not started Play");
            bool reversed = false;
            for (const auto& row : state.at("runtime_snapshot").value("entities", Json::array()))
                if (row.at("components").contains("project.rotator") &&
                    row.at("components").contains("forge.local_rotation"))
                    reversed |=
                        row.at("components").at("forge.local_rotation").value("y", 0.0) < -0.0001;
            require(reversed, "Rebuilt RotationSystem did not rotate in the opposite direction");
        } else if (what == "starter-ticked") {
            require(state.at("playing").get<bool>() && state.at("runtime_snapshot").is_object(),
                    "Waiting for started gameplay runtime and its first snapshot");
            bool increased = false;
            for (const auto& row : state.at("runtime_snapshot").value("entities", Json::array()))
                if (row.at("components").contains("project.counter"))
                    increased |= row.at("components").at("project.counter").value("value", 0.0) > 0;
            bool rotated = false;
            for (const auto& row : state.at("runtime_snapshot").value("entities", Json::array()))
                if (row.at("components").contains("project.rotator") &&
                    row.at("components").contains("forge.local_rotation"))
                    rotated |=
                        std::abs(row.at("components").at("forge.local_rotation").value("y", 0.0)) >
                        0.0001;
            require(increased && rotated,
                    "Generated C++ Flecs systems did not advance the authored entity");
        } else if (what == "apply-instance") {
            require(!state.at("selected").get<std::string>().empty(),
                    "Instantiate did not select entity");
            bool instance = false;
            for (const auto& row : entities)
                if (row.at("id") == state.at("selected"))
                    instance = row.contains("prefab_instance");
            require(instance, "Prefab root not selected");
        } else if (what == "apply-overridden") {
            require(state.at("selected_preview")
                            .at("components")
                            .at("forge.local_translation")
                            .at("x") == 4,
                    "Translation input did not edit instance");
            apply_before_ = doc;
        } else if (what == "apply-cancelled")
            require(doc == apply_before_, "Cancel changed instance");
        else if (what == "apply-published" || what == "apply-redone") {
            require(!state.at("dirty").get<bool>(), "Apply/Redo did not save scene");
            for (const auto& row : entities)
                if (row.at("id") == state.at("selected"))
                    require(!row.at("components").contains("forge.local_translation"),
                            "Apply retained channel override");
            require(state.at("prefab_sources")[0]
                            .at("members")[0]
                            .at("components")
                            .at("forge.local_translation")
                            .at("x") == 4,
                    "Source did not receive override");
            apply_after_ = doc;
        } else if (what == "apply-undone") {
            for (const auto& row : entities)
                if (row.at("id") == state.at("selected"))
                    require(row.at("components").contains("forge.local_translation"),
                            "Undo did not restore instance override");
            require(!state.at("dirty").get<bool>(), "Apply Undo did not save pair");
        } else if (what == "multi-selected") {
            const auto ids = state.at("selected_entities").get<std::vector<std::string>>();
            require(ids.size() == 2 && std::find(ids.begin(), ids.end(), cube_) != ids.end() &&
                        std::find(ids.begin(), ids.end(), light_) != ids.end(),
                    "Additive UI selection failed");
            require(state.at("hierarchy_keyboard_focused").get<bool>(),
                    "Hierarchy entity click did not acquire keyboard focus");
            multi_before_ = doc;
        } else if (what == "multi-duplicated") {
            require(entities.size() == multi_before_.at("entities").size() + 2 &&
                        state.at("selected_entities").size() == 2,
                    "Batch duplicate UI failed");
        } else if (what == "multi-deleted") {
            require(entities.size() + 2 == multi_before_.at("entities").size(),
                    "Batch delete UI failed");
        } else if (what == "multi-restored") {
            require(doc == multi_before_, "Batch UI operation was not one Undo step");
        } else if (what == "collision-preview") {
            if (!state.at("collision_preview_ready").get<bool>())
                throw std::runtime_error("Collision preview is not ready: " +
                                         state.value("collision_preview_status", std::string{}));
        } else if (what == "physics-playing") {
            require(state.at("playing").get<bool>() && state.at("control_ready").get<bool>() &&
                        state.at("physics").value("characters", 0) == 1 &&
                        state.at("physics").contains("character_debug") &&
                        state.at("physics").at("character_debug").at("ground") == 0,
                    "Character Play ground observation is not ready");
        } else if (what == "collision-external-refresh") {
            require(state.at("collision_source").at("nodes")[0].at("translation")[0] == .125,
                    "External collision source has not been refreshed");
            require(ui_targets.contains("collision:kind:compound"),
                    "Background collision refresh closed the active Shape popup");
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
            collision_asset_ = state.at("collision_source").at("asset_id").get<std::string>();
        } else if (what == "collision-assigned") {
            require(std::any_of(entities.begin(), entities.end(),
                                [&](const Json& row) {
                                    const auto& c = row.at("components");
                                    return c.contains("forge.asset_collider") &&
                                           c.at("forge.asset_collider").at("asset") ==
                                               collision_asset_;
                                }),
                    "Typed Collision picker did not assign the published asset");
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
        } else if (what == "graph-material-two-sided") {
            require(state.at("material_document").at("overrides").at("double_sided").get<bool>(),
                    "Flat imported test geometry needs the material's explicit Two-sided state");
        } else if (what == "graph-mesh-ready") {
            require(!state.at("scene_mesh_pending").get<bool>(),
                    "Assigned graph mesh preparation is pending");
            for (const auto& diagnostic : state.at("scene_mesh_diagnostics"))
                require(!diagnostic.at("entity").is_string() ||
                            diagnostic.at("entity").get<std::string>() != model_mesh_,
                        ("Assigned graph rejected its UV-less mesh: " +
                         diagnostic.at("text").get<std::string>())
                            .c_str());
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
            for (const auto& entity : entities)
                if (entity.value("parent", std::string{}) == model_root_ &&
                    entity.at("components").contains("forge.mesh_renderer"))
                    model_mesh_ = entity.at("id").get<std::string>();
            require(!model_mesh_.empty(), "Placed model has no authored mesh child");
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
        } else if (what == "game-relative-captured")
            require(state.at("game_input_captured").get<bool>() &&
                        state.at("game_mouse_relative").get<bool>(),
                    "SDL relative capture was not acquired");
        else if (what == "game-relative-released")
            require(!state.at("game_input_captured").get<bool>() &&
                        !state.at("game_mouse_relative").get<bool>(),
                    "Native Escape did not release relative capture");
        else if (what == "playing")
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
                                 const std::filesystem::path& physics_project = {},
                                 bool sdk_onboarding = false) {
        observe_ui = enabled;
        if (!enabled)
            return;
        if (sdk_onboarding) {
            key(ImGuiKey_0, true);
            check("empty");
            create("3D Primitive", "Cube");
            check("cube");
            create("Rendering", "Camera");
            check("camera");
            text("transform:forge.position:2", "-5", true);
            key(ImGuiKey_Enter);
            create("Rendering", "Light");
            check("light");
            key(ImGuiKey_S, true);
            check("saved");
            click("tab:Native");
            capture("gameplay-create");
            click("button:Create C++ gameplay project");
            check("starter-created");
            click("button:Create C++ Component");
            check("cpp-component-created");
            capture("cpp-component-created");
            click("button:Create C++ System");
            check("cpp-system-created");
            capture("cpp-system-created");
            click("button:Native/Components/Rotator.hpp");
            check("cpp-component-opened");
            click("cpp:editor");
            steps_.push_back({Kind::SourceText, "component-edit"});
            check("cpp-component-dirty");
            capture("cpp-component-dirty");
            key(ImGuiKey_S, true);
            check("cpp-component-saved");
            check("build-required");
            capture("gameplay-build-required");
            click("sdk:compiler-setup");
            click("button:Test compiler tools");
            check("compiler-ready");
            capture("compiler-ready");
            click("button:Open C++ source");
            capture("cpp-source-editor");
            text("cpp:new-filename", "extra.cpp");
            click("button:Create source file");
            text("cpp:editor", "int forge_extra_source() { return 9; }", true);
            check("cpp-source-dirty");
            capture("cpp-source-dirty");
            click("cpp:build-on-save");
            key(ImGuiKey_S, true);
            check("cpp-source-saved");
            capture("cpp-source-saved");
            click("cpp:editor");
            key(ImGuiKey_A, true);
            key(ImGuiKey_Delete);
            check("cpp-source-empty");
            key(ImGuiKey_Z, true);
            check("cpp-source-saved");
            key(ImGuiKey_Y, true);
            check("cpp-source-empty");
            key(ImGuiKey_Z, true);
            check("cpp-source-saved");
            click("cpp:build-on-save");
            click("tab:Native");
            check("starter-built");
            hover("sdk:build-status");
            capture("gameplay-built");
            steps_.push_back({Kind::SourceEdit, "starter-break"});
            check("build-required");
            capture("gameplay-build-required");
            click("icon:play");
            check("stale-play-offered");
            capture("gameplay-stale-play-offer");
            click("button:Cancel");
            click("button:Build gameplay");
            check("starter-rejected");
            hover("sdk:build-error");
            capture("gameplay-build-rejected");
            click("sdk:build-output");
            hover("cpp:diagnostic:0");
            click("cpp:diagnostic:0");
            check("cpp-diagnostic-source");
            capture("cpp-compiler-diagnostic");
            click("tab:Native");
            steps_.push_back({Kind::SourceEdit, "starter-restore"});
            click("button:Build gameplay");
            check("starter-rebuilt");
            click("button:Inspect components");
            check("starter-admitted");
            click("tab:Scene");
            click("saved-cube-row");
            click("button:+ Add Component");
            text("component-search", "Gameplay Counter");
            click("component-choice:project.counter");
            key(ImGuiKey_Escape);
            click("button:+ Add Component");
            text("component-search", "Rotator");
            click("component-choice:project.rotator");
            key(ImGuiKey_Escape);
            key(ImGuiKey_S, true);
            check("saved");
            hover("component-field:project.counter:value");
            capture("gameplay-counter-inspector");
            check("authored-rotator");
            hover("component-field:project.rotator:speed");
            capture("cpp-rotator-inspector");
            click("icon:play");
            check("starter-ticked");
            capture("cpp-rotator-playing");
            text("runtime-field:project.rotator:speed", "360", true);
            key(ImGuiKey_Enter);
            check("live-rotator-tuned");
            capture("cpp-rotator-live-tuned");
            click("icon:stop");
            check("stopped");
            check("authored-rotator");
            click("tab:Native");
            click("button:Open C++ source");
            click("button:Native/Systems/RotationSystem.cpp");
            check("cpp-system-opened");
            click("cpp:editor");
            steps_.push_back({Kind::SourceText, "system-reverse"});
            check("cpp-system-dirty");
            capture("cpp-system-dirty");
            key(ImGuiKey_S, true);
            check("build-required");
            click("button:Build gameplay");
            check("starter-rebuilt");
            capture("cpp-system-current");
            click("tab:Scene");
            click("icon:play");
            check("system-reversed");
            capture("cpp-system-reversed");
            click("icon:stop");
            check("stopped");
            click("tab:Content");
            click("button:Actions");
            click("button:Create / Register");
            click("content:prefabs");
            click("button:Create from selection");
            key(ImGuiKey_Escape);
            key(ImGuiKey_Escape);
            // Close both nested Content popups, then use the selected asset's
            // existing Inspector placement action.
            click("button:Place in Scene");
            key(ImGuiKey_S, true);
            check("apply-instance");
            click("tab:Scene");
            text("transform:forge.position:0", "4", true);
            key(ImGuiKey_Enter);
            check("apply-overridden");
            click("button:Apply instance overrides...");
            capture("prefab-apply-review");
            click("button:Cancel");
            check("apply-cancelled");
            click("button:Apply instance overrides...");
            click("button:Apply and save both");
            check("apply-published");
            capture("prefab-apply-published");
            key(ImGuiKey_Z, true);
            check("apply-undone");
            key(ImGuiKey_Y, true);
            check("apply-redone");
            click("menu:Run");
            click("action:game.export");
            click("button:Project Settings");
            click("button:Use saved current scene as startup");
            click("button:Save Settings");
            click("button:Close Settings");
            text("export:Destination", path_utf8(evidence / "exported-starter-game"));
            click("export:start");
            check("game-exported");
            capture("starter-export-complete");
            click("button:Close Export");
            return;
        }
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
        click("saved-cube-row", true);
        check("multi-selected");
        capture("multi-selection-inspector");
        click("scene:view-menu");
        click("scene:frame");
        capture("multi-selection-framed");
        key(ImGuiKey_D, true);
        check("multi-duplicated");
        capture("multi-selection-duplicated");
        key(ImGuiKey_Z, true);
        check("multi-restored");
        click("light-marker");
        click("saved-cube-row", true);
        check("multi-selected");
        key(ImGuiKey_Delete);
        check("multi-deleted");
        key(ImGuiKey_Z, true);
        check("multi-restored");
        capture("multi-selection-restored");
        click("light-marker");
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
        click("game-relative-mouse");
        click("button:Capture gameplay input");
        check("game-relative-captured");
        capture("game-relative-mouse-captured");
        steps_.push_back({Kind::Key, "game-native-escape", ImGuiKey_Escape});
        check("game-relative-released");
        click("game-relative-mouse");
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
        click("button:Actions");
        click("button:Create / Register");
        click("graph:new");
        text("graph:new-path", "Assets/WorkflowSurface.shader.json");
        click("graph:create");
        check("graph-created");
        click("graph:save");
        check("graph-ready");
        capture("material-graph-workspace");
        click("graph:add-node");
        text("graph:search", "CoNsTaNt");
        click("graph:node-choice:constant");
        check("graph-added");
        click("graph-added-node");
        click("graph:node-type");
        click("graph:node-type:scalar");
        click("graph:frame");
        capture("material-graph-node-properties");
        drag("graph-added-port", "graph-roughness-port");
        click("graph-added-node");
        click("graph:extract-function");
        check("graph-function-extracted");
        click("graph:functions");
        click("graph-function-edit");
        click("graph-added-node");
        text("graph:node-value", "0.4", true);
        click("graph:back-surface");
        check("graph-function-edited");
        capture("material-graph-function-edited");
        click("graph-call-node");
        key(ImGuiKey_Delete);
        check("graph-add-removed");
        drag("graph-parameter-port", "graph-surface-port");
        check("graph-connected");
        key(ImGuiKey_Z, true);

        click("graph-parameter-node");
        text("graph:node-label", "Surface Tint");
        click("graph-output-node");
        check("graph-renamed");
        key(ImGuiKey_Z, true);
        check("graph-undo");
        key(ImGuiKey_Y, true);
        check("graph-renamed");
        click("graph:save");
        check("graph-ready");
        click("graph-output-node");
        key(ImGuiKey_Delete);
        click("graph:save");
        check("graph-rejected");
        capture("material-graph-error-retention");
        key(ImGuiKey_Z, true);
        click("graph:save");
        check("graph-ready");
        click("graph:close");
        check("graph-closed");
        click("tab:Content");
        text("content:search", "WorkflowSurface");
        click("source:Assets/WorkflowSurface.shader.json");
        click("menu:Assets");
        click("action:asset.open");
        check("graph-reopened");
        capture("material-graph-reopened");
        click("tab:Content");
        click("button:Actions");
        click("button:Create / Register");
        click("button:New material...");
        text("material:new-path", "Assets/WorkflowGraph.material.json");
        click("button:Create");
        check("graph-material-created");
        click("asset-picker:shader:Surface Shader");
        click("graph-shader-option");
        click("graph-material-parameter");
        text("graph-material-value", "0.2");
        check("graph-material-override");
        click("material:document");
        key(ImGuiKey_Z, true);
        check("graph-material-reverted");
        key(ImGuiKey_Y, true);
        check("graph-material-override");
        click("graph-material-revert");
        check("graph-material-reverted");
        text("graph-material-value", "0.2");
        check("graph-material-override");
        click("material:state:double_sided");
        check("graph-material-two-sided");
        click("material:document");
        key(ImGuiKey_S, true);
        check("graph-material-saved");
        capture("graph-material-instance");
        click("tab:Scene");
        click("saved-cube-row");
        click("asset-picker:material:##material");
        click("authored-material-option");
        check("material-assigned");
        key(ImGuiKey_S, true);
        check("saved");
        capture("cube-graph-material");
        click("hierarchy:expand-all");
        click("placed-model-mesh-row");
        click("asset-picker:material:##material");
        click("authored-material-option");
        check("material-assigned");
        key(ImGuiKey_S, true);
        check("saved");
        check("graph-mesh-ready");
        capture("scene-graph-material");
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
        click("button:Enable input contexts");
        capture("input-context-default");
        click("button:Add context");
        capture("input-context-authoring");
        hover("input-context-evaluation:Gameplay");
        capture("input-context-gameplay-policy");
        hover("input-context-evaluation:Context 1");
        capture("input-context-inactive-policy");
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
        steps_.push_back({Kind::SourceEdit, "collision-external-refresh"});
        check("collision-external-refresh");
        capture("collision-refresh-keeps-shape-menu");
        click("collision:kind:compound");
        check("collision-compound");
        capture("collision-compound-document");
        key(ImGuiKey_Z, true);
        check("collision-convex-parts");
        capture("collision-compound-undo");
        click("tab:Scene");
        create("3D Primitive", "Cube");
        key(ImGuiKey_F);
        click("button:+ Add Component");
        text("component-search", "Physics Body");
        click("component-choice:forge.physics_body");
        key(ImGuiKey_Escape);
        click("button:+ Add Component");
        text("component-search", "Asset Collider");
        click("component-choice:forge.asset_collider");
        key(ImGuiKey_Escape);
        click("asset-picker:collision:##Asset");
        click("authored-collision-option");
        check("collision-assigned");
        check("collision-preview");
        capture("collision-published-asset-assigned");
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
        if (step.kind == Kind::SourceEdit && frame_ == 0) {
            if (step.value == "starter-break" || step.value == "starter-restore") {
                const auto path = project_ / "Native/gameplay.cpp";
                if (step.value == "starter-break") {
                    std::ifstream input(path);
                    starter_source_.assign(std::istreambuf_iterator<char>(input), {});
                    write(path, starter_source_ + "\nthis intentionally fails compilation;\n");
                } else
                    write(path, starter_source_);
            } else if (step.value == "collision-external-refresh") {
                const auto path = project_ / "Assets/workflow.collision.json";
                Json source;
                {
                    std::ifstream file(path);
                    file >> source;
                }
                source["nodes"][0]["translation"][0] = .125;
                write(path, source.dump(2));
            } else
                write(project_ / "Assets/Imported/Source-1/workflow.gltf",
                      step.value == "corrupt-external-model" ? "{" : model_source(true).dump());
        }
        if (step.value == "game-native-escape" && frame_ < 2) {
            SDL_Event event{};
            event.type = frame_ == 0 ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            event.key.windowID = window;
            event.key.key = SDLK_ESCAPE;
            event.key.scancode = SDL_SCANCODE_ESCAPE;
            event.key.down = frame_ == 0;
            if (!SDL_PushEvent(&event))
                throw std::runtime_error(SDL_GetError());
        }
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
        const auto limit =
            step.kind == Kind::Check &&
                    (step.value == "compiler-ready" || step.value == "starter-built" ||
                     step.value == "starter-rebuilt" || step.value == "starter-rejected" ||
                     step.value == "graph-ready" || step.value == "graph-rejected")
                ? 180000u
                : 12000u;
        if (SDL_GetTicks() - since_ > limit) {
            failure_ = "Timed out at step " + std::to_string(index_) + ": " + step.value + " " +
                       last_check_;
            if (step.value == "authored-collision-option") {
                failure_ += " expected picker-option:" + collision_asset_;
                for (const auto& [name, target] : ui_targets)
                    if (name.starts_with("picker-option:"))
                        failure_ += " observed " + name +
                                    " enabled=" + std::to_string(target.enabled) +
                                    " y=" + std::to_string(target.minimum.y) + ":" +
                                    std::to_string(target.maximum.y) +
                                    " clip=" + std::to_string(target.clip_minimum.y) + ":" +
                                    std::to_string(target.clip_maximum.y);
            }
        }
        if (step.kind == Kind::SourceText && frame_ == 0) {
            const bool component = step.value == "component-edit";
            require(component || step.value == "system-reverse",
                    "Unknown editor-driven source replacement");
            const auto path = project_ / (component ? "Native/Components/Rotator.hpp"
                                                    : "Native/Systems/RotationSystem.cpp");
            std::ifstream file(path, std::ios::binary);
            require(bool(file), "Generated C++ source disappeared before editor edit");
            generated_input_.assign(std::istreambuf_iterator<char>(file), {});
            if (component)
                generated_input_ += "\n// Edited and saved through the FORGE C++ source editor.\n";
            else {
                const std::string before = "0.008726646259971648";
                const auto pos = generated_input_.find(before);
                require(pos != std::string::npos, "RotationSystem scaffold changed unexpectedly");
                generated_input_.replace(pos, before.size(), "-" + before);
                const auto header = generated_input_.find("#include <cmath>\n");
                require(header != std::string::npos, "RotationSystem has no standard math include");
                generated_input_.insert(header, "#include <cstdio>\n");
                const std::string update = "entity.set<forge::LocalRotation>(next);";
                const auto update_pos = generated_input_.find(update);
                require(update_pos != std::string::npos,
                        "RotationSystem no longer publishes its rotation");
                generated_input_.insert(
                    update_pos + update.size(),
                    "\n                std::fprintf(stderr, \"FORGE_TEST_ROTATOR_HALF=%f\\n\", "
                    "double(half));");
            }
            require(SDL_SetClipboardText(generated_input_.c_str()),
                    "Cannot paste edited C++ source into the editor");
        }
        if ((step.kind == Kind::Click || step.kind == Kind::Hover) && frame_ == 0) {
            const auto target =
                step.value == "saved-cube-row"         ? "entity:" + cube_
                : step.value == "graph-parameter-node" ? "graph:node:" + graph_parameter_
                : step.value == "graph-function-edit"  ? "graph:function-edit:" + graph_function_
                : step.value == "graph-call-node"      ? "graph:node:" + graph_call_
                : step.value == "graph-added-node"     ? "graph:node:" + graph_added_
                : step.value == "graph-output-node"    ? "graph:node:" + graph_output_
                : step.value == "graph-shader-option"  ? "picker-option:" + graph_asset_
                : step.value == "graph-material-parameter"  ? "material:parameter:" + graph_binding_
                : step.value == "graph-material-value"      ? "material:value:" + graph_binding_
                : step.value == "graph-material-revert"     ? "material:revert:" + graph_binding_
                : step.value == "authored-material-option"  ? "picker-option:" + material_asset_
                : step.value == "authored-collision-option" ? "picker-option:" + collision_asset_
                : step.value == "failed-model-problem"      ? "problem:reimport:" + model_asset_
                : step.value == "placed-model-row"          ? "entity:" + model_root_
                : step.value == "placed-model-mesh-row"     ? "entity:" + model_mesh_
                : step.value == "camera-marker"             ? "marker:" + camera_
                : step.value == "light-marker"              ? "marker:" + light_
                : step.value == "saved-scene-asset"         ? "asset:" + scene_
                                                            : step.value;
            const auto it = ui_targets.find(target);
            if (it == ui_targets.end() || !it->second.enabled) {
                io.AddMousePosEvent(pointer_.x, pointer_.y);
                ui_targets.clear();
                return;
            }
            const auto& t = it->second;
            pointer_ = {(t.minimum.x + t.maximum.x) * .5f, (t.minimum.y + t.maximum.y) * .5f};
            // InputScalarN groups four channels and its label. Aim at the
            // first channel rather than the center of the entire group.
            if (step.value == "graph-material-value")
                pointer_.x = t.minimum.x + (t.maximum.x - t.minimum.x) * .1f;
            // Selectable expands its hit rectangle into ItemSpacing. The first
            // visible row can extend above its child clip edge even though its
            // click center is fully visible; scrolling cannot remove that padding.
            if (pointer_.y < t.clip_minimum.y || pointer_.y > t.clip_maximum.y) {
                // Pinned ImGui caps one wheel unit at 0.67 * viewport height.
                // Three units can jump completely over a short dock; half a unit
                // moves at most 0.335 * height and cannot skip its visible span.
                const float direction = pointer_.y < t.clip_minimum.y ? .5f : -.5f;
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
        if (step.kind == Kind::Drag) {
            const auto separator = step.value.find('|');
            auto name =
                frame_ < 4 ? step.value.substr(0, separator) : step.value.substr(separator + 1);
            if (name == "graph-parameter-port")
                name = "graph:output:" + graph_parameter_ + ":out";
            if (name == "graph-added-port")
                name = "graph:output:" + graph_added_ + ":out";
            if (name == "graph-roughness-port")
                name = "graph:input:" + graph_output_ + ":roughness";
            if (name == "graph-surface-port")
                name = "graph:input:" + graph_output_ + ":base_color";
            const auto found = ui_targets.find(name);
            if (found == ui_targets.end() || !found->second.enabled) {
                ui_targets.clear();
                return;
            }
            const auto& t = found->second;
            pointer_ = {(t.minimum.x + t.maximum.x) * .5f, (t.minimum.y + t.maximum.y) * .5f};
            if (pointer_.x < t.clip_minimum.x || pointer_.x > t.clip_maximum.x ||
                pointer_.y < t.clip_minimum.y || pointer_.y > t.clip_maximum.y) {
                failure_ = "Graph port is outside the visible canvas: " + name;
                return;
            }
            io.AddMousePosEvent(pointer_.x, pointer_.y);
            if (frame_ == 2)
                io.AddMouseButtonEvent(0, true);
            if (frame_ == 6)
                io.AddMouseButtonEvent(0, false);
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
        } else if (step.kind == Kind::SourceText) {
            if (frame_ == 0) {
                io.AddKeyEvent(ImGuiMod_Ctrl, true);
                io.AddKeyEvent(ImGuiKey_A, true);
            }
            if (frame_ == 1)
                io.AddKeyEvent(ImGuiKey_A, false);
            if (frame_ == 2)
                io.AddKeyEvent(ImGuiKey_V, true);
            if (frame_ == 3) {
                io.AddKeyEvent(ImGuiKey_V, false);
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
            }
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
        } else if (step.kind == Kind::Key && frame_ < 2 && step.value != "game-native-escape") {
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
        if (steps_[index_].kind == Kind::Check && steps_[index_].value == "compiler-ready" &&
            !state.at("sdk_build_busy").get<bool>() &&
            !state.at("sdk_build_error").get<std::string>().empty())
            failure_ =
                "Compiler readiness failed: " + state.at("sdk_build_error").get<std::string>();
        if (!failure_.empty()) {
            image("FAILED-" + std::to_string(index_));
            Json available = Json::object();
            for (const auto& [name, target] : ui_targets)
                available[name] = {
                    {"enabled", target.enabled},
                    {"clip",
                     {target.clip_minimum.x, target.clip_minimum.y, target.clip_maximum.x,
                      target.clip_maximum.y}},
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
                // A real mouse press can straddle an asynchronous enabled-state
                // change. Retry only an untouched stopped session, at most twice;
                // preserve reported startup failures rather than masking them.
                if (step.value == "starter-ticked" && !state.at("playing").get<bool>() &&
                    state.at("play_status").get<std::string>().starts_with("Stopped.") &&
                    state.at("play_log").get<std::string>().empty() &&
                    state.at("problems").empty() && play_click_retries_ < 2 &&
                    SDL_GetTicks() - since_ > 500 && index_ > 0 &&
                    steps_[index_ - 1].kind == Kind::Click &&
                    steps_[index_ - 1].value == "icon:play") {
                    ++play_click_retries_;
                    trace_.push_back({{"step", index_},
                                      {"operation", "retry stopped toolbar click"},
                                      {"attempt", play_click_retries_ + 1}});
                    --index_;
                    frame_ = 0;
                    since_ = 0;
                    return;
                }
                // Keep current failure evidence even if CTest's overall limit
                // expires before this individual readiness check does.
                if (SDL_GetTicks() - checkpoint_ >= 5000) {
                    checkpoint_ = SDL_GetTicks();
                    record({{"ok", false},
                            {"completed_steps", index_},
                            {"total_steps", steps_.size()},
                            {"waiting_for", step.value},
                            {"last_check", last_check_},
                            {"trace", trace_},
                            {"state", state}});
                }
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
        constexpr const char* names[] = {"click",
                                         "drag",
                                         "hover",
                                         "type",
                                         "shortcut",
                                         "assert",
                                         "capture",
                                         "SDL file drop",
                                         "external source edit",
                                         "type generated C++ source"};
        static_assert(std::size(names) == static_cast<unsigned>(Kind::SourceText) + 1);
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
