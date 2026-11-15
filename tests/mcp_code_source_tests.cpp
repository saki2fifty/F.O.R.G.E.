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
        if (argc != 3)
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
        check(SDL_Init(0), "Cannot initialize native process support");
        struct Quit {
            ~Quit() { SDL_Quit(); }
        } quit;
        ImGui::CreateContext();
        struct DestroyGui {
            ~DestroyGui() { ImGui::DestroyContext(); }
        } destroy_gui;
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
        std::filesystem::remove(root / "Native/Alias.cpp");
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
        for (const auto* name : {"code.analysis.status", "code.analysis.log"})
            check(std::any_of(readonly_methods.begin(), readonly_methods.end(),
                              [&](const auto& method) {
                                  return method.name == name && !method.mutation &&
                                         method.capability == "code.read";
                              }),
                  "Analysis reading acquired execution");
        for (const auto* name :
             {"code.analysis.start", "code.analysis.cancel", "code.analysis.use"})
            check(std::none_of(readonly_methods.begin(), readonly_methods.end(),
                               [&](const auto& method) { return method.name == name; }),
                  "Unpermitted analysis operation was exposed");
        bool missing_context = false;
        try {
            (void)build.semantic_context();
        } catch (const std::exception&) {
            missing_context = true;
        }
        check(missing_context, "Unconfigured build invented a compiler context");
        const auto context = root / ".forge/analysis-fixture";
        forge::atomic_write(context / "compile_commands.json", "[]");
        auto* environment = SDL_CreateEnvironment(true);
        check(environment != nullptr, "Cannot create fixture tool environment");
        struct FreeEnvironment {
            SDL_Environment* value;
            ~FreeEnvironment() { SDL_DestroyEnvironment(value); }
        } free_environment{environment};
        std::string generation = "fixture-context-1";
        editor.semantic_context = [&] {
            return forge::CppSemanticContext{root, context, generation, environment};
        };
        editor.semantic_tool = [&] { return std::string(argv[2]); };
        permissions.execute = true;
        methods = forge::CodeAutomation::methods(scene, files, editor, authored, prefab, providers,
                                                 permissions);
        editor.open(files.document, "Native/First.cpp");
        const std::string draft = "// café\nstruct Spinner { float degrees_per_second; };\n"
                                  "void tick() { Spinner s; s.de }\n";
        editor.replace_text(draft);
        const auto disk_before_analysis = forge::ui::read_cpp_source(root / "Native/First.cpp");
        const auto start_analysis = [&](const std::string& kind = "completion") {
            const auto observed = invoke("code.read", {{"path", "Native/First.cpp"}});
            return invoke("code.analysis.start", {{"path", "Native/First.cpp"},
                                                  {"session", observed.at("session")},
                                                  {"state", observed.at("state")},
                                                  {"kind", kind},
                                                  {"offset", draft.rfind("s.de") + 4}});
        };
        const auto settle_analysis = [&] {
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            for (;;) {
                auto status = invoke("code.analysis.status", {{"limit", 1}});
                if (!status.at("busy").get<bool>()) {
                    // Forward child diagnostics even for expected query failures.
                    std::cout << invoke("code.analysis.log", {{"limit", 262144}})
                                     .at("text")
                                     .get<std::string>();
                    return status;
                }
                if (std::chrono::steady_clock::now() >= end)
                    throw std::runtime_error("C++ analysis did not settle");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        };
        const auto use_analysis = [&](const forge::Json& status) {
            return invoke("code.analysis.use",
                          {{"operation", status.at("operation")}, {"index", 0}});
        };
        start_analysis();
        auto analysis = settle_analysis();
        check(analysis.at("complete") && analysis.at("items").size() == 1 &&
                  analysis.at("items").at(0).at("available") && !analysis.at("stale").get<bool>(),
              "Supervised analysis did not produce the draft completion: " + analysis.dump());
        use_analysis(analysis);
        auto completed = draft;
        completed.replace(draft.rfind("s.de") + 2, 2, "degrees_per_second");
        check(editor.active_text() == completed, "Completion replaced the wrong source range");
        editor.undo(false);
        check(editor.active_text() == draft, "Completion did not share one source Undo record");
        editor.undo(true);
        check(editor.active_text() == completed, "Completion Redo lost source bytes");
        editor.undo(false);
        start_analysis("declaration");
        analysis = settle_analysis();
        use_analysis(analysis);
        check(invoke("code.read", {{"path", "Native/First.cpp"}}).at("cursor").at("line") == 2 &&
                  editor.active_text() == draft,
              "Declaration lookup changed source instead of navigating");
        invoke("code.analysis.cancel", {{"operation", analysis.at("operation")}});
        auto cancelled_result = invoke("code.analysis.status", {{"limit", 1}});
        check(cancelled_result.at("cancelled") && !cancelled_result.at("complete").get<bool>(),
              "Cancellation retained an applicable finished result");
        const auto cancelled_log = invoke("code.analysis.log", {{"limit", 4096}});
        invoke("code.analysis.cancel", {{"operation", analysis.at("operation")}});
        check(invoke("code.analysis.log", {{"limit", 4096}}) == cancelled_log,
              "Repeated cancellation changed the completed cancellation receipt");
        start_analysis();
        analysis = settle_analysis();
        editor.open(files.document, "Native/Second.hpp");
        editor.replace_text("// changed dependency\n");
        bool stale_analysis = false;
        try {
            use_analysis(analysis);
        } catch (const std::exception&) {
            stale_analysis = true;
        }
        check(stale_analysis, "Analysis accepted a changed open dependency");
        editor.open(files.document, "Native/First.cpp");
        start_analysis();
        analysis = settle_analysis();
        generation = "fixture-context-2";
        bool stale_context = false;
        try {
            use_analysis(analysis);
        } catch (const std::exception&) {
            stale_context = true;
        }
        check(stale_context, "Analysis accepted a retired compiler generation");
        for (const auto* mode : {"unsupported", "external", "wrong-version", "bad-frame",
                                 "invalid-range", "exit-failure"}) {
            SDL_SetEnvironmentVariable(environment, "FORGE_SEMANTIC_FIXTURE_CASE", mode, true);
            start_analysis(std::string_view(mode) == "external" ? "declaration" : "completion");
            analysis = settle_analysis();
            if (std::string_view(mode) == "unsupported" || std::string_view(mode) == "external")
                check(analysis.at("complete") &&
                          !analysis.at("items").at(0).at("available").get<bool>(),
                      "Untrusted provider command or external declaration became available");
            else
                check(!analysis.at("complete").get<bool>() &&
                          !analysis.at("error").get<std::string>().empty(),
                      "Malformed or failed provider was accepted: " + analysis.dump());
        }
        SDL_UnsetEnvironmentVariable(environment, "FORGE_SEMANTIC_FIXTURE_CASE");
        const auto cancelled = start_analysis();
        invoke("code.analysis.cancel", {{"operation", cancelled.at("operation")}});
        analysis = settle_analysis();
        check(analysis.at("cancelled") && !analysis.at("complete").get<bool>(),
              "Cancelled analysis claimed a completed result");
        check(forge::ui::read_cpp_source(root / "Native/First.cpp") == disk_before_analysis &&
                  editor.active_text() == draft && scene.document() == scene_before_search,
              "Analysis changed saved source or scene");
        check(!invoke("code.analysis.log", {{"limit", 4096}}).at("text").get<std::string>().empty(),
              "Analysis log lost its timestamped diagnostics");
        auto state = invoke("code.read", {{"path", "Native/First.cpp"}});
        invoke("code.close", {{"path", "Native/First.cpp"},
                              {"session", state.at("session")},
                              {"state", state.at("state")},
                              {"discard", true}});
        check(invoke("code.read", {{"path", "Native/Second.hpp"}}).at("open").get<bool>(),
              "Closing the first tab damaged another tab");
        check(clipboard_reads == 0 && clipboard_writes == 0,
              "Source operations used the OS clipboard");
        editor.open(files.document, "Native/First.cpp");
        editor.replace_text(draft);
        start_analysis();
        editor.project_changed(root / "DifferentProject");
        check(editor.active_source().empty() &&
                  invoke("code.analysis.status", {}).at("operation").is_null() &&
                  std::filesystem::is_empty(root / ".forge/native/semantic"),
              "Project retirement retained a native query or previous project's analysis");
        std::cout << "Actual C++ source/widget history, clipboard isolation, save guards and tab "
                     "close passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
