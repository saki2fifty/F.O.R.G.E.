#include "automation.hpp"
#include "code_automation.hpp"
#include "mcp_test_marker.hpp"
#include <chrono>
#include <forge/native_sdk_identity.h>
#include <iostream>
#include <thread>

// Actual editor owners without a graphics device. No alternate build service.
int main(int argc, char** argv) {
    try {
        if (argc != 6 && argc != 8)
            return 2;
        const auto root = std::filesystem::u8path(argv[1]);
        const auto sdk = std::filesystem::u8path(argv[2]);
        const auto mode = std::string(argv[5]);
        forge::SceneDocument::create_project(root, "MCP C++ owner");
        forge::EngineContext engine;
        forge::Scene scene(engine.world());
        std::vector<std::string> recent;
        forge::EditorFiles files(scene, nullptr, recent);
        files.start(root);
        // Native inspection fixture: install genuine built probe modules through
        // the project owner. This models external SDK builds on Linux, whose
        // managed deployment kits deliberately remain Windows-only.
        const auto install_probe = [&](const char* module) {
            const auto input = std::filesystem::absolute(module);
            const auto library = input.filename();
            std::filesystem::copy_file(input, root / library,
                                       std::filesystem::copy_options::overwrite_existing);
            const auto before = files.document.settings().document();
            auto after = before;
            after["modules"] =
                forge::Json::array({{{"id", "project.sdk_probe"},
                                     {"implementation", "1"},
                                     {"sdk", "experimental-1"},
                                     {"fingerprint", FORGE_NATIVE_SDK_FINGERPRINT},
                                     {"library", forge::path_utf8(library)},
                                     {"dependencies", {"forge.input", "forge.transforms"}}}});
            files.document.save_settings(after, &before);
        };
        if (argc == 8)
            install_probe(argv[6]);
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        forge::SdkBuild build(root);
        forge::ui::CppSourceEditor sources;
        forge::AuthoredComponents authored;
        authored.project_changed(scene, files.document);
        forge::PrefabEditor prefab;
        forge::CodeAutomationProviders providers;
        providers.build = [&]() -> forge::SdkBuild& { return build; };
        providers.sdk = [&] { return sdk; };
        providers.compiler_tools = [&] {
            return std::pair<std::string, std::string>{argv[3], argv[4]};
        };
        forge::ui::AutomationWorkspace workspace;
        workspace.workflow_methods = [&](const auto& permissions) {
            return forge::CodeAutomation::methods(scene, files, sources, authored, prefab,
                                                  providers, permissions);
        };
        forge::ui::AutomationPermissions permissions;
        permissions.code = mode != "read";
        permissions.execute = mode == "execute" || mode == "inspect";
        workspace.start(scene, files, mode != "read", mode != "read", permissions);
        std::cout << workspace.connection().connection().dump() << std::endl;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(240);
        while (workspace.active() && std::chrono::steady_clock::now() < end) {
            files.changed = false;
            if (argc == 8 && forge::test::consume_marker(root / "install-v2"))
                install_probe(argv[7]);
            build.pump(files.document);
            authored.poll(scene, files.document);
            workspace.pump(files.document,
                           build.busy() || authored.busy() ? "Native task pending" : "");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
