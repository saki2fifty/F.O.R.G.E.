#include "editor/code_automation.hpp"
#include <iostream>
#include <thread>

namespace {
void check(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc != 2)
            return 2;
        const auto root = std::filesystem::u8path(argv[1]) / forge::AssetId::generate().str();
        std::filesystem::create_directories(root.parent_path());
        forge::SceneDocument::create_project(root, "C++ source owner");
        forge::EngineContext engine;
        forge::Scene scene(engine.world());
        std::vector<std::string> recent;
        forge::EditorFiles files(scene, nullptr, recent);
        files.start(root);
        forge::atomic_write(root / "Native/First.cpp", "int value = 1;\n");
        forge::atomic_write(root / "Native/Second.hpp", "#pragma once\n");
        ImGui::CreateContext();
        auto& platform = ImGui::GetPlatformIO();
        int clipboard_reads = 0, clipboard_writes = 0;
        struct Clipboard {
            int* reads;
            int* writes;
        } clipboard{&clipboard_reads, &clipboard_writes};
        platform.Platform_ClipboardUserData = &clipboard;
        platform.Platform_GetClipboardTextFn = [](ImGuiContext* context) {
            ++*static_cast<Clipboard*>(context->PlatformIO.Platform_ClipboardUserData)->reads;
            return "Unchanged operating-system clipboard";
        };
        platform.Platform_SetClipboardTextFn = [](ImGuiContext* context, const char*) {
            ++*static_cast<Clipboard*>(context->PlatformIO.Platform_ClipboardUserData)->writes;
        };
        const auto original_reader = platform.Platform_GetClipboardTextFn;
        forge::ui::CppSourceEditor editor;
        editor.open(files.document, "Native/First.cpp");
        const auto original = editor.active_text();
        const std::string replacement = "// Unicode: \xc3\xa9\nint value = 2;\n";
        editor.replace_text(replacement);
        check(editor.active_text() == replacement && editor.can_undo(),
              "Source replacement did not use the widget history");
        check(clipboard_reads == 0 && clipboard_writes == 0 &&
                  platform.Platform_GetClipboardTextFn == original_reader &&
                  platform.Platform_ClipboardUserData == &clipboard,
              "Source replacement touched or failed to restore the OS clipboard callbacks");
        editor.undo(false);
        check(editor.active_text() == original, "One Undo did not restore the full source");
        editor.undo(true);
        check(editor.active_text() == replacement, "One Redo did not restore the replacement");
        for (const auto& [input, expected] :
             std::vector<std::pair<std::string, std::string>>{{"", "\n"},
                                                              {"one line", "one line\n"},
                                                              {"two\n\n", "two\n\n"},
                                                              {"a\r\nb\r\n", "a\nb\n"}}) {
            const auto before = editor.active_text();
            editor.replace_text(input);
            check(editor.active_text() == expected, "Widget source normalization changed");
            editor.undo(false);
            check(editor.active_text() == before, "Replacement did not keep one native Undo step");
        }
        editor.build_on_save = true;
        editor.save(files.document, false);
        check(!editor.build_pending && !editor.dirty(),
              "An ungranted source save queued execution");
        editor.replace_text("int value = 3;\n");
        forge::atomic_write(root / "Native/First.cpp", "int external = 4;\n");
        bool rejected = false;
        try {
            editor.save(files.document, false);
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && editor.dirty() && editor.active_text() == "int value = 3;\n",
              "External conflict did not retain the source draft");
        editor.open(files.document, "Native/Second.hpp");
#ifndef _WIN32
        const auto scene_before = forge::asset_storage::read(files.document.path());
        std::filesystem::create_symlink(files.document.path(), root / "Native/Alias.cpp");
        bool alias_rejected = false;
        try {
            editor.open(files.document, "Native/Alias.cpp");
        } catch (const std::exception&) {
            alias_rejected = true;
        }
        check(alias_rejected && forge::asset_storage::read(files.document.path()) == scene_before,
              "A contained C++ alias crossed into another source owner");
#endif
        forge::SdkBuild build(root);
        forge::AuthoredComponents authored;
        forge::PrefabEditor prefab;
        forge::CodeAutomationProviders providers;
        providers.build = [&]() -> forge::SdkBuild& { return build; };
        providers.sdk = [&] { return root / "UnusedSdk"; };
        providers.compiler_tools = [] {
            return std::pair<std::string, std::string>{"cmake", "ninja"};
        };
        forge::ui::AutomationPermissions permissions;
        permissions.code = true;
        auto methods = forge::CodeAutomation::methods(scene, files, editor, authored, prefab,
                                                      providers, permissions);
        const auto invoke = [&](const std::string& name, const forge::Json& args) {
            const auto found =
                std::find_if(methods.begin(), methods.end(),
                             [&](const auto& method) { return method.name == name; });
            if (found == methods.end())
                throw std::runtime_error("Missing source method " + name);
            return found->invoke(args);
        };
        const auto sources = invoke("code.sources", {{"offset", 1}, {"limit", 1}});
        check(sources.at("total") == 2 &&
                  sources.at("sources") == forge::Json::array({"Native/Second.hpp"}),
              "MCP source discovery diverged from the ordered project source list");
        const auto scene_before_search = scene.document();
        const auto revision_before_search = editor.source_revision;
        const auto settle_search = [&] {
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            for (;;) {
                auto status = invoke("code.search.status", {{"offset", 0}, {"limit", 1}});
                if (!status.at("busy").get<bool>())
                    return status;
                if (std::chrono::steady_clock::now() >= end)
                    throw std::runtime_error("Source search did not settle");
                std::this_thread::yield();
            }
        };
        invoke("code.search.start", {{"query", "value"}, {"whole_word", true}});
        auto searched = settle_search();
        check(searched.at("complete") && searched.at("total") == 1 &&
                  searched.at("matches").at(0).at("excerpt") == "int value = 3;" &&
                  searched.at("matches").at(0).at("open") == true &&
                  searched.at("matches").at(0).at("column") == 5 &&
                  editor.active_source() == "Native/Second.hpp" &&
                  editor.source_revision == revision_before_search &&
                  scene.document() == scene_before_search,
              "Search did not use the shared dirty draft or changed document/history/focus");
        const auto match = searched.at("matches").at(0);
        invoke("code.open", {{"path", match.at("path")},
                             {"state", match.at("state")},
                             {"line", match.at("line")},
                             {"column", match.at("column")}});
        check(editor.active_source() == "Native/First.cpp" && editor.dirty() && editor.can_undo(),
              "Search navigation replaced an existing source draft/history");
        editor.open_search_result(files.document, 0);
        editor.replace_text("int value = 5;\n");
        bool stale_search = false;
        try {
            invoke("code.open", {{"path", match.at("path")}, {"state", match.at("state")}});
        } catch (const forge::AuthoringError& error) {
            stale_search = error.code == "stale_source";
        }
        check(stale_search, "Search navigation accepted a changed source digest");
        bool stale_ui = false;
        try {
            editor.open_search_result(files.document, 0);
        } catch (const std::exception&) {
            stale_ui = true;
        }
        check(stale_ui && editor.active_text() == "int value = 5;\n",
              "Stale interactive result moved or replaced source text");
        bool wrong_job = false;
        try {
            invoke("code.search.cancel", {{"operation", forge::AssetId::generate().str()}});
        } catch (const forge::AuthoringError& error) {
            wrong_job = error.code == "wrong_job";
        }
        check(wrong_job, "Source search cancellation accepted another operation");
        const auto started_search = invoke("code.search.start", {{"query", "value"}});
        invoke("code.search.cancel", {{"operation", started_search.at("operation")}});
        searched = settle_search();
        check(!started_search.at("busy").get<bool>() ||
                  (searched.at("cancelled") && !searched.at("complete").get<bool>()),
              "Cancelled source search claimed completeness");
        auto readonly_permissions = permissions;
        readonly_permissions.code = false;
        const auto readonly_methods = forge::CodeAutomation::methods(
            scene, files, editor, authored, prefab, providers, readonly_permissions);
        for (const auto* name : {"code.search.start", "code.search.status", "code.search.cancel"})
            check(std::any_of(readonly_methods.begin(), readonly_methods.end(),
                              [&](const auto& method) {
                                  return method.name == name && !method.mutation &&
                                         method.capability == "code.read";
                              }),
                  "Read-only source search is missing or grants source mutation");
        auto state = invoke("code.read", {{"path", "Native/First.cpp"}});
        invoke("code.close", {{"path", "Native/First.cpp"},
                              {"session", state.at("session")},
                              {"state", state.at("state")},
                              {"discard", true}});
        check(invoke("code.read", {{"path", "Native/Second.hpp"}}).at("open").get<bool>(),
              "Closing the first tab damaged another tab");
        check(clipboard_reads == 0 && clipboard_writes == 0,
              "Source operations used the OS clipboard");
        std::cout << "Actual C++ source/widget history, clipboard isolation, save guards and tab "
                     "close passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
