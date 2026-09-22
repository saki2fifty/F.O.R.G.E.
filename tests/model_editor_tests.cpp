#define FORGE_UI_FIXTURE 1
#include "component_inspector.hpp"
#include "content.hpp"
#include "model_imports.hpp"
#include "scene_asset_drop.hpp"
#include "source_import.hpp"
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void save(const std::filesystem::path& path, const Json& source) {
    std::ofstream out(path);
    out << source.dump();
    require(bool(out.flush()), "Cannot save owned model editor fixture");
}
void check_content_refresh(const std::filesystem::path& root) {
    EngineContext engine;
    Scene scene(engine.world());
    std::vector<std::string> recent;
    EditorFiles files(scene, nullptr, recent);
    const auto a = root / "ContentA", b = root / "ContentB";
    std::filesystem::create_directories(a);
    std::filesystem::create_directories(b);
    files.document.open_project(a, true);
    const auto first = scene.asset_id();
    save(a / "First.scene.json", scene.document());
    ContentBrowser content;
    content.refresh(files);
    require(content.refreshing() && !content.record(first),
            "Content scan performed synchronous discovery/adoption");
    auto finish = [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (content.refreshing() && std::chrono::steady_clock::now() < deadline) {
            content.poll(files);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(!content.refreshing(), "Content scan did not settle");
    };
    for (unsigned i = 0; i < 8; ++i)
        content.refresh(files);
    finish();
    require(content.record(first), "Asynchronous discovery lost scene AssetId");
    std::ofstream(a / "forge.assets.json") << "{invalid";
    content.refresh(files);
    finish();
    require(content.record(first), "Failed discovery destroyed the last good list");
    content.refresh(files);
    files.document.open_project(b, true);
    auto second_scene = scene.document();
    const auto second = AssetId::generate();
    second_scene["asset_id"] = second;
    save(b / "Second.scene.json", second_scene);
    content.poll(files);
    require(!content.record(first), "Project switch retained previous Content selection");
    finish();
    require(content.record(second) && !content.record(first),
            "An old Content job published into the newly opened project");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need model worker and scratch root");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        check_content_refresh(root);
        const auto path = root / "Assets/model.gltf";
        Json source{{"asset", {{"version", "2.0"}}},
                    {"extensionsUsed", {"VENDOR_optional_fixture"}},
                    {"scene", 0},
                    {"scenes", Json::array({{{"nodes", {0, 1}}}})},
                    {"nodes", Json::array({{{"name", "Duplicate"}}, {{"name", "Duplicate"}}})}};
        save(path, source);
        EngineContext engine;
        Scene scene(engine.world());
        std::vector<std::string> recent;
        EditorFiles files(scene, nullptr, recent);
        auto& document = files.document;
        document.open_project(root, true);
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1100, 1000};
        io.DeltaTime = 1.f / 60;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        ui::EditorUiContext context;
        ui::ContextScope scope(context);
        ModelImportEditor editor(std::filesystem::absolute(argv[1]), {}, scene, context.selection);
        std::string message;
        auto frame = [&] {
            editor.poll(document, message);
            ImGui::NewFrame();
            editor.draw(document, false);
            ImGui::Render();
        };
        auto wait = [&](auto done) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            do {
                frame();
                if (done())
                    return;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            } while (std::chrono::steady_clock::now() < deadline);
            throw std::runtime_error("Model editor wait expired: " + message);
        };
        editor.open(document, "Assets/model.gltf");
        frame();
        require(editor.dirty(), "New model import lacks a close/save guard");
        editor.request_close();
        frame();
        require(ImGui::GetTopMostPopupModal(), "Model close bypassed its import draft");
        editor.request_save();
        wait([&] { return !editor.is_open(); });
        frame();
        require(!editor.dirty() && !ImGui::GetTopMostPopupModal(),
                "Model import did not finish guarded close");
        require(!scene.can_undo(), "Model import entered scene history");
        editor.open(document, "Assets/model.gltf");
        wait([&] { return editor.placement_ready(); });
        require(context.task.id() == "model_import", "Model document did not own Save focus");
        const auto owner = editor.selected_asset();
        require(std::any_of(context.problems.items().begin(), context.problems.items().end(),
                            [&](const auto& note) {
                                return note.asset == owner && note.severity == "Warning" &&
                                       note.source == "Assets/model.gltf" &&
                                       note.text.find("VENDOR_optional_fixture") !=
                                           std::string::npos;
                            }),
                "Published optional-extension warning lacks visible asset/source context");
        const auto baseline = read_json(root / "forge.assets.json");
        source.erase("extensionsUsed");
        source["asset"]["generator"] = "revision2";
        save(path, source);
        editor.request_save();
        frame();
        wait([&] { return !editor.pending(); });
        require(editor.identity_conflicts().size() == 1 &&
                    read_json(root / "forge.assets.json") == baseline,
                "Ambiguous model source changed selected identity without review");
        auto decide = [&] {
            const auto conflicts = editor.identity_conflicts();
            for (const auto& c : conflicts) {
                require(c.observations.size() == c.previous.size(),
                        "Unexpected fixture correspondence cardinality");
                for (unsigned i = 0; i < c.observations.size(); ++i)
                    editor.decide_identity(c.observations[i], c.previous[i]);
            }
        };
        decide();
        require(editor.dirty(), "Identity decisions were not guarded as a pending draft");
        source["asset"]["generator"] = "revision3";
        save(path, source);
        editor.request_save();
        frame();
        wait([&] { return !editor.pending(); });
        require(read_json(root / "forge.assets.json") == baseline &&
                    message.find("Source/settings changed") != std::string::npos,
                "Stale identity decisions were applied to a different source input");
        editor.request_save();
        frame();
        wait([&] { return !editor.pending(); });
        require(!editor.identity_conflicts().empty(),
                "Changed input did not request a fresh identity review");
        decide();
        editor.request_save();
        frame();
        wait([&] { return !editor.pending() && editor.placement_ready(); });
        editor.placement_allowed = [] { return false; };
        bool locked_placement = false;
        const auto locked_scene = scene.document();
        try {
            editor.place(document);
        } catch (const std::exception&) {
            locked_placement = true;
        }
        require(locked_placement && scene.document() == locked_scene,
                "Import document bypassed host scene-edit lock");
        editor.placement_allowed = [] { return true; };
        require(!editor.dirty() && editor.selected_asset() == owner,
                "Reviewed model reimport lost root identity");
        require(std::none_of(context.problems.items().begin(), context.problems.items().end(),
                             [&](const auto& note) {
                                 return note.asset == owner &&
                                        note.text.find("VENDOR_optional_fixture") !=
                                            std::string::npos;
                             }),
                "Successful clean model revision retained an obsolete import warning");
        const auto before = scene.document();
        asset_detail::ModelPlacementOptions options;
        options.name = "Placed signed model";
        options.transform.scale = {-1, 0, 1};
        const auto placed = editor.place(document, options);
        require(scene.entity_count() == 3 && context.selection.entity() == placed.str(),
                "Model editor did not place/select ordinary entities");
        const auto after = scene.document();
        // ModelSource is non-optional provenance. Its controls must be reachable
        // without falsely advertising it as an addable/removable behavior.
        ComponentInspector inspector;
        for (unsigned i = 0; i < 3; ++i) {
            ImGui::NewFrame();
            ImGui::SetNextWindowSize({1000, 850});
            ImGui::Begin("Model inspector regression");
            ui::fixture_open_model_variant = true;
            inspector.draw(scene, document, placed.str());
            require(!ui::fixture_open_model_variant &&
                        !ImGui::GetCurrentContext()->OpenPopupStack.empty(),
                    "Placed model material variants are unreachable in the Inspector");
            ImGui::End();
            ImGui::Render();
        }
        require(scene.document() == after, "Viewing model provenance mutated authored data");
        ImGui::ClosePopupToLevel(0, true);
        require(scene.undo() && scene.document() == before && !scene.can_undo(),
                "Model placement was not one scene Undo step");
        require(scene.redo() && scene.document() == after,
                "Model placement Redo changed identities");
        const auto catalog = read_json(root / "forge.assets.json");
        std::ofstream(path) << "{invalid";
        editor.request_save();
        frame();
        wait([&] { return !editor.pending(); });
        require(read_json(root / "forge.assets.json") == catalog && scene.document() == after,
                "Invalid model reimport replaced usable assets or mutated the scene");
        // Content-to-Scene uses deferred owner-thread commands and one history
        // boundary. The published last-good model remains placeable after failure.
        SceneAssetDrop drop;
        files.external_busy = [&] { return drop.busy(); };
        auto placement_wait = [&] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (drop.busy() && std::chrono::steady_clock::now() < deadline) {
                drop.poll(scene, files, context.selection, false, message);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            require(!drop.busy(), "Deferred model placement did not settle");
        };
        const auto assets = AssetCatalog::open_project(root);
        const auto model_record = assets.records().at(owner);
        drop.queue(model_record, scene, document, {7, 8, 9});
        require(scene.document() == after, "Drop mutated the scene while drawing");
        placement_wait();
        require(scene.entity_count() == 6, message.c_str());
        require(scene.undo() && scene.document() == after,
                "Deferred model drop was not one undoable scene placement");
        drop.queue(model_record, scene, document, {});
        authoring_command(scene, "entity.rename", {{"entity", placed}, {"name", "Newer edit"}});
        const auto newer = scene.document();
        placement_wait();
        require(scene.document() == newer, "Stale drop overwrote a newer scene edit");
        require(scene.undo() && scene.document() == after, "Drop cancellation modified history");
        drop.queue(model_record, scene, document, {});
        drop.cancel();
        placement_wait();
        require(scene.document() == after, "Cancelled drop changed the scene");
        auto wrong = model_record;
        wrong.type = "mesh";
        drop.queue(wrong, scene, document, {});
        placement_wait();
        require(scene.document() == after, "Model was silently coerced into a Mesh");
        const auto builtin = assets.resolve(engine_primitive(0));
        drop.queue(*builtin.record, scene, document, {2, 3, 4});
        placement_wait();
        auto mesh_doc = scene.document();
        const auto& mesh_row = mesh_doc.at("entities").back();
        require(mesh_row.at("components").at("forge.mesh_renderer").at("mesh") ==
                        Json(builtin.record->id) &&
                    mesh_row.at("components").at("forge.local_translation").at("x") == 2,
                "Mesh drop lost the typed reference or placement");
        require(scene.undo() && scene.document() == after, "Mesh drop was not one Undo step");
        const auto prefab = document.prefabs().create(
            scene, create_prefab_source(scene, placed.str()), "Assets/drop.prefab.json");
        const auto before_prefab = scene.document();
        drop.queue(document.prefabs().records().at(prefab), scene, document, {4, 5, 6});
        placement_wait();
        const auto prefab_doc = scene.document();
        const auto root_id = context.selection.entity();
        bool prefab_root = false;
        for (const auto& row : prefab_doc.at("entities"))
            if (row.at("id") == root_id) {
                const auto& owned = row.at("components");
                prefab_root =
                    row.contains("prefab_instance") && owned.contains("forge.local_translation") &&
                    !owned.contains("forge.local_rotation") && !owned.contains("forge.local_scale");
            }
        require(prefab_root, "Prefab placement accidentally owned rotation or scale");
        require(scene.undo() && scene.document() == before_prefab,
                "Prefab placement and translation were not one Undo step");
        AssetRecord scene_record{scene.asset_id(), "scene", "Dropped.scene.json", 3, {}};
        save(root / scene_record.source, scene.document());
        drop.queue(scene_record, scene, document, {});
        placement_wait();
        require(files.busy() && scene.document() == before_prefab,
                "Scene drop bypassed the unsaved document guard");
        files.resolve_pending(EditorFiles::Resolution::Cancel);
        require(!files.busy() && scene.document() == before_prefab,
                "Cancelling scene Open lost the current document");
        const auto incoming = root / "Incoming";
        std::filesystem::create_directory(incoming);
        save(incoming / "first.gltf",
             {{"asset", {{"version", "2.0"}}}, {"nodes", Json::array({{{"name", "Imported"}}})}});
        save(incoming / "bad.gltf",
             {{"asset", {{"version", "2.0"}}},
              {"meshes",
               Json::array({{{"primitives", Json::array({{{"attributes", Json::object()}}})}}})},
              {"nodes", Json::array({{{"mesh", 0}}})}});
        SourceImport incoming_import;
        ContentImports background;
        incoming_import.routes = [&] { return editor.automatic_routes(); };
        incoming_import.select(document, {incoming / "first.gltf", incoming / "bad.gltf"});
        incoming_import.prepare();
        auto import_frame = [&] {
            incoming_import.poll(document, background, message);
            ImGui::NewFrame();
            incoming_import.draw(false);
            ImGui::Render();
        };
        auto import_wait = [&](auto done) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            do {
                import_frame();
                require(incoming_import.diagnostic().empty(), incoming_import.diagnostic().c_str());
                if (done())
                    return;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            } while (std::chrono::steady_clock::now() < deadline);
            throw std::runtime_error("Source import UI did not settle");
        };
        import_wait([&] { return incoming_import.review_ready(); });
        ui::style(2);
        io.DisplaySize = {1920, 1080};
        import_frame();
        ui::style(1);
        io.DisplaySize = {1100, 1000};
        require(!std::filesystem::exists(root / "Assets/Imported"),
                "Preparing source import copied files without confirmation");
        incoming_import.copy_sources(true);
        import_wait([&] { return incoming_import.finished(); });
        require(incoming_import.published_count() == 1 &&
                    std::filesystem::exists(root / "Assets/Imported/Source-2/bad.gltf") &&
                    scene.document() == before_prefab,
                "Batch did not retain first success/failed source or changed scene history");
        incoming_import.close();
        import_frame();
        editor.request_close();
        frame();
        ImGui::DestroyContext();
        std::cout << "Model editor import, identity review/stale decisions, placement/history and "
                     "failure retention passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
