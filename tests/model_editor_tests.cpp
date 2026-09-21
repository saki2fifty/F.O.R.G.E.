#include "model_imports.hpp"
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
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need model worker and scratch root");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        const auto path = root / "Assets/model.gltf";
        Json source{{"asset", {{"version", "2.0"}}},
                    {"scene", 0},
                    {"scenes", Json::array({{{"nodes", {0, 1}}}})},
                    {"nodes", Json::array({{{"name", "Duplicate"}}, {{"name", "Duplicate"}}})}};
        save(path, source);
        EngineContext engine;
        Scene scene(engine.world());
        SceneDocument document(scene);
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
        const auto baseline = read_json(root / "forge.assets.json");
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
        require(!editor.dirty() && editor.selected_asset() == owner,
                "Reviewed model reimport lost root identity");
        const auto before = scene.document();
        asset_detail::ModelPlacementOptions options;
        options.name = "Placed signed model";
        options.transform.scale = {-1, 0, 1};
        const auto placed = editor.place(document, options);
        require(scene.entity_count() == 3 && context.selection.entity() == placed.str(),
                "Model editor did not place/select ordinary entities");
        const auto after = scene.document();
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
