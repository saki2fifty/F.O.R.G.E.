#include "animation_playback_access.hpp"
#include "editor/animation_graph_editor.hpp"
#include "editor/animation_preview.hpp"
#include "editor/component_inspector.hpp"
#include "editor/runtime_tuning_review.hpp"
#include "imgui_input_driver.hpp"
#include "imgui_software_capture.hpp"
#include "model_placement.hpp"
#include <forge/runtime_world.hpp>
#include <imgui_internal.h>
#include <iostream>
#include <thread>

using namespace forge;
using namespace forge::asset_detail;
namespace {
void require(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
void exercise(const std::filesystem::path& prepared, const std::filesystem::path& output,
              float scale, bool narrow) {
    const auto root = output / AssetId::generate().str();
    SceneDocument::create_project(root, "Animation graph controls");
    for (const auto& path :
         {std::filesystem::path("Assets"), std::filesystem::path(".forge/cache")}) {
        std::filesystem::create_directories((root / path).parent_path());
        std::filesystem::copy(prepared / path, root / path,
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::overwrite_existing);
    }
    auto catalog = AssetCatalog::open_project(root);
    std::vector<AssetRecord> records;
    for (const auto& [id, record] : catalog.records())
        records.push_back(record);
    const auto prepared_catalog = AssetCatalog::open_project(prepared);
    AssetId graph;
    for (const auto& [id, record] : prepared_catalog.records()) {
        records.push_back(record);
        if (record.type == AnimationGraphAsset::type)
            graph = id;
    }
    catalog.replace_all(std::move(records));
    catalog.save(AssetCatalog::project_index(root));
    const auto source = AnimationGraphSource::parse(
        read_bytes(root / "Assets/Layered.animgraph.json", animation_graph_byte_limit));
    const auto skeleton = source.compiled().skeleton;
    const auto model = catalog.records().at(skeleton).subasset->owner;
    const auto selection = load_model_selection(root, catalog, model);
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    ModelPlacementOptions options;
    options.clip = {selection.bindings.at("/animations/0")};
    const auto actor = instantiate_model(
        scene, catalog,
        prepare_model_placement(selection, scene.asset_id(), scene.revision(), options));
    const auto original_scene = scene.document();
    test::ImGuiSoftwareCapture capture(int((narrow ? 520 : 1000) * scale), int(1100 * scale));
    auto& io = ImGui::GetIO();
    io.ConfigInputTrickleEventQueue = false;
    ui::style(scale);
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    context.selection.select_entity(actor.str());
    AnimationGraphEditor editor;
    editor.open(project, "Assets/Layered.animgraph.json");
    require(editor.document() && editor.diagnostic().empty(), editor.diagnostic());
    bool locked = false, rasterize = false;
    unsigned view = 0;
    AnimationPreviewEditor preview;
    ComponentInspector inspector;
    RuntimeTuningReview review;
    std::unique_ptr<RuntimeWorld> runtime;
    const auto live_snapshot = [&] {
        auto document = runtime->scene.effective_document();
        for (auto& row : document["entities"])
            if (row.at("id").get<EntityId>() == actor)
                row["animation_playback"] =
                    AnimationPlaybackAccess::status(*animation_runtime(runtime->engine.world()),
                                                    runtime->scene.reference(actor.str()));
        return document;
    };
    std::string message;
    test::observe_ui = true;
    const auto frame = [&] {
        editor.poll(project, message);
        if (view == 1)
            preview.poll(project, scene, false, 0);
        test::ui_targets.clear();
        capture.new_frame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
        if (view == 0)
            editor.draw(project, scene, locked);
        else if (view == 1)
            preview.draw(project, scene, locked);
        else {
            ImGui::Begin("Graph Live Inspector");
            inspector.draw_runtime(
                live_snapshot(), runtime->scene.schema(), root, actor.str(),
                [&](const auto& type, const auto& property, const auto& value) {
                    (void)runtime->tune_property(runtime->scene.reference(actor.str()), type,
                                                 property, value);
                    return true;
                },
                false,
                [&] {
                    require(review.capture(scene, project, live_snapshot(), runtime->scene.schema(),
                                           actor.str(), 0),
                            "Graph live-value review could not capture changed inputs");
                });
            ImGui::End();
        }
        if (rasterize)
            capture.render();
        else
            ImGui::Render();
    };
    const auto wait = [&](auto done) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(25);
        while (!done()) {
            frame();
            require(std::chrono::steady_clock::now() < deadline,
                    "Native graph operation timed out: " + editor.diagnostic());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        frame();
    };
    frame();
    frame();
    const auto draft = [&] { return editor.document()->source().document; };
    const auto initial = draft();
    test::ImGuiInputDriver input(frame);
    input.type("animation_pose_graph:/cycle_seconds", "0");
    require(draft() == initial && editor.has_unapplied_edits() && !editor.diagnostic().empty(),
            "Invalid graph cycle changed source or discarded the pending value");
    editor.undo();
    frame();
    require(!editor.has_unapplied_edits() && draft() == initial,
            "Undo did not cancel the invalid field");
    input.type("animation_pose_graph:/cycle_seconds", "3.5");
    require(draft().at("cycle_seconds") == 3.5 && !editor.has_unapplied_edits(),
            "Native graph cycle did not commit after Enter");
    input.click("animation_pose_graph:undo");
    require(draft() == initial, "Graph cycle Undo changed unrelated fields");
    input.click("animation_pose_graph:redo");
    require(draft().at("cycle_seconds") == 3.5, "Graph cycle Redo failed");
    input.type("animation_pose_graph:/inputs/0/name", "Layer strength");
    input.type("animation_pose_graph:/inputs/0/initial", "0.25");
    input.type("animation_pose_graph:/nodes/0/weight/scale", "2");
    input.type("animation_pose_graph:/nodes/0/weight/bias", "-0.125");
    require(draft().at("inputs")[0].at("slot") == 1 &&
                draft().at("inputs")[0].at("name") == "Layer strength" &&
                draft().at("inputs")[0].at("initial") == .25 &&
                draft().at("nodes")[0].at("weight").at("scale") == 2 &&
                draft().at("nodes")[0].at("weight").at("bias") == -.125,
            "Graph input labels/defaults or bound weight fields failed");
    input.click("animation_pose_graph:add_input");
    require(draft().at("inputs").size() == 2 && draft().at("inputs")[1].at("slot") == 2,
            "Add input renumbered the existing slot");
    input.click("animation_pose_graph:remove_input:2");
    require(draft().at("inputs").size() == 1, "Remove unused input failed");
    frame();
    require(!test::ui_targets.at("animation_pose_graph:remove_input:1").enabled,
            "Bound graph input could be removed");
    const auto before_nodes = draft();
    input.click("animation_pose_graph:add:blend");
    require(draft().at("nodes").size() == 4, "Add blend node failed");
    input.click("animation_pose_graph:remove_node");
    require(draft() == before_nodes, "Remove new node changed existing identities/links");
    input.click("animation_pose_graph:undo");
    require(draft().at("nodes").size() == 4, "Remove node was not one Source Undo step");
    input.click("animation_pose_graph:undo");
    require(draft() == before_nodes, "Add node was not one Source Undo step");
    // Reopen the selection with the actual canvas, then rewire its second socket.
    input.click("animation_pose_graph:node:1");
    const auto before_link = draft();
    input.click("animation_pose_graph:out:2");
    input.click("animation_pose_graph:in:1:2");
    require(draft().at("nodes")[0].at("second") == initial.at("nodes")[1].at("id"),
            "Canvas input2 did not save the source UUID");
    input.click("animation_pose_graph:undo");
    require(draft() == before_link, "Canvas connection was not one source Undo step");
    const auto before_arrange = draft();
    input.click("animation_pose_graph:arrange");
    // Default grid positions need no source edit; explicitly move one card below.
    input.click("animation_pose_graph:node:1");
    const auto box = test::ui_targets.at("animation_pose_graph:node:1");
    const auto center =
        ImVec2{(box.minimum.x + box.maximum.x) / 2, (box.minimum.y + box.maximum.y) / 2};
    io.AddMousePosEvent(center.x, center.y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMousePosEvent(center.x + 25 * scale, center.y + 12 * scale);
    frame();
    require(editor.canvas.draft_dirty(), "Graph card drag did not retain a pending gesture");
    editor.undo();
    io.AddMouseButtonEvent(0, false);
    frame();
    frame();
    require(draft() == before_arrange && !editor.canvas.draft_dirty(),
            "Cancelled drag reapplied after Undo");
    io.AddMousePosEvent(center.x, center.y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMousePosEvent(center.x + 30 * scale, center.y + 15 * scale);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    require(draft().contains("editor_layout") && draft().at("nodes") == before_arrange.at("nodes"),
            "Graph movement did not save UUID positions or changed node values");
    input.click("animation_pose_graph:undo");
    require(draft() == before_arrange, "Graph move was not one Source Undo step");
    const auto final_source = draft();
    locked = true;
    frame();
    for (const auto* key : {"save", "add:clip", "add_input", "/cycle_seconds", "attach"})
        require(!test::ui_targets.at("animation_pose_graph:" + std::string(key)).enabled,
                "Graph editing remained enabled during Play");
    locked = false;
    frame();
    input.click("animation_pose_graph:save");
    wait([&] { return !editor.pending() && !editor.dirty(); });
    require(editor.diagnostic().empty() && scene.document() == original_scene,
            "Graph publication changed the Scene or failed");
    input.click("animation_pose_graph:attach");
    require(editor.diagnostic().empty() &&
                scene.entity(actor.str()).get<Animator>().graph.id == graph &&
                !scene.entity(actor.str()).get<Animator>().clip.id,
            "Graph attachment did not select graph mode atomically: " + editor.diagnostic());
    const auto attached = scene.document();
    require(scene.undo() && scene.document() == original_scene && scene.redo() &&
                scene.document() == attached,
            "Graph attachment did not use one Scene Undo/Redo transaction");
    ImGui::SetScrollY(ImGui::FindWindowByName("Animation Graph"), 0);
    rasterize = true;
    frame();
    frame();
    capture.save(output / (std::string(narrow ? "narrow-" : "wide-") +
                           std::to_string(int(scale * 100)) + ".png"));
    rasterize = false;
    view = 1;
    preview.open(project, scene);
    frame();
    frame();
    input.click("button:Play preview");
    wait([&] { return test::ui_targets.contains("animation_preview:graph_override:1"); });
    input.click("animation_preview:graph_override:1");
    input.type("animation_preview:graph_input:1", "0.75");
    const auto preview_doc = preview.snapshot(scene.document());
    for (const auto& row : preview_doc.at("entities"))
        if (row.at("id").get<EntityId>() == actor)
            require(row.at("animation_pose").at("graph_inputs")[0].at("value") == .75,
                    "Native graph preview did not adopt its named input");
    require(scene.document() == attached, "Native graph preview changed the authored Scene");
    input.click("button:Apply preview settings to Animator");
    require(scene.entity(actor.str()).get<Animator>().graph_override_1 &&
                scene.entity(actor.str()).get<Animator>().graph_input_1 == .75f,
            "Apply preview settings did not save graph inputs");
    const auto applied_preview = scene.document();
    require(scene.undo() && scene.document() == attached && scene.redo() &&
                scene.document() == applied_preview,
            "Applying graph preview settings was not one Scene Undo/Redo step");
    require(scene.undo() && scene.document() == attached,
            "Could not restore graph preview baseline");
    preview.close();
    runtime = std::make_unique<RuntimeWorld>(std::vector<EngineModule>{}, PhysicsConfig{},
                                             std::nullopt, root, false);
    runtime->scene.restore_snapshot(attached);
    const auto animation = animation_runtime(runtime->engine.world());
    wait([&] { return animation->prepare_initial_pose(); });
    view = 2;
    frame();
    frame();
    require(!test::ui_targets.contains("runtime-field:forge.animator:graph_input_2") &&
                !test::ui_targets.contains("runtime-field:forge.animator:skeleton"),
            "Live graph Inspector exposed an absent input slot or asset assignment");
    input.click("runtime-field:forge.animator:graph_override_1");
    input.type("runtime-field:forge.animator:graph_input_1", "0.625");
    require(runtime->scene.entity(actor.str()).get<Animator>().graph_input_1 == .625f &&
                scene.document() == attached,
            "Live graph input changed authored values or failed to reach the runtime owner");
    input.click("runtime:keep_values");
    require(review.fields().size() == 2, "Graph tuning review captured non-input fields");
    rasterize = true;
    frame();
    frame();
    capture.save(output / (std::string(narrow ? "live-narrow-" : "live-wide-") +
                           std::to_string(int(scale * 100)) + ".png"));
    rasterize = false;
    runtime.reset();
    view = 0;
    require(review.apply(scene, project, false), "Kept graph inputs did not apply after Stop");
    const auto kept = scene.document();
    require(scene.entity(actor.str()).get<Animator>().graph_input_1 == .625f && scene.undo() &&
                scene.document() == attached && scene.redo() && scene.document() == kept,
            "Keeping graph inputs was not one ordinary Scene Undo/Redo step");
    const auto session = editor.document()->session();
    editor.request_close();
    require(!editor.is_open(), "Published graph did not close");
    editor.open(project, "Assets/Layered.animgraph.json");
    frame();
    require(editor.document()->session() != session && draft() == final_source && !editor.dirty(),
            "Published graph did not reopen with exact values and a fresh Source session");
}
void capture_gallery(const std::filesystem::path& gallery, const std::filesystem::path& output) {
    const auto root = output / "SourceProject";
    std::filesystem::create_directories(output);
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    scene.load(root / "Scenes/180-animation-graphs.scene.json");
    test::ImGuiSoftwareCapture capture(1100, 1100);
    ui::style(1);
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    AnimationGraphEditor editor;
    editor.open(project, "Assets/AnimationGraphs/Layered.animgraph.json");
    require(editor.document() && !editor.dirty() && editor.diagnostic().empty(),
            "Gallery graph source did not open as its exact published document");
    for (unsigned i = 0; i < 3; ++i) {
        capture.new_frame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
        editor.draw(project, scene, false);
        capture.render();
    }
    capture.save(output / "gallery-source.png");
    require(!editor.dirty(), "Capturing the graph view changed its source");
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string_view(argv[1]) == "--capture-gallery") {
            capture_gallery(argv[2], argv[3]);
            return 0;
        }
        if (argc != 3)
            return 2;
        std::filesystem::create_directories(argv[2]);
        for (float scale : {1.f, 1.5f, 2.f})
            for (bool narrow : {false, true})
                exercise(argv[1], argv[2], scale, narrow);
        std::cout << "Native animation graph/preview/live input controls/history/publication at "
                     "six layouts passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
