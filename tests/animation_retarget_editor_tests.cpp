#include "editor/animation_retarget_editor.hpp"
#include "imgui_input_driver.hpp"
#include "imgui_software_capture.hpp"
#include "model_placement.hpp"
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
    SceneDocument::create_project(root, "Retarget controls");
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
    for (const auto& [id, record] : prepared_catalog.records())
        records.push_back(record);
    catalog.replace_all(std::move(records));
    catalog.save(AssetCatalog::project_index(root));
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    AssetId baked;
    for (const auto& [id, record] : catalog.records())
        if (record.source == "Assets/Motion.retarget.json")
            baked = id;
    require(bool(baked), "Prepared retarget clip missing");
    const auto target = catalog.records().at(baked).metadata.at("forge.animation_clip");
    const auto selection = load_model_selection(root, catalog, target.at("model").get<AssetId>());
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
    AnimationRetargetEditor editor;
    editor.open(project, "Assets/Motion.retarget.json");
    require(editor.document() && editor.diagnostic().empty(), editor.diagnostic());
    bool locked = false;
    bool rasterize = false;
    std::string message;
    test::observe_ui = true;
    const auto frame = [&] {
        editor.poll(project, message);
        test::ui_targets.clear();
        capture.new_frame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
        editor.draw(project, scene, locked);
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
                    "Native retarget operation timed out: " + editor.diagnostic());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        frame();
    };
    wait([&] { return !editor.inspecting(); });
    require(test::ui_targets.contains("animation_retarget:source_joint"),
            "Published joint inspection did not populate native pickers");
    const auto draft = [&] { return editor.document()->source().document; };
    const auto initial = draft();
    test::ImGuiInputDriver input(frame);
    input.type("animation_retarget:rate", "0");
    require(draft() == initial && !editor.diagnostic().empty(),
            "Invalid native sampling rate changed the recipe");
    input.type("animation_retarget:rate", "30");
    require(draft().at("sampling_rate") == 30 && editor.diagnostic().empty(),
            "Native sampling rate was not committed");
    input.click("button:Undo");
    require(draft() == initial, "Native retarget Undo changed unrelated settings");
    input.click("button:Redo");
    require(draft().at("sampling_rate") == 30, "Native retarget Redo failed");
    const auto before_root = draft();
    input.click("animation_retarget:root_translation");
    require(draft().at("root_translation") == false, "Root translation checkbox failed");
    input.click("button:Undo");
    require(draft() == before_root, "Root translation Undo failed");
    input.type("animation_retarget:translation_scale", "1.75");
    require(draft().at("translation_scale") == 1.75, "Root translation scale field failed");
    const auto before_alignment = draft();
    input.type("animation_retarget:alignment", "45");
    require(draft().at("alignment") != before_alignment.at("alignment"),
            "Native degree alignment did not commit a rotation");
    input.click("button:Undo");
    require(draft() == before_alignment, "Alignment Undo changed the recipe");
    const auto before_pairs = draft();
    const auto last = before_pairs.at("entries").back();
    const auto index = before_pairs.at("entries").size() - 1;
    input.click(("animation_retarget:remove:" + std::to_string(index)).c_str());
    require(draft().at("entries").size() == index, "Native joint pair removal failed");
    for (const auto* side : {"source", "target"}) {
        input.click(("animation_retarget:" + std::string(side) + "_joint").c_str());
        input.select(("picker-option:" + last.at(side).get<AssetId>().str()).c_str());
    }
    input.click("animation_retarget:add_pair");
    require(draft() == before_pairs, "Native explicit joint pickers did not restore the pair");
    frame();
    require(!test::ui_targets.at("animation_retarget:add_pair").enabled,
            "Duplicate source/target joint mapping stayed enabled");
    input.click("animation_retarget:clear");
    require(draft().at("entries").empty(), "Clear mapping failed");
    input.click("button:Undo");
    require(draft() == before_pairs, "Clear mapping was not one Source Undo step");
    input.click("button:Redo");
    require(draft().at("entries").empty(), "Clear mapping Redo failed");
    input.click("button:Undo");
    require(draft() == before_pairs && scene.document() == original_scene,
            "Retarget recipe edits changed the authored Scene");
    locked = true;
    frame();
    for (const auto* key :
         {"rate", "source_skeleton", "source_clip", "target_skeleton", "root_translation",
          "translation_scale", "alignment", "clear", "attach", "save"})
        require(!test::ui_targets.at("animation_retarget:" + std::string(key)).enabled,
                "Native retarget edit remained enabled during Play");
    locked = false;
    frame();
    input.click("animation_retarget:save");
    wait([&] { return !editor.pending() && !editor.dirty(); });
    require(editor.diagnostic().empty(), editor.diagnostic());
    input.click("animation_retarget:attach");
    require(editor.diagnostic().empty() &&
                scene.entity(actor.str()).get<Animator>().clip.id == baked,
            "Published retarget clip did not attach through the native control");
    const auto attached = scene.document();
    require(scene.undo() && scene.document() == original_scene && scene.redo() &&
                scene.document() == attached,
            "Retarget attachment did not use one Scene Undo/Redo transaction");
    const auto name = std::string(narrow ? "narrow-" : "wide-") + std::to_string(int(scale * 100));
    // Capture the actual production panel at the top; scrolling checks above
    // exercise every control even when the narrow window needs vertical scrolling.
    ImGui::SetScrollY(ImGui::FindWindowByName("Animation Retarget"), 0);
    rasterize = true;
    frame();
    frame();
    capture.save(output / (name + ".png"));
    rasterize = false;
    const auto session = editor.document()->session();
    editor.request_close();
    require(!editor.is_open(), "Published recipe did not close");
    editor.open(project, "Assets/Motion.retarget.json");
    frame(); // Start inspection, then close before accepting its result.
    editor.request_close();
    frame();
    require(!editor.is_open() && !test::ui_targets.contains("animation_retarget:source_joint"),
            "Closed document accepted a stale skeleton inspection");
    editor.open(project, "Assets/Motion.retarget.json");
    require(editor.document()->session() != session, "Reopen retained the old Source session");
    wait([&] { return !editor.inspecting(); });
    require(draft() == before_pairs && editor.diagnostic().empty(),
            "Published retarget recipe did not survive reopen");
    editor.asset_catalog_changed(
        std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
    frame();
    require(!editor.inspecting() && test::ui_targets.contains("animation_retarget:source_joint"),
            "Unchanged selected revisions unnecessarily discarded joint inspection");
    editor.request_close();
    test::observe_ui = false;
    std::cout << name
              << " native controls, history, publication, attachment and lifecycle passed\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need prepared retarget project and UI scratch");
        std::filesystem::create_directories(argv[2]);
        for (float scale : {1.f, 1.5f, 2.f})
            for (bool narrow : {false, true})
                exercise(argv[1], argv[2], scale, narrow);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
