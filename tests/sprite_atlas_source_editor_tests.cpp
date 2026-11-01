#include "editor/source_workflow_automation.hpp"
#include "editor/sprite_atlas_editor.hpp"
#include "imgui_software_capture.hpp"
#include "sprite_atlas_pipeline_fixture.hpp"
#include <imgui_internal.h>
#include <iostream>
#include <thread>

using namespace forge;
using namespace std::chrono_literals;
namespace {
void require(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid atlas document operation accepted");
}
void capture(const std::filesystem::path& gallery, const std::filesystem::path& output) {
    const auto root = std::filesystem::absolute(output) / "Project";
    require(!std::filesystem::exists(root), "Use a fresh atlas capture folder");
    std::filesystem::create_directories(root.parent_path());
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    test::ImGuiSoftwareCapture pixels(920, 850);
    ui::style(1);
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root, true);
    SpriteAtlasEditor editor("unused-capture-worker");
    editor.open(project, "Assets/Sprites/SourceAware.atlas.json");
    require(editor.document(), "Actual atlas source did not open for capture");
    for (unsigned frame = 0; frame < 4; ++frame) {
        pixels.new_frame();
        ImGui::SetNextWindowPos({10, 10});
        ImGui::SetNextWindowSize({900, 830});
        editor.draw(project, false);
        pixels.render();
    }
    pixels.save(std::filesystem::absolute(output) / "sprite-atlas-source-editor.png");
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string_view(argv[3]) == "--capture") {
            capture(std::filesystem::absolute(argv[1]), std::filesystem::absolute(argv[2]));
            return 0;
        }
        require(argc == 3, "Need worker and scratch folder");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        EngineContext engine;
        Scene scene(engine.world());
        SceneDocument project(scene);
        project.open_project(root, true);
        const auto scene_before = scene.document();
        const auto worker = std::filesystem::absolute(argv[1]);
        const auto red = AssetId::generate(), green = AssetId::generate();
        AssetImportService images(project.writer_guard(), texture_import_registry(worker),
                                  desktop_texture_target());
        test_sprite_atlas_io::image(root / "Assets/Red.tga", 3, 2, {211, 12, 23});
        test_sprite_atlas_io::image(root / "Assets/Green.tga", 2, 4, {8, 199, 34});
        require(test_sprite_atlas_io::publish(images, "Assets/Red.tga", red).published,
                "Input image publication failed");
        require(test_sprite_atlas_io::publish(images, "Assets/Green.tga", green).published,
                "Input image publication failed");
        SpriteAtlasEditor editor(worker);
        ui::DocumentWorkspace workspace;
        workspace.add({"sprite_atlas", "Sprite atlas", "Sprite atlas###SpriteAtlas", true});
        workspace.bind_source("sprite_atlas", AssetDocumentAutomation::bind(editor, project));
        ui::AutomationPermissions permissions;
        const auto read_methods = source_document_automation_methods(workspace, permissions);
        require(std::none_of(read_methods.begin(), read_methods.end(),
                             [](const auto& m) { return m.mutation; }),
                "Read-only automation exposed atlas mutations");
        permissions.assets = true;
        const auto methods = source_document_automation_methods(workspace, permissions);
        const auto invoke = [&](std::string_view name, Json args) {
            args["editor"] = "sprite_atlas";
            const auto found = std::find_if(methods.begin(), methods.end(),
                                            [&](const auto& m) { return m.name == name; });
            require(found != methods.end(), "Atlas source workflow is not advertised");
            return found->invoke(args);
        };
        rejects([&] { invoke("asset.document.template", Json::object()); });
        const Json textures = {red, green};
        rejects([&] {
            invoke("asset.document.template", {{"textures", textures}, {"tile_set", red}});
        });
        const auto shape = invoke("asset.document.template", {{"textures", textures}});
        require(SpriteAtlasRecipe{shape}.inputs().size() == 2, "Atlas source template is invalid");
        const auto created =
            invoke("asset.document.create",
                   {{"path", "Assets/Characters.atlas.json"}, {"textures", textures}});
        const auto id = created.at("asset").get<AssetId>();
        require(editor.document() && editor.document()->source().asset() == id &&
                    id != SpriteAtlasRecipe{shape}.asset(),
                "Atlas creation failed or reused a preview identity");
        const auto token = [&] {
            return Json{{"document_session", editor.document()->session()},
                        {"document_revision", editor.document()->revision()}};
        };
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1200, 1000};
        io.DeltaTime = 1.f / 60;
        io.ConfigInputTrickleEventQueue = false;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        ui::style(1);
        test::observe_ui = true;
        ui::EditorUiContext context;
        ui::ContextScope scope(context);
        bool locked = false;
        std::string message;
        const auto frame = [&] {
            test::ui_targets.clear();
            editor.poll(project, message);
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({1100, 960}, ImGuiCond_Always);
            editor.draw(project, locked);
            ImGui::Render();
        };
        const auto click = [&](const std::string& name) {
            frame();
            require(test::ui_targets.contains(name), "Missing atlas control: " + name);
            const auto box = test::ui_targets.at(name);
            require(box.enabled && box.minimum.y >= box.clip_minimum.y &&
                        box.maximum.y <= box.clip_maximum.y,
                    "Atlas control is disabled or unreachable: " + name);
            io.AddMousePosEvent((box.minimum.x + box.maximum.x) / 2,
                                (box.minimum.y + box.maximum.y) / 2);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
        };
        frame();
        frame();
        const auto stale = token();
        click("sprite_atlas:trim");
        require(editor.document()->source().settings().trim_transparent,
                "UI did not edit packing source");
        auto invalid = stale;
        invalid["patch"] = {{{"op", "replace"}, {"path", "/settings/extrude"}, {"value", false}}};
        rejects([&] { invoke("asset.document.patch", invalid); });
        invoke("asset.document.undo", token());
        require(!editor.document()->source().settings().trim_transparent,
                "Automation did not Undo the UI edit");
        invoke("asset.document.redo", token());
        require(editor.document()->source().settings().trim_transparent,
                "Automation did not Redo the UI edit");
        auto patch = token();
        patch["patch"] = {
            {{"op", "remove"}, {"path", "/frames/1"}},
            {{"op", "add"}, {"path", "/frames/-"}, {"value", {{"id", 2}, {"texture", green}}}},
            {{"op", "replace"}, {"path", "/next_id"}, {"value", 3}}};
        invoke("asset.document.patch", patch);
        invoke("asset.document.undo", token());
        require(editor.document()->source().inputs()[1].id == 1 &&
                    editor.document()->source().next_id() == 3,
                "Source API Undo reused an allocated atlas key");
        auto reused = token();
        reused["patch"] = patch["patch"];
        rejects([&] { invoke("asset.document.patch", reused); });
        auto wrong_session = token();
        wrong_session["document_session"] = 0;
        rejects([&] { invoke("asset.document.undo", wrong_session); });
        invoke("asset.document.save", token());
        frame();
        const auto deadline = std::chrono::steady_clock::now() + 20s;
        while (editor.pending()) {
            frame();
            require(std::chrono::steady_clock::now() < deadline,
                    "Atlas editor publication timed out");
            std::this_thread::sleep_for(1ms);
        }
        require(!editor.dirty() && editor.diagnostic().empty() && editor.take_catalog(),
                "Source API Save did not use the editor's actual successful publication path: " +
                    editor.diagnostic());
        require(AssetCatalog::open_project(root)
                        .records()
                        .at(id)
                        .metadata.at("forge.import")
                        .at("output_version") == 2,
                "Atlas editor published a legacy Texture profile");
        locked = true;
        frame();
        require(!test::ui_targets.at("sprite_atlas:trim").enabled &&
                    !test::ui_targets.at("sprite_atlas:save").enabled,
                "Atlas editor remained editable under the existing busy/Play lock");
        locked = false;
        click("sprite_atlas:trim");
        rejects([&] { invoke("asset.document.close", token()); });
        auto close = token();
        close["discard"] = true;
        invoke("asset.document.close", close);
        require(!editor.is_open() && scene.document() == scene_before,
                "Atlas source workflow mutated the scene or failed explicit draft discard");
        test::observe_ui = false;
        test::ui_targets.clear();
        ImGui::DestroyContext();
        std::cout << "Atlas editor UI, source API discovery/grants/session/revision/history and "
                     "real worker Save passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
