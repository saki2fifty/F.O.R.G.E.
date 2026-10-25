#include "cache_tools.hpp"
#include "files.hpp"
#include <forge/derived_cache.hpp>
#include <imgui_internal.h>
#include <iostream>
#include <thread>

using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need cache UI scratch directory");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        struct Cleanup {
            std::filesystem::path root;
            ~Cleanup() {
                std::error_code ignored;
                std::filesystem::remove_all(root, ignored);
            }
        } cleanup{root};
        std::filesystem::create_directories(root);
        const auto first = root / "First", second = root / "Second";
        SceneDocument::create_project(first, "First");
        SceneDocument::create_project(second, "Second");
        AssetBuildInput input;
        input.source_digest = std::string(64, 'a');
        input.importer = "fixture.cache";
        input.importer_revision = std::string(64, 'b');
        input.output_format = "fixture.bytes";
        input.platform = "portable";
        input.backend = "none";
        input.profile = "cpu";
        DerivedDataCache(first).publish(input, {{"data.bin", {std::byte{42}}}},
                                        [](const CachedArtifact&) {});
        const auto first_catalog = AssetCatalog::open_project(first).document();
        const auto second_catalog = AssetCatalog::open_project(second).document();
        EngineContext engine;
        Scene scene(engine.world());
        std::vector<std::string> recent;
        EditorFiles files(scene, nullptr, recent);
        files.start(first);
        ContentImports imports;
        CacheTools tools;
        files.external_busy = [&] { return tools.busy(); };
        ImGui::CreateContext();
        struct UiCleanup {
            ~UiCleanup() {
                test::observe_ui = false;
                ImGui::DestroyContext();
            }
        } ui_cleanup;
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1000, 700};
        io.DeltaTime = 1.f / 60;
        io.ConfigInputTrickleEventQueue = false;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        ui::style(1);
        ui::EditorUiContext context;
        ui::ContextScope scope(context);
        test::observe_ui = true;
        auto frame = [&](bool poll = true, bool locked = false) {
            if (poll)
                tools.poll(files.document, imports);
            test::ui_targets.clear();
            ImGui::NewFrame();
            tools.draw(locked);
            ImGui::Render();
        };
        auto click = [&](const char* target) {
            const auto box = test::ui_targets.at(target);
            check(box.enabled, "Cache action disabled");
            io.AddMousePosEvent((box.minimum.x + box.maximum.x) * .5f,
                                (box.minimum.y + box.maximum.y) * .5f);
            frame(false);
            io.AddMouseButtonEvent(0, true);
            frame(false);
            io.AddMouseButtonEvent(0, false);
            frame(false);
        };
        auto settle = [&] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (tools.busy()) {
                frame();
                check(std::chrono::steady_clock::now() < deadline, "Cache job did not settle");
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            frame();
            check(test::ui_targets.contains("cache:complete"), "Actual cache result missing");
        };
        tools.open();
        frame();
        frame();
        click("cache:statistics");
        check(tools.busy() && files.busy(), "Queued cache request did not hold project guard");
        bool rejected = false;
        try {
            files.automation_project({EditorFiles::Command::OpenProject, second, {}}, false);
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && files.document.project() == first,
              "Project switch bypassed queued cache operation");
        settle();
        frame();
        check(test::ui_targets.contains("cache:complete"),
              "Idle polling discarded same-project result");
        click("cache:close");
        files.automation_project({EditorFiles::Command::OpenProject, second, {}}, false);
        frame(); // Poll while the cache window is closed, as the editor does.
        tools.open();
        frame();
        check(!test::ui_targets.contains("cache:complete"),
              "Previous project's cache result remained in the new project");
        click("cache:statistics");
        click("button:Cancel operation");
        frame();
        check(!tools.busy() && !test::ui_targets.contains("cache:complete"),
              "Cancelled queued cache work published a result");
        click("cache:statistics");
        settle();
        frame();
        check(test::ui_targets.contains("cache:complete"), "New-project result was discarded");
        frame(true, true);
        check(!test::ui_targets.at("cache:statistics").enabled,
              "Locked editor allowed cache maintenance");
        check(DerivedDataCache(first).statistics().entries == 1 &&
                  DerivedDataCache(second).statistics().entries == 0 &&
                  AssetCatalog::open_project(first).document() == first_catalog &&
                  AssetCatalog::open_project(second).document() == second_catalog &&
                  !scene.can_undo(),
              "Read-only cache workflow changed cache/catalog/history");
        std::cout << "Actual cache UI, project guard and completed-result boundary passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
