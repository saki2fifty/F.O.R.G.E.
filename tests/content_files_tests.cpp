#include "asset_storage.hpp"
#include "content_browser_tests.hpp"
#include "content_files.hpp"
#include <iostream>
using namespace forge;
int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Need Content test scratch root");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code e;
                std::filesystem::remove_all(path, e);
            }
        } cleanup{root};
        EngineContext engine;
        Scene scene(engine.world());
        SceneDocument project(scene);
        project.open_project(root, true);
        auto source = empty_scene();
        const AssetRecord asset{
            source.at("asset_id").get<AssetId>(), "scene", "Assets/test.scene.json", 3, {}};
        asset_storage::replace(root / asset.source, source.dump(2));
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1440, 900};
        io.DeltaTime = 1.f / 60;
        unsigned char* pixels;
        int w, h;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
        ui::EditorUiContext context;
        ui::ContextScope scope(context);
        test_content_browser(root);
        ContentImports imports;
        ContentFiles dialog;
        std::string message;
        const auto check = [](bool ok, const char* why) {
            if (!ok)
                throw std::runtime_error(why);
        };
        auto frame = [&](const char* activate = nullptr) {
            dialog.poll(project, scene, imports, message);
            ImGui::NewFrame();
            if (activate) {
                auto* window = ImGui::FindWindowByName("Asset source files");
                check(window != nullptr, "File dialog did not create its modal");
                auto& state = *ImGui::GetCurrentContext();
                state.NavActivateId = state.NavActivateDownId = window->GetID(activate);
                state.NavInputSource = ImGuiInputSource_Keyboard;
            }
            dialog.draw(imports);
            ImGui::Render();
        };
        dialog.unavailable = [](const auto&, auto) { return "A draft is still open"; };
        check(!dialog.begin(asset, AssetFileAction::Delete) && !dialog.busy(),
              "File action ignored its authoring guard");
        dialog.unavailable = {};
        for (float scale : {1.f, 2.f}) {
            ui::style(scale);
            io.DisplaySize = scale == 1 ? ImVec2{1440, 900} : ImVec2{960, 640};
            check(dialog.begin(asset, AssetFileAction::Delete), "File dialog did not open");
            frame();
            frame();
            frame("Prepare review");
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            do {
                frame();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                check(std::chrono::steady_clock::now() < deadline, "Content review stalled");
            } while (!dialog.operation() ||
                     dialog.operation()->state() == AssetFileState::Preparing);
            if (dialog.operation()->state() != AssetFileState::Review)
                throw std::runtime_error(dialog.operation()->diagnostic());
            frame("Confirm file changes");
            check(dialog.operation()->state() == AssetFileState::Review &&
                      std::filesystem::exists(root / asset.source),
                  "Delete bypassed explicit acknowledgement");
            frame("Cancel");
            frame();
            check(!dialog.busy() && imports.quiescent() &&
                      std::filesystem::exists(root / asset.source),
                  "Cancelling review changed source or retained UI lock");
        }
        ImGui::DestroyContext();
        std::cout << "Content file review/guard/cancel and scaled modal tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
