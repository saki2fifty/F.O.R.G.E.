#include "editor/camera.hpp"
#include "editor/navigation_automation.hpp"
#include <imgui_internal.h>
#include <iostream>
#include <thread>

using namespace forge;
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need navigation lifecycle scratch root and actual worker");
        const auto root = std::filesystem::absolute(argv[1]);
        const auto first = root / "First", second = root / "Second";
        SceneDocument::create_project(first, "First");
        SceneDocument::create_project(second, "Second");
        EngineContext engine;
        Scene scene(engine.world());
        std::vector<std::string> recent;
        EditorFiles files(scene, nullptr, recent);
        files.start(first);
        const auto floor =
            authoring_command(scene, "entity.create", {{"kind", 0}, {"name", "Floor"}})
                .at("selected")
                .get<std::string>();
        scene.entity(floor).set<LocalScale>({20, 1, 20});
        scene.entity(floor).set<LocalTranslation>({0, -.5, 0});
        scene.entity(floor).set<NavigationSurface>({true});
        files.document.save();
        // Copying a project preserves its scene UUID. The second project has no
        // baked navigation, despite containing the same authored Scene identity.
        const auto scene_path = files.document.path().lexically_relative(first);
        std::filesystem::copy_file(first / scene_path, second / scene_path,
                                   std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file(first / "forge.project.json", second / "forge.project.json",
                                   std::filesystem::copy_options::overwrite_existing);
        const auto saved_scene = scene.document();
        NavigationTools navigation(engine.world(), std::filesystem::absolute(argv[2]));
        navigation.build(scene, files.document, {});
        std::string message;
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (navigation.pending()) {
            navigation.poll(scene, files.document, false, message);
            check(std::chrono::steady_clock::now() < until, "Navigation worker did not settle");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const auto baked = NavigationAutomation::state(navigation, scene, files);
        check(baked.at("error") == "" && !baked.at("asset").is_null(),
              "Actual first-project navigation build failed");
        const auto saved_catalog = AssetCatalog::open_project(first).document();

        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1200, 900};
        io.DeltaTime = 1.f / 60;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        EditorCamera camera;
        camera.target = {0, 0, 0};
        camera.pitch = -.7f;
        camera.distance = 30;
        const auto overlay_vertices = [&](bool load_saved = false) {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({1200, 900}, ImGuiCond_Always);
            ImGui::Begin("Navigation overlay boundary");
            if (load_saved) {
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                navigation.content(scene, files.document, false, message,
                                   std::make_shared<const AssetCatalog>(
                                       AssetCatalog::open_project(files.document.project())));
            }
            auto* draw = ImGui::GetWindowDrawList();
            const auto before = draw->VtxBuffer.Size;
            navigation.draw(scene.effective_document(), camera, {0, 0}, {1200, 900});
            const auto added = draw->VtxBuffer.Size - before;
            ImGui::End();
            ImGui::Render();
            return added;
        };
        const auto original_vertices = overlay_vertices();
        check(original_vertices > 0, "Baked navigation did not draw in its original project");
        navigation.poll(scene, files.document, false, message);
        check(overlay_vertices() == original_vertices,
              "Idle same-project update discarded the usable navigation overlay");
        files.automation_project({EditorFiles::Command::OpenProject, second, {}}, false);
        check(scene.document() == saved_scene, "Copied project did not preserve the Scene");
        navigation.poll(scene, files.document, false, message);
        check(!NavigationAutomation::state(navigation, scene, files).at("started").get<bool>(),
              "Navigation adapter exposed another project's bake");
        check(overlay_vertices() == 0,
              "Old navigation overlay remained in a different project with the same Scene UUID");
        files.changed = false; // Consume the file-change event as the editor frame does.
        files.automation_project({EditorFiles::Command::OpenProject, first, {}}, false);
        navigation.poll(scene, files.document, false, message);
        check(overlay_vertices() == 0, "Returning to a project resurrected a stale cache");
        check(overlay_vertices(true) == original_vertices && !navigation.pending(),
              "Opening Navigation did not reload the saved overlay without rebuilding");
        check(NavigationAutomation::state(navigation, scene, files).at("asset") ==
                  baked.at("asset"),
              "Reopening Navigation lost the saved asset selection");
        check(scene.document() == saved_scene && !scene.can_undo() &&
                  AssetCatalog::open_project(first).document() == saved_catalog &&
                  AssetCatalog::open_project(second).records().empty(),
              "Navigation cache changes wrote authored state or either project catalog");
        files.changed = false;
        files.automation_switch({EditorFiles::Command::NewScene, {}, {}}, false);
        navigation.poll(scene, files.document, false, message);
        check(overlay_vertices() == 0 &&
                  !NavigationAutomation::state(navigation, scene, files).at("started").get<bool>(),
              "Scene switching retained the previous scene's navigation cache");
        ImGui::DestroyContext();
        std::cout << "Actual baked navigation overlay respects project/scene boundaries and saved "
                     "reload\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
