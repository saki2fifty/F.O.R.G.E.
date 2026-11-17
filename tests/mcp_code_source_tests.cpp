#include "editor/code_automation.hpp"
#include <iostream>
#include <thread>
#include <tuple>

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
        editor.semantic_generation = [&] { return std::string_view(generation); };
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
        const auto signature_source = invoke("code.read", {{"path", "Native/First.cpp"}});
        start_analysis("signature");
        analysis = settle_analysis();
        check(analysis.at("complete") && analysis.at("readonly") && analysis.at("total") == 2 &&
                  analysis.at("active_signature") == 0 && analysis.at("active_parameter") == 1 &&
                  analysis.at("items").at(0).at("parameters").at(1).at("label") == "float 𐐀rate" &&
                  analysis.at("items").at(0).at("documentation") == "Adjust speed.",
              "Call signatures lost overload, plain-text documentation or active Unicode argument");
        bool signature_readonly = false;
        try {
            use_analysis(analysis);
        } catch (const std::exception& error) {
            signature_readonly =
                std::string_view(error.what()).find("read-only") != std::string_view::npos;
        }
        check(signature_readonly &&
                  invoke("code.read", {{"path", "Native/First.cpp"}}) == signature_source,
              "Read-only signature modified the source, cursor or history");
        generation = "fixture-context-3";
        check(invoke("code.analysis.status", forge::Json::object()).at("stale"),
              "Read-only signature did not detect a retired compiler generation");
        for (const auto* mode :
             {"signature-null", "signature-empty", "signature-zero", "signature-limited",
              "signature-offsets", "signature-markdown", "signature-bad-index",
              "signature-fractional", "signature-oversized", "signature-unavailable"}) {
            SDL_SetEnvironmentVariable(environment, "FORGE_SEMANTIC_FIXTURE_CASE", mode, true);
            start_analysis("signature");
            analysis = settle_analysis();
            const std::string_view selected(mode);
            if (selected == "signature-null" || selected == "signature-empty")
                check(analysis.at("complete") && analysis.at("total") == 0 &&
                          analysis.at("active_signature").is_null(),
                      "Empty signature result failed");
            else if (selected == "signature-zero")
                check(analysis.at("complete") && analysis.at("active_signature") == 0 &&
                          analysis.at("active_parameter").is_null(),
                      "Zero-argument signature failed");
            else if (selected == "signature-limited")
                check(analysis.at("complete") && analysis.at("limited") &&
                          analysis.at("total") == 64 && analysis.at("active_signature").is_null(),
                      "Signature limit lost its bounds");
            else
                check(!analysis.at("complete").get<bool>() &&
                          !analysis.at("error").get<std::string>().empty(),
                      std::string("Untrusted signature reply was accepted: ") + mode);
        }
        SDL_UnsetEnvironmentVariable(environment, "FORGE_SEMANTIC_FIXTURE_CASE");
        const auto diagnostic_source = invoke("code.read", {{"path", "Native/First.cpp"}});
        start_analysis("diagnostics");
        analysis = settle_analysis();
        check(analysis.at("complete") && analysis.at("total") == 1 &&
                  analysis.at("items").at(0).at("severity") == "Warning" &&
                  analysis.at("items").at(0).at("code") == "fixture-warning" &&
                  !analysis.at("occurred_at").get<std::string>().empty() &&
                  invoke("code.read", {{"path", "Native/First.cpp"}}) == diagnostic_source,
              "Diagnostics lost severity/date/source guards or changed source during query");
        use_analysis(analysis);
        check(editor.active_text() == draft &&
                  invoke("code.read", {{"path", "Native/First.cpp"}}).at("cursor").at("line") ==
                      analysis.at("items").at(0).at("line"),
              "Diagnostic navigation changed source or chose the wrong line");
        for (const auto* mode :
             {"diagnostic-late", "diagnostic-empty", "diagnostic-external", "diagnostic-limited",
              "diagnostic-version", "diagnostic-unversioned", "diagnostic-range",
              "diagnostic-severity", "diagnostic-oversized", "diagnostic-missing"}) {
            SDL_SetEnvironmentVariable(environment, "FORGE_SEMANTIC_FIXTURE_CASE", mode, true);
            start_analysis("diagnostics");
            analysis = settle_analysis();
            const std::string_view selected(mode);
            if (selected == "diagnostic-late" || selected == "diagnostic-external")
                check(analysis.at("complete") && analysis.at("total") == 1,
                      "Diagnostics did not wait for or filter the target notification");
            else if (selected == "diagnostic-empty")
                check(analysis.at("complete") && analysis.at("total") == 0,
                      "A valid empty diagnostic publication was refused");
            else if (selected == "diagnostic-limited")
                check(analysis.at("complete") && analysis.at("limited") &&
                          analysis.at("total") == 64,
                      "Diagnostics exceeded their result budget");
            else
                check(!analysis.at("complete").get<bool>() &&
                          !analysis.at("error").get<std::string>().empty(),
                      std::string("Malformed or missing diagnostics accepted: ") + mode);
        }
        SDL_UnsetEnvironmentVariable(environment, "FORGE_SEMANTIC_FIXTURE_CASE");
        const auto information_source = invoke("code.read", {{"path", "Native/First.cpp"}});
        start_analysis("hover");
        analysis = settle_analysis();
        check(analysis.at("complete") && analysis.at("readonly") && analysis.at("total") == 1 &&
                  analysis.at("items").at(0).at("documentation").get<std::string>().find("float") !=
                      std::string::npos,
              "Symbol information lost negotiated plain text");
        bool information_readonly = false;
        try {
            use_analysis(analysis);
        } catch (const std::exception& error) {
            information_readonly =
                std::string_view(error.what()).find("read-only") != std::string_view::npos;
        }
        check(information_readonly &&
                  invoke("code.read", {{"path", "Native/First.cpp"}}) == information_source,
              "Symbol information changed source, cursor or history");
        start_analysis("symbols");
        analysis = settle_analysis();
        const auto nested = invoke("code.analysis.status", {{"offset", 1}, {"limit", 1}});
        check(analysis.at("complete") && analysis.at("total") == 2 &&
                  nested.at("items").at(0).at("depth") == 1 &&
                  nested.at("items").at(0).at("label") == "degrees_per_second",
              "File outline lost nested source symbols or paged indices");
        invoke("code.analysis.use", {{"operation", analysis.at("operation")}, {"index", 1}});
        check(editor.active_text() == draft &&
                  invoke("code.read", {{"path", "Native/First.cpp"}}).at("cursor").at("line") == 2,
              "File outline navigation changed source or chose the wrong location");
        for (const auto* mode :
             {"hover-null", "hover-no-range", "hover-markdown", "hover-range", "hover-oversized",
              "hover-unavailable", "symbols-empty", "symbols-limited", "symbols-depth",
              "symbols-range", "symbols-kind", "symbols-label"}) {
            SDL_SetEnvironmentVariable(environment, "FORGE_SEMANTIC_FIXTURE_CASE", mode, true);
            const std::string_view selected(mode);
            start_analysis(selected.starts_with("hover") ? "hover" : "symbols");
            analysis = settle_analysis();
            if (selected == "hover-null" || selected == "symbols-empty")
                check(analysis.at("complete") && analysis.at("total") == 0,
                      "Empty symbol query failed");
            else if (selected == "hover-no-range")
                check(analysis.at("complete") && analysis.at("readonly") &&
                          !analysis.at("items").at(0).contains("begin"),
                      "Optional information range was invented");
            else if (selected == "symbols-limited" || selected == "symbols-depth")
                check(analysis.at("complete") && analysis.at("limited") &&
                          analysis.at("total") == (selected == "symbols-limited" ? 64 : 16),
                      "File symbol count/depth exceeded its bounds");
            else
                check(!analysis.at("complete").get<bool>() &&
                          !analysis.at("error").get<std::string>().empty(),
                      std::string("Malformed symbol reply accepted: ") + mode);
        }
        SDL_UnsetEnvironmentVariable(environment, "FORGE_SEMANTIC_FIXTURE_CASE");
        for (const auto* kind : {"references", "typeDefinition", "definition", "implementation"}) {
            const auto before_query = invoke("code.read", {{"path", "Native/First.cpp"}});
            start_analysis(kind);
            analysis = settle_analysis();
            check(analysis.at("complete") && analysis.at("total") == 1 &&
                      invoke("code.read", {{"path", "Native/First.cpp"}}) == before_query,
                  "Symbol-navigation query changed source/cursor/history");
            if (std::string_view(kind) == "references" ||
                std::string_view(kind) == "implementation")
                check(analysis.at("scope") == "current_context" &&
                          !analysis.at("exhaustive").get<bool>(),
                      "Symbol navigation claimed an exhaustive project index");
            use_analysis(analysis);
            const auto after_jump = invoke("code.read", {{"path", "Native/First.cpp"}});
            check(after_jump.at("text") == draft && after_jump.at("cursor").at("line") == 2 &&
                      after_jump.at("can_undo") == before_query.at("can_undo") &&
                      after_jump.at("can_redo") == before_query.at("can_redo"),
                  "Symbol navigation edited source or its Undo/Redo");
            for (const auto* mode :
                 {"locations-null", "locations-empty", "locations-object", "locations-link",
                  "locations-link-bounds", "locations-range", "locations-fractional",
                  "locations-limited", "locations-unavailable", "external"}) {
                SDL_SetEnvironmentVariable(environment, "FORGE_SEMANTIC_FIXTURE_CASE", mode, true);
                start_analysis(kind);
                analysis = settle_analysis();
                const std::string_view selected(mode);
                const bool references = std::string_view(kind) == "references";
                if (selected == "locations-null" || selected == "locations-empty")
                    check(analysis.at("complete") && analysis.at("total") == 0,
                          "Empty symbol-navigation reply failed");
                else if (selected == "locations-limited") {
                    const auto last =
                        invoke("code.analysis.status", {{"offset", 63}, {"limit", 1}});
                    check(analysis.at("complete") && analysis.at("limited") &&
                              analysis.at("total") == 64 &&
                              last.at("items").at(0).at("index") == 63,
                          "Symbol-navigation paging exceeded its bound");
                } else if (selected == "external")
                    check(analysis.at("complete") &&
                              !analysis.at("items").at(0).at("available").get<bool>(),
                          "External symbol navigation read or admitted a private source");
                else if (!references &&
                         (selected == "locations-object" || selected == "locations-link"))
                    check(analysis.at("complete") && analysis.at("total") == 1,
                          "Valid symbol-navigation location/link failed");
                else
                    check(!analysis.at("complete").get<bool>() &&
                              !analysis.at("error").get<std::string>().empty(),
                          std::string("Malformed/unavailable symbol navigation accepted: ") + mode);
            }
            SDL_UnsetEnvironmentVariable(environment, "FORGE_SEMANTIC_FIXTURE_CASE");
        }
        const auto before_selection = invoke("code.read", {{"path", "Native/First.cpp"}});
        start_analysis("selection");
        analysis = settle_analysis();
        check(analysis.at("complete").get<bool>() && analysis.at("total").get<std::size_t>() == 2 &&
                  invoke("code.read", {{"path", "Native/First.cpp"}}) == before_selection,
              "Syntax range query changed source/caret/selection/history");
        invoke("code.analysis.use", {{"operation", analysis.at("operation")}, {"index", 1}});
        const auto selected = invoke("code.read", {{"path", "Native/First.cpp"}});
        check(selected.at("text").get<std::string>() == draft &&
                  selected.at("selection").at("active").get<bool>() &&
                  selected.at("selection").at("bytes").get<std::size_t>() == draft.size() - 1 &&
                  selected.at("selection").at("state").get<std::string>() ==
                      forge::ui::cpp_source_state(draft.substr(0, draft.size() - 1)) &&
                  selected.at("can_undo") == before_selection.at("can_undo") &&
                  selected.at("can_redo") == before_selection.at("can_redo"),
              "Syntax selection edited source/history or did not select the widget range");
        for (const auto* mode :
             {"selection-null", "selection-empty", "selection-object", "selection-node-null",
              "selection-count", "selection-parent", "selection-parent-type", "selection-cursor",
              "selection-reversed", "selection-range", "selection-fractional", "selection-split",
              "selection-duplicate", "selection-limited", "selection-unavailable"}) {
            SDL_SetEnvironmentVariable(environment, "FORGE_SEMANTIC_FIXTURE_CASE", mode, true);
            start_analysis("selection");
            analysis = settle_analysis();
            const std::string_view value(mode);
            if (value == "selection-null" || value == "selection-empty")
                check(analysis.at("complete").get<bool>() &&
                          analysis.at("total").get<std::size_t>() == 0,
                      "Empty syntax selection reply failed");
            else if (value == "selection-duplicate" || value == "selection-limited")
                check(analysis.at("complete").get<bool>() &&
                          analysis.at("total").get<std::size_t>() == 2 &&
                          analysis.at("limited").get<bool>() == (value == "selection-limited"),
                      "Syntax selection duplicate/depth bound was not retained");
            else
                check(!analysis.at("complete").get<bool>() &&
                          !analysis.at("error").get<std::string>().empty(),
                      std::string("Malformed/unavailable syntax selection accepted: ") + mode);
        }
        SDL_UnsetEnvironmentVariable(environment, "FORGE_SEMANTIC_FIXTURE_CASE");
        invoke("code.analysis.start", {{"path", "Native/First.cpp"},
                                       {"session", before_selection.at("session")},
                                       {"state", before_selection.at("state")},
                                       {"kind", "selection"},
                                       {"offset", draft.size()}});
        analysis = settle_analysis();
        check(!analysis.at("complete").get<bool>(),
              "Syntax selection admitted a cursor past the visible final newline");
        editor.open(files.document, "Native/First.cpp", 1, 1);
        const auto range_begin = draft.find("float "), range_end = draft.find("};", range_begin);
        const auto range_args = [&](const std::string& kind = "formatSelection") {
            const auto observed = invoke("code.read", {{"path", "Native/First.cpp"}});
            return forge::Json{{"path", "Native/First.cpp"},    {"session", observed.at("session")},
                               {"state", observed.at("state")}, {"kind", kind},
                               {"offset", range_begin},         {"end_offset", range_end}};
        };
        for (const auto* mode :
             {"range-format-object", "range-format-unavailable", "range-format-absent"}) {
            SDL_SetEnvironmentVariable(environment, "FORGE_SEMANTIC_FIXTURE_CASE", mode, true);
            const auto before_range = invoke("code.read", {{"path", "Native/First.cpp"}});
            invoke("code.analysis.start", range_args());
            analysis = settle_analysis();
            if (std::string_view(mode) == "range-format-object") {
                check(analysis.at("complete").get<bool>() &&
                          analysis.at("total").get<std::size_t>() == 1 &&
                          invoke("code.read", {{"path", "Native/First.cpp"}}) == before_range,
                      "Selected formatting did not retain read-only review/source state");
                const auto proposal = analysis.at("items").at(0).at("text").get<std::string>();
                check(proposal.substr(0, range_begin) == draft.substr(0, range_begin) &&
                          proposal.ends_with(draft.substr(range_end)),
                      "Fixture selected-format proposal changed unselected neighboring source");
                use_analysis(analysis);
                check(editor.active_text() == proposal,
                      "Selected format differed from reviewed proposal");
                editor.undo(false);
                check(editor.active_text() == draft, "Selected formatting was not one source Undo");
                editor.undo(true);
                check(editor.active_text() == proposal,
                      "Selected formatting Redo lost its proposal");
                editor.undo(false);
            } else
                check(!analysis.at("complete").get<bool>() &&
                          !analysis.at("error").get<std::string>().empty(),
                      "Unsupported range formatting capability was admitted");
            check(forge::ui::read_cpp_source(root / "Native/First.cpp") == disk_before_analysis,
                  "Range query or proposal changed saved bytes");
        }
        SDL_UnsetEnvironmentVariable(environment, "FORGE_SEMANTIC_FIXTURE_CASE");
        const auto before_invalid_range = invoke("code.analysis.status", {});
        for (const auto& invalid :
             {forge::Json{{"end_offset", range_begin}},
              forge::Json{{"end_offset", draft.size() + 1}}, forge::Json{{"kind", "format"}}}) {
            auto arguments = range_args();
            arguments.update(invalid);
            bool refused = false;
            try {
                invoke("code.analysis.start", arguments);
            } catch (const std::exception&) {
                refused = true;
            }
            check(refused && invoke("code.analysis.status", {}) == before_invalid_range,
                  "Invalid formatting range launched or retired an existing task");
        }
        const auto before_format = invoke("code.read", {{"path", "Native/First.cpp"}});
        start_analysis("format");
        analysis = settle_analysis();
        check(analysis.at("complete") && analysis.at("total") == 1 &&
                  analysis.at("items").at(0).at("edit_count") == 2 &&
                  !analysis.contains("format_edits") && !analysis.contains("source_edits") &&
                  invoke("code.read", {{"path", "Native/First.cpp"}}) == before_format,
              "Formatting preview changed source/history or exposed internal cursor edits");
        const auto formatted = analysis.at("items").at(0).at("text").get<std::string>();
        const auto caret = forge::ui::cpp_source_coordinate(draft, draft.rfind("s.de") + 2);
        editor.open(files.document, "Native/First.cpp", caret.first + 1, caret.second + 1);
        use_analysis(analysis);
        const auto mapped =
            forge::ui::cpp_source_coordinate(formatted, formatted.rfind("s.de") + 2);
        check(editor.active_text() == formatted &&
                  invoke("code.read", {{"path", "Native/First.cpp"}}).at("cursor") ==
                      forge::Json{{"line", mapped.first + 1}, {"column", mapped.second + 1}},
              "Formatting failed to map the current cursor through its edits");
        editor.undo(false);
        check(editor.active_text() == draft, "Formatting required more than one source Undo");
        editor.undo(true);
        check(editor.active_text() == formatted, "Formatting Redo lost its complete proposal");
        editor.undo(false);
        for (const auto* mode :
             {"format-null", "format-empty", "format-noop", "format-object", "format-reversed",
              "format-overlap", "format-ambiguous", "format-range", "format-utf8", "format-control",
              "format-budget", "format-count", "format-unavailable", "format-crlf", "format-tabs",
              "format-unicode", "format-disordered"}) {
            SDL_SetEnvironmentVariable(environment, "FORGE_SEMANTIC_FIXTURE_CASE", mode, true);
            start_analysis("format");
            analysis = settle_analysis();
            const std::string_view selected(mode);
            if (selected == "format-null" || selected == "format-empty" ||
                selected == "format-noop")
                check(analysis.at("complete") && analysis.at("total") == 0,
                      "Empty/no-op formatting produced a source replacement");
            else if (selected == "format-crlf" || selected == "format-tabs" ||
                     selected == "format-unicode" || selected == "format-disordered") {
                check(analysis.at("complete") && analysis.at("total") == 1,
                      "Valid bounded formatting failed");
                const auto proposed = analysis.at("items").at(0).at("text").get<std::string>();
                check(proposed.find('\r') == proposed.npos,
                      "Formatting escaped the editor's LF contract");
                use_analysis(analysis);
                check(editor.active_text() == proposed,
                      "Formatting diverged from its reviewed text");
                editor.undo(false);
                check(editor.active_text() == draft, "Formatting tab/Unicode Undo lost source");
            } else
                check(!analysis.at("complete").get<bool>() &&
                          !analysis.at("error").get<std::string>().empty(),
                      std::string("Malformed/unavailable formatting accepted: ") + mode);
            check(editor.active_text() == draft &&
                      forge::ui::read_cpp_source(root / "Native/First.cpp") == disk_before_analysis,
                  "Formatting query or Undo changed source/disk baseline");
        }
        SDL_UnsetEnvironmentVariable(environment, "FORGE_SEMANTIC_FIXTURE_CASE");
        std::string large_format = draft, expected_large_format = draft;
        for (int i = 0; i != 8192; ++i) {
            const auto line = "// line " + std::to_string(i) + " café\n";
            large_format += line;
            expected_large_format += " " + line;
        }
        editor.replace_text(large_format);
        SDL_SetEnvironmentVariable(environment, "FORGE_SEMANTIC_FIXTURE_CASE", "format-many", true);
        start_analysis("format");
        analysis = settle_analysis();
        check(analysis.at("complete") && analysis.at("total") == 1 &&
                  analysis.at("items").at(0).at("edit_count") == 8192 &&
                  analysis.at("items").at(0).at("text") == expected_large_format,
              "Large formatting set was truncated or used incorrect indexed positions");
        use_analysis(analysis);
        check(editor.active_text() == expected_large_format,
              "Large format diverged from its candidate");
        editor.undo(false);
        check(editor.active_text() == large_format, "Large format did not share one source Undo");
        editor.undo(false);
        check(editor.active_text() == draft,
              "Large-format fixture lost the preceding source draft");
        SDL_UnsetEnvironmentVariable(environment, "FORGE_SEMANTIC_FIXTURE_CASE");
        {
            // Actual isolated source owner; navigation has no compiler/provider dependency.
            forge::atomic_write(root / "Native/NavA.cpp", "first\nsecond\nthird\n");
            forge::atomic_write(root / "Native/NavB.hpp", "#pragma once\n");
            forge::ui::CppSourceEditor navigator;
            auto navigation_methods = forge::CodeAutomation::methods(
                scene, files, navigator, authored, prefab, providers, permissions);
            const auto call_navigation = [&](const std::string& name, const forge::Json& request) {
                const auto method =
                    std::find_if(navigation_methods.begin(), navigation_methods.end(),
                                 [&](const auto& value) { return value.name == name; });
                check(method != navigation_methods.end(), "Missing navigation operation");
                return method->invoke(request);
            };
            const auto navigation = [&] {
                return call_navigation("code.navigation.status", forge::Json::object());
            };
            const auto move_request = [&](const forge::Json& before, const char* direction) {
                const auto& source = before.at("source");
                return forge::Json{{"path", source.at("path")},
                                   {"session", source.at("session")},
                                   {"state", source.at("state")},
                                   {"navigation_revision", before.at("revision")},
                                   {"direction", direction}};
            };
            const auto move = [&](const char* direction) {
                return call_navigation("code.navigation.move",
                                       move_request(navigation(), direction));
            };
            navigator.open(files.document, "Native/NavA.cpp", 3, 5);
            const auto original_navigation_text = navigator.active_text();
            navigator.open(files.document, "Native/NavB.hpp");
            auto before = navigation();
            navigator.open(files.document, "Native/NavB.hpp");
            check(navigation() == before && before.at("back_count") == 1,
                  "Repeated open manufactured a source navigation location");
            auto moved = move("back");
            check(moved.at("source").at("path") == "Native/NavA.cpp" &&
                      moved.at("source").at("cursor") == forge::Json{{"line", 3}, {"column", 5}},
                  "Back lost its source/caret witness");
            moved = move("forward");
            before = navigation();
            for (const auto* fault : {"revision", "source", "direction"}) {
                auto request = move_request(before, "back");
                if (std::string_view(fault) == "revision")
                    request["navigation_revision"] = 0;
                else if (std::string_view(fault) == "source")
                    request["state"] = std::string(64, '0');
                else
                    request["direction"] = "sideways";
                bool refused = false;
                try {
                    call_navigation("code.navigation.move", request);
                } catch (const std::exception&) {
                    refused = true;
                }
                check(refused && navigation() == before,
                      "Failed navigation changed source or location history");
            }
            auto source = call_navigation("code.read", {{"path", "Native/NavA.cpp"}});
            call_navigation("code.replace", {{"path", source.at("path")},
                                             {"session", source.at("session")},
                                             {"state", source.at("state")},
                                             {"text", "\té\n"}});
            move("back");
            moved = move("back");
            check(navigator.active_text() == "\té\n" &&
                      moved.at("source").at("cursor") == forge::Json{{"line", 1}, {"column", 5}},
                  "Historical location did not clamp to edited UTF8/tab coordinates");
            navigator.undo(false);
            check(navigator.active_text() == original_navigation_text,
                  "Navigation Undo mismatch: expected " +
                      forge::Json(original_navigation_text).dump() + " actual " +
                      forge::Json(navigator.active_text()).dump());
            navigator.undo(true);
            navigator.open(files.document, "Native/NavB.hpp");
            check(navigation().at("forward_count") == 0,
                  "A new source jump retained the old Forward branch");
            for (int i = 0; i != 140; ++i)
                navigator.open(files.document, i % 2 == 0 ? "Native/NavA.cpp" : "Native/NavB.hpp");
            check(navigation().at("back_count") == 128,
                  "Source navigation exceeded its location budget");
            source = call_navigation("code.read", {{"path", "Native/NavA.cpp"}});
            call_navigation("code.close", {{"path", source.at("path")},
                                           {"session", source.at("session")},
                                           {"state", source.at("state")},
                                           {"discard", true}});
            std::filesystem::remove(root / "Native/NavA.cpp");
            before = navigation();
            bool missing_refused = false;
            try {
                move("back");
            } catch (const std::exception&) {
                missing_refused = true;
            }
            check(missing_refused && navigation() == before,
                  "A missing closed source silently popped or redirected a navigation location");
            const auto text_before_clear = navigator.active_text();
            auto cleared = call_navigation("code.navigation.clear",
                                           {{"navigation_revision", before.at("revision")}});
            check(cleared.at("back_count") == 0 && cleared.at("forward_count") == 0 &&
                      navigator.active_text() == text_before_clear,
                  "Clear navigation changed source");
            check(call_navigation("code.navigation.clear",
                                  {{"navigation_revision", cleared.at("revision")}}) == cleared,
                  "Empty navigation clear was not a no-op");
            navigator.project_changed(root / "AnotherProject");
            check(navigation().at("revision").is_null() && navigation().at("source").is_null(),
                  "Navigation leaked a retired project's source or locations");
        }
        {
            const auto rename_source = draft + "\nfloat rename_probe(float rename_rate) { /* café "
                                               "*/ return rename_rate + rename_rate; }\n";
            const auto rename_after = draft + "\nfloat rename_probe(float angular_speed) { /* café "
                                              "*/ return angular_speed + angular_speed; }\n";
            editor.replace_text(rename_source);
            const auto rename_query = [&](const std::string& name, const char* kind = "rename") {
                const auto observed = invoke("code.read", {{"path", "Native/First.cpp"}});
                return invoke("code.analysis.start", {{"path", "Native/First.cpp"},
                                                      {"session", observed.at("session")},
                                                      {"state", observed.at("state")},
                                                      {"kind", kind},
                                                      {"offset", rename_source.find("rename_rate")},
                                                      {"new_name", name}});
            };
            const auto before = invoke("code.read", {{"path", "Native/First.cpp"}});
            rename_query("angular_speed");
            analysis = settle_analysis();
            check(analysis.at("complete").get<bool>() &&
                      analysis.at("total").get<std::size_t>() == 1 &&
                      analysis.at("new_name").get<std::string>() == "angular_speed" &&
                      analysis.at("scope").get<std::string>() == "current_context" &&
                      !analysis.at("exhaustive").get<bool>() &&
                      analysis.at("items").at(0).at("text").get<std::string>() == rename_after &&
                      analysis.at("items").at(0).at("edit_count").get<std::size_t>() == 3 &&
                      !analysis.contains("source_edits") && !analysis.contains("format_edits") &&
                      invoke("code.read", {{"path", "Native/First.cpp"}}) == before,
                  "Rename preview changed source/selection/history or lost its complete "
                  "proposal/scope");
            const auto caret = forge::ui::cpp_source_coordinate(
                rename_source, rename_source.rfind("rename_rate") + 2);
            editor.open(files.document, "Native/First.cpp", caret.first + 1, caret.second + 1);
            use_analysis(analysis);
            const auto mapped = forge::ui::cpp_source_coordinate(
                rename_after,
                rename_after.rfind("angular_speed") + std::string_view("angular_speed").size());
            auto renamed = invoke("code.read", {{"path", "Native/First.cpp"}});
            check(renamed.at("text").get<std::string>() == rename_after &&
                      renamed.at("cursor") ==
                          forge::Json{{"line", mapped.first + 1}, {"column", mapped.second + 1}},
                  "Rename did not map the current caret through its shared source edit path");
            editor.undo(false);
            check(editor.active_text() == rename_source, "Rename was not one source Undo");
            editor.undo(true);
            check(editor.active_text() == rename_after, "Rename Redo lost edits");
            editor.undo(false);
            rename_query("rename_rate");
            analysis = settle_analysis();
            check(analysis.at("complete").get<bool>() &&
                      analysis.at("total").get<std::size_t>() == 0,
                  "Equal-name rename created a source proposal");
            for (const auto* mode :
                 {"rename-null", "rename-empty", "rename-count", "rename-other", "rename-multi",
                  "rename-foreign", "rename-document-changes", "rename-resource",
                  "rename-annotations", "rename-annotated-edit", "rename-command", "rename-range",
                  "rename-overlap", "rename-split", "rename-disordered", "rename-unavailable"}) {
                SDL_SetEnvironmentVariable(environment, "FORGE_SEMANTIC_FIXTURE_CASE", mode, true);
                const auto observed = invoke("code.read", {{"path", "Native/First.cpp"}});
                rename_query("angular_speed");
                analysis = settle_analysis();
                const std::string_view value(mode);
                if (value == "rename-null" || value == "rename-empty")
                    check(analysis.at("complete").get<bool>() &&
                              analysis.at("total").get<std::size_t>() == 0,
                          "Empty rename reply failed");
                else if (value == "rename-disordered")
                    check(analysis.at("complete").get<bool>() &&
                              analysis.at("total").get<std::size_t>() == 1 &&
                              analysis.at("items").at(0).at("text").get<std::string>() ==
                                  rename_after,
                          "Unordered rename edits lost the whole proposal");
                else
                    check(!analysis.at("complete").get<bool>() &&
                              !analysis.at("error").get<std::string>().empty(),
                          std::string("Unsafe rename reply accepted: ") + mode);
                check(invoke("code.read", {{"path", "Native/First.cpp"}}) == observed,
                      "Rejected or preview-only rename changed source/caret/selection/history");
            }
            SDL_UnsetEnvironmentVariable(environment, "FORGE_SEMANTIC_FIXTURE_CASE");
            for (const auto& name : {std::string{}, std::string("not valid"), std::string(257, 'x'),
                                     std::string("bad\xFF", 4)}) {
                const auto operation =
                    invoke("code.analysis.status", forge::Json::object()).at("operation");
                bool rejected = false;
                try {
                    rename_query(name);
                } catch (const std::exception&) {
                    rejected = true;
                }
                check(rejected &&
                          invoke("code.analysis.status", forge::Json::object()).at("operation") ==
                              operation,
                      "Invalid rename name retired the query or launched a tool");
            }
            bool wrong_kind = false;
            try {
                rename_query("angular_speed", "completion");
            } catch (const std::exception&) {
                wrong_kind = true;
            }
            check(wrong_kind, "Another query accepted a rename name");
            editor.undo(false);
            check(editor.active_text() == draft, "Rename checks lost the preceding source history");
        }
        {
            // Closed sources retain raw CRLF/no-final-newline bytes until explicit Save.
            const std::string header_path = "Native/ProjectClosed.hpp";
            const std::string use_path = "Native/ProjectClosedUse.cpp";
            const std::string header_raw =
                "#pragma once\r\nstruct ProjectClosed { float project_rate; };";
            const std::string use_raw =
                "// café\n#include \"ProjectClosed.hpp\"\n"
                "float read_project(ProjectClosed& x) { return x.project_rate; }";
            forge::atomic_write(root / header_path, header_raw);
            forge::atomic_write(root / use_path, use_raw);
            editor.open(files.document, "Native/First.cpp");
            const auto original_first = editor.active_text();
            const auto first_raw = forge::ui::read_cpp_source(root / "Native/First.cpp");
            const auto first =
                original_first + "\n// Multi-source rename fixture\nfloat project_rate = 1;\n";
            editor.replace_text(first);
            const auto tabs_before = invoke("code.status", forge::Json::object()).at("tabs").size();
            SDL_SetEnvironmentVariable(environment, "FORGE_SEMANTIC_FIXTURE_CASE", "rename-open",
                                       true);
            const auto query = [&] {
                editor.open(files.document, "Native/First.cpp");
                const auto observed = invoke("code.read", {{"path", "Native/First.cpp"}});
                return invoke("code.analysis.start", {{"path", "Native/First.cpp"},
                                                      {"session", observed.at("session")},
                                                      {"state", observed.at("state")},
                                                      {"kind", "renameProject"},
                                                      {"offset", first.find("project_rate")},
                                                      {"new_name", "angular_speed"}});
            };
            {
                forge::ui::CppSemanticTask captured;
                const auto disk_state = forge::gameplay_source_digest(root);
                forge::atomic_write(root / use_path, use_raw + "\n// changed during capture\n");
                bool stale_capture = false;
                try {
                    captured.start(editor.semantic_context(), argv[2], "Native/First.cpp",
                                   "renameProject", first.find("project_rate"),
                                   {{std::filesystem::path("Native/First.cpp"), first}},
                                   "angular_speed", {}, disk_state);
                } catch (const std::exception&) {
                    stale_capture = true;
                }
                check(stale_capture && !captured.busy() && captured.operation().empty() &&
                          editor.active_text() == first,
                      "Changed project capture launched analysis or changed an existing draft");
                forge::atomic_write(root / use_path, use_raw);
            }
            query();
            auto project_result = settle_analysis();
            check(project_result.at("complete").get<bool>() &&
                      project_result.at("scope").get<std::string>() ==
                          "project_sources_current_context" &&
                      !project_result.at("exhaustive").get<bool>() &&
                      project_result.at("items").at(0).at("file_count").get<std::size_t>() == 3 &&
                      invoke("code.status", forge::Json::object()).at("tabs").size() == tabs_before,
                  "Project rename did not capture unopened sources without changing tabs");
            std::vector<std::filesystem::path> input_limit_files;
            for (unsigned i = 0; i < 65; ++i) {
                const auto path = root / "Native" / ("ProjectLimit" + std::to_string(i) + ".hpp");
                forge::atomic_write(path, "// bounded project input\n");
                input_limit_files.push_back(path);
            }
            bool input_limit = false;
            try {
                query();
            } catch (const std::exception&) {
                input_limit = true;
            }
            check(input_limit && editor.active_text() == first &&
                      invoke("code.status", forge::Json::object()).at("tabs").size() == tabs_before,
                  "Project source limit truncated input or changed visible drafts");
            for (const auto& path : input_limit_files)
                std::filesystem::remove(path);
            // A stale later file refuses the whole proposal before any missing tab is admitted.
            forge::atomic_write(root / use_path, use_raw + "\n// changed after preview\n");
            bool refused = false;
            try {
                use_analysis(project_result);
            } catch (const std::exception&) {
                refused = true;
            }
            check(refused && editor.active_text() == first &&
                      invoke("code.status", forge::Json::object()).at("tabs").size() == tabs_before,
                  "Stale closed source changed an earlier draft or admitted partial tabs");
            forge::atomic_write(root / use_path, use_raw);
            // Membership changes retire a complete proposal, even without edited file bytes.
            for (bool added : {true, false}) {
                query();
                project_result = settle_analysis();
                const auto extra_path = root / "Native/ProjectAdded.hpp";
                if (added)
                    forge::atomic_write(extra_path, "// discovered after preview\n");
                else
                    std::filesystem::remove(root / use_path);
                bool membership_changed = false;
                try {
                    use_analysis(project_result);
                } catch (const std::exception&) {
                    membership_changed = true;
                }
                check(membership_changed && editor.active_text() == first &&
                          invoke("code.status", forge::Json::object()).at("tabs").size() ==
                              tabs_before,
                      "Changed source membership allowed partial rename or admitted tabs");
                if (added)
                    std::filesystem::remove(extra_path);
                else
                    forge::atomic_write(root / use_path, use_raw);
            }
            std::vector<std::string> extra_tabs;
            while (invoke("code.status", forge::Json::object()).at("tabs").size() < 32) {
                const auto path = "Native/ProjectTab" + std::to_string(extra_tabs.size()) + ".hpp";
                forge::atomic_write(root / path, "// unrelated clean source\n");
                editor.open(files.document, path);
                extra_tabs.push_back(path);
            }
            query();
            project_result = settle_analysis();
            bool tab_limit = false;
            try {
                use_analysis(project_result);
            } catch (const std::exception&) {
                tab_limit = true;
            }
            check(tab_limit && editor.active_text() == first &&
                      invoke("code.status", forge::Json::object()).at("tabs").size() == 32,
                  "Project tab limit changed an earlier draft or admitted partial tabs");
            for (const auto& path : extra_tabs) {
                const auto observed = invoke("code.read", {{"path", path}});
                invoke("code.close", {{"path", path},
                                      {"session", observed.at("session")},
                                      {"state", observed.at("state")}});
                std::filesystem::remove(root / path);
            }
            query();
            project_result = settle_analysis();
            // Opening the unchanged captured header keeps the project-scope query current.
            editor.open(files.document, header_path);
            use_analysis(project_result);
            for (const auto& proposed : project_result.at("items").at(0).at("files")) {
                const auto path = proposed.at("path").get<std::string>();
                const auto expected = proposed.at("text").get<std::string>();
                const auto state = proposed.at("state").get<std::string>();
                editor.open(files.document, path);
                check(editor.active_text() == expected,
                      "Project rename lost a complete file proposal");
                editor.undo(false);
                check(forge::ui::cpp_source_state(editor.active_text()) == state,
                      "Project rename did not preserve each file's canonical Undo baseline");
                editor.undo(true);
                check(editor.active_text() == expected, "Project rename Redo lost a file proposal");
                editor.undo(false);
            }
            check(forge::ui::read_cpp_source(root / header_path) == header_raw &&
                      forge::ui::read_cpp_source(root / use_path) == use_raw &&
                      forge::ui::read_cpp_source(root / "Native/First.cpp") == first_raw,
                  "Query, Apply or Undo saved a project rename implicitly");
            editor.open(files.document, "Native/First.cpp");
            editor.undo(false);
            check(editor.active_text() == original_first,
                  "Project rename lost preceding source history");
        }
        {
            const auto first = draft + "\nfloat shared_rate = 1; // café\n";
            const std::string second = "#pragma once\nextern float shared_rate;\n";
            const std::string second_path = "Native/OpenRename.hpp";
            forge::atomic_write(root / second_path, second);
            editor.open(files.document, second_path);
            const auto second_draft = editor.active_text();
            editor.open(files.document, "Native/First.cpp");
            editor.replace_text(first);
            const auto query = [&] {
                const auto observed = invoke("code.read", {{"path", "Native/First.cpp"}});
                return invoke("code.analysis.start", {{"path", "Native/First.cpp"},
                                                      {"session", observed.at("session")},
                                                      {"state", observed.at("state")},
                                                      {"kind", "renameOpen"},
                                                      {"offset", first.find("shared_rate")},
                                                      {"new_name", "angular_speed"}});
            };
            SDL_SetEnvironmentVariable(environment, "FORGE_SEMANTIC_FIXTURE_CASE", "rename-open",
                                       true);
            const auto before_first = invoke("code.read", {{"path", "Native/First.cpp"}});
            const auto before_second = invoke("code.read", {{"path", second_path}});
            query();
            analysis = settle_analysis();
            check(analysis.at("complete").get<bool>() && analysis.at("total") == 1 &&
                      analysis.at("scope") == "open_sources_current_context" &&
                      !analysis.at("exhaustive").get<bool>() &&
                      analysis.at("items").at(0).at("file_count") == 2 &&
                      analysis.at("items").at(0).at("edit_count") == 2 &&
                      invoke("code.read", {{"path", "Native/First.cpp"}}) == before_first &&
                      invoke("code.read", {{"path", second_path}}) == before_second,
                  "Rename in open files lost its complete preview or changed a draft");
            const auto first_caret =
                forge::ui::cpp_source_coordinate(first, first.find("shared_rate") + 2);
            const auto second_caret = forge::ui::cpp_source_coordinate(
                second_draft, second_draft.find("shared_rate") + 3);
            editor.open(files.document, second_path, second_caret.first + 1,
                        second_caret.second + 1);
            editor.open(files.document, "Native/First.cpp", first_caret.first + 1,
                        first_caret.second + 1);
            use_analysis(analysis);
            auto first_after = first, second_after = second_draft;
            first_after.replace(first_after.find("shared_rate"), 11, "angular_speed");
            second_after.replace(second_after.find("shared_rate"), 11, "angular_speed");
            check(editor.active_text() == first_after &&
                      invoke("code.read", {{"path", second_path}}).at("text") == second_after,
                  "Rename in open files did not apply every reviewed draft: active=" +
                      editor.active_source() + "; first=" + editor.active_text() +
                      "; expected first=" + first_after +
                      "; second=" + invoke("code.read", {{"path", second_path}}).at("text").dump() +
                      "; expected second=" + second_after);
            for (const auto& [path, text] :
                 {std::pair{std::string("Native/First.cpp"), first_after},
                  std::pair{second_path, second_after}}) {
                const auto point =
                    forge::ui::cpp_source_coordinate(text, text.find("angular_speed") + 13);
                check(invoke("code.read", {{"path", path}}).at("cursor") ==
                          forge::Json{{"line", point.first + 1}, {"column", point.second + 1}},
                      "Rename did not map each file's own current caret");
            }
            editor.undo(false);
            check(editor.active_text() == first, "First source rename was not one Undo");
            editor.undo(true);
            check(editor.active_text() == first_after, "First source rename Redo failed");
            editor.undo(false);
            editor.open(files.document, second_path);
            editor.undo(false);
            check(editor.active_text() == second_draft, "Second source rename was not one Undo");
            editor.undo(true);
            check(editor.active_text() == second_after, "Second source rename Redo failed");
            editor.undo(false);
            editor.open(files.document, "Native/First.cpp");
            query();
            analysis = settle_analysis();
            editor.open(files.document, second_path);
            editor.replace_text(second_draft + "// changed after preview\n");
            bool stale = false;
            try {
                use_analysis(analysis);
            } catch (const std::exception&) {
                stale = true;
            }
            check(stale &&
                      invoke("code.read", {{"path", "Native/First.cpp"}}).at("text") == first &&
                      editor.active_text() == second_draft + "// changed after preview\n",
                  "A changed second draft allowed a partial rename");
            editor.undo(false);
            editor.open(files.document, "Native/First.cpp");
            for (const auto* mode :
                 {"rename-open-unopened", "rename-open-alias", "rename-open-range",
                  "rename-open-overlap", "rename-open-annotations", "rename-foreign",
                  "rename-document-changes", "rename-resource"}) {
                SDL_SetEnvironmentVariable(environment, "FORGE_SEMANTIC_FIXTURE_CASE", mode, true);
                const auto unchanged_first = invoke("code.read", {{"path", "Native/First.cpp"}});
                const auto unchanged_second = invoke("code.read", {{"path", second_path}});
                query();
                analysis = settle_analysis();
                check(!analysis.at("complete").get<bool>() &&
                          !analysis.at("error").get<std::string>().empty() &&
                          invoke("code.read", {{"path", "Native/First.cpp"}}) == unchanged_first &&
                          invoke("code.read", {{"path", second_path}}) == unchanged_second,
                      std::string("Unsafe multi-source rename was admitted: ") + mode);
            }
            forge::atomic_write(root / "Native/BudgetRename.hpp", "extern float shared_rate;\n");
            editor.open(files.document, "Native/BudgetRename.hpp");
            const auto budget_before = invoke("code.read", {{"path", "Native/BudgetRename.hpp"}});
            editor.open(files.document, "Native/First.cpp");
            const auto first_budget_before = invoke("code.read", {{"path", "Native/First.cpp"}});
            const auto second_budget_before = invoke("code.read", {{"path", second_path}});
            SDL_SetEnvironmentVariable(environment, "FORGE_SEMANTIC_FIXTURE_CASE",
                                       "rename-open-budget", true);
            query();
            analysis = settle_analysis();
            check(!analysis.at("complete").get<bool>() &&
                      analysis.at("error").get<std::string>().size() &&
                      invoke("code.analysis.log", forge::Json::object())
                              .at("text")
                              .get<std::string>()
                              .find("4 MiB") != std::string::npos &&
                      invoke("code.read", {{"path", "Native/First.cpp"}}) == first_budget_before &&
                      invoke("code.read", {{"path", second_path}}) == second_budget_before &&
                      invoke("code.read", {{"path", "Native/BudgetRename.hpp"}}) == budget_before,
                  "Oversized rename preview was accepted or changed a source");
            SDL_UnsetEnvironmentVariable(environment, "FORGE_SEMANTIC_FIXTURE_CASE");
            check(forge::ui::read_cpp_source(root / second_path) == second &&
                      forge::ui::read_cpp_source(root / "Native/First.cpp") == disk_before_analysis,
                  "Multi-source rename saved a draft implicitly");
            editor.undo(false);
            check(editor.active_text() == draft, "Multi-source rename lost earlier source history");
        }
        {
            const auto source =
                draft + "\n// café\nint fixes_probe() { int value = 1 return value; }\n";
            auto fixed = source;
            fixed.insert(fixed.find("1 return") + 1, ";");
            editor.replace_text(source);
            const auto query = [&] {
                const auto observed = invoke("code.read", {{"path", "Native/First.cpp"}});
                return invoke("code.analysis.start", {{"path", "Native/First.cpp"},
                                                      {"session", observed.at("session")},
                                                      {"state", observed.at("state")},
                                                      {"kind", "fixes"},
                                                      {"offset", source.find("1 return") + 2}});
            };
            const auto before = invoke("code.read", {{"path", "Native/First.cpp"}});
            query();
            analysis = settle_analysis();
            check(analysis.at("complete").get<bool>() &&
                      analysis.at("total").get<std::size_t>() == 1 &&
                      analysis.at("items").at(0).at("text").get<std::string>() == fixed &&
                      !analysis.contains("source_edits") &&
                      invoke("code.read", {{"path", "Native/First.cpp"}}) == before,
                  "Quick-fix preview changed source/history or exposed private mappings");
            const auto caret = forge::ui::cpp_source_coordinate(source, source.rfind("value"));
            editor.open(files.document, "Native/First.cpp", caret.first + 1, caret.second + 1);
            use_analysis(analysis);
            check(editor.active_text() == fixed, "Quick fix lost the complete source proposal");
            const auto mapped = forge::ui::cpp_source_coordinate(fixed, fixed.rfind("value"));
            check(invoke("code.read", {{"path", "Native/First.cpp"}}).at("cursor") ==
                      forge::Json{{"line", mapped.first + 1}, {"column", mapped.second + 1}},
                  "Quick fix did not map the current caret");
            editor.undo(false);
            check(editor.active_text() == source, "Quick fix needed more than one Undo");
            editor.undo(true);
            check(editor.active_text() == fixed, "Quick-fix Redo lost source edits");
            editor.undo(false);
            for (const auto* mode :
                 {"fixes-null", "fixes-empty", "fixes-command", "fixes-cross-file",
                  "fixes-annotations", "fixes-payload", "fixes-range", "fixes-disabled",
                  "fixes-resolve", "fixes-kind", "fixes-no-edit", "fixes-title",
                  "fixes-unavailable", "diagnostic-version", "diagnostic-unversioned",
                  "fixes-multiple", "fixes-mixed", "fixes-limited", "fixes-budget"}) {
                SDL_SetEnvironmentVariable(environment, "FORGE_SEMANTIC_FIXTURE_CASE", mode, true);
                const auto observed = invoke("code.read", {{"path", "Native/First.cpp"}});
                query();
                analysis = settle_analysis();
                const std::string_view selected(mode);
                if (selected == "fixes-title" || selected == "fixes-unavailable" ||
                    selected == "diagnostic-version" || selected == "diagnostic-unversioned")
                    check(!analysis.at("complete").get<bool>() &&
                              !analysis.at("error").get<std::string>().empty(),
                          "Malformed/unsupported quick-fix query succeeded");
                else if (selected == "fixes-null" || selected == "fixes-empty")
                    check(analysis.at("complete").get<bool>() &&
                              analysis.at("total").get<std::size_t>() == 0,
                          "Empty quick-fix response failed");
                else if (selected == "fixes-budget") {
                    check(analysis.at("complete").get<bool>() &&
                              analysis.at("total").get<std::size_t>() == 32 &&
                              analysis.at("limited").get<bool>() &&
                              analysis.at("items").at(0).at("available").get<bool>(),
                          "Quick-fix aggregate preview budget did not retain bounded results");
                    const auto tail =
                        invoke("code.analysis.status", {{"offset", 31}, {"limit", 1}});
                    check(!tail.at("items").at(0).at("available").get<bool>() &&
                              tail.at("items").at(0).at("reason").get<std::string>().find(
                                  "4 MiB") != std::string::npos,
                          "Over-budget quick-fix preview remained available");
                } else if (selected == "fixes-multiple" || selected == "fixes-mixed" ||
                           selected == "fixes-limited") {
                    const auto count = selected == "fixes-limited" ? 64u : 2u;
                    const auto second =
                        invoke("code.analysis.status", {{"offset", 1}, {"limit", 1}});
                    check(analysis.at("complete").get<bool>() &&
                              analysis.at("total").get<std::size_t>() == count &&
                              second.at("items").at(0).at("available").get<bool>() &&
                              analysis.at("limited").get<bool>() == (selected == "fixes-limited"),
                          "Quick-fix alternatives or truncation lost bounded availability");
                    // Choose a nonzero proposal: its caret mapping must use that row's edits.
                    invoke("code.analysis.use",
                           {{"operation", analysis.at("operation")}, {"index", 1}});
                    auto expected = source;
                    expected.insert(expected.find("1 return") + 1,
                                    selected == "fixes-multiple" ? "; /* fixed */" : ";");
                    check(editor.active_text() == expected,
                          "Quick-fix alternative used another row's source edits");
                    const auto expected_caret =
                        forge::ui::cpp_source_coordinate(expected, expected.rfind("value"));
                    check(invoke("code.read", {{"path", "Native/First.cpp"}}).at("cursor") ==
                              forge::Json{{"line", expected_caret.first + 1},
                                          {"column", expected_caret.second + 1}},
                          "Quick-fix alternative used another row's caret mapping");
                    editor.undo(false);
                } else
                    check(analysis.at("complete").get<bool>() &&
                              analysis.at("total").get<std::size_t>() == 1 &&
                              !analysis.at("items").at(0).at("available").get<bool>() &&
                              !analysis.at("items").at(0).at("reason").get<std::string>().empty(),
                          "Unsafe quick fix was available or lost its reason");
                check(invoke("code.read", {{"path", "Native/First.cpp"}}) == observed,
                      "Quick-fix query/rejection changed source/selection/caret/history");
            }
            SDL_UnsetEnvironmentVariable(environment, "FORGE_SEMANTIC_FIXTURE_CASE");
            editor.undo(false);
            check(editor.active_text() == draft, "Quick-fix checks lost preceding source history");
        }
        {
            const auto source =
                draft + "\n// café\nint diagnostic_probe() { int value = 1 return value; }\n";
            editor.replace_text(source);
            const auto first =
                forge::ui::cpp_source_coordinate(source, source.find("degrees_per_second"));
            const auto last = forge::ui::cpp_source_coordinate(source, source.find("1 return") + 2);
            const auto cursor = [&](const auto& point) {
                return forge::Json{{"line", point.first + 1}, {"column", point.second + 1}};
            };
            const auto move = [&](const forge::Json& operation, const char* direction) {
                const auto observed = invoke("code.read", {{"path", editor.active_source()}});
                return invoke("code.analysis.diagnostic_move", {{"path", observed.at("path")},
                                                                {"session", observed.at("session")},
                                                                {"state", observed.at("state")},
                                                                {"operation", operation},
                                                                {"direction", direction}});
            };
            for (const auto* mode : {"", "diagnostic-disordered", "diagnostic-duplicate"}) {
                SDL_SetEnvironmentVariable(environment, "FORGE_SEMANTIC_FIXTURE_CASE", mode, true);
                editor.open(files.document, "Native/First.cpp", 1, 1);
                start_analysis("diagnostics");
                analysis = settle_analysis();
                check(analysis.at("complete").get<bool>(), "Diagnostic navigation query failed");
                const auto operation = analysis.at("operation");
                check(move(operation, "next").at("cursor") == cursor(first),
                      "Next diagnostic did not choose the nearest first source location");
                check(move(operation, "next").at("cursor") == cursor(last),
                      "Next diagnostic repeated an equal-position diagnostic");
                check(move(operation, "next").at("cursor") == cursor(first),
                      "Next diagnostic did not wrap at the end");
                check(move(operation, "previous").at("cursor") == cursor(last),
                      "Previous diagnostic did not wrap at the start");
                check(move(operation, "previous").at("cursor") == cursor(first),
                      "Previous diagnostic did not choose the preceding source location");
                const auto before = invoke("code.read", {{"path", "Native/First.cpp"}});
                for (const auto* direction : {"other", ""}) {
                    bool rejected = false;
                    try {
                        move(operation, direction);
                    } catch (const std::exception&) {
                        rejected = true;
                    }
                    check(rejected && invoke("code.read", {{"path", "Native/First.cpp"}}) == before,
                          "Invalid diagnostic direction changed the source or caret");
                }
                bool wrong_operation = false;
                try {
                    move(forge::AssetId::generate().str(), "next");
                } catch (const std::exception&) {
                    wrong_operation = true;
                }
                check(wrong_operation &&
                          invoke("code.read", {{"path", "Native/First.cpp"}}) == before,
                      "Wrong diagnostic operation navigated source");
                editor.open(files.document, "Native/Second.hpp");
                check(move(operation, "previous").at("cursor") == cursor(last) &&
                          editor.active_source() == "Native/First.cpp",
                      "Previous diagnostic from another source did not choose the final location");
                const auto back = invoke("code.navigation.status", forge::Json::object());
                invoke("code.navigation.move", {{"path", back.at("source").at("path")},
                                                {"session", back.at("source").at("session")},
                                                {"state", back.at("source").at("state")},
                                                {"navigation_revision", back.at("revision")},
                                                {"direction", "back"}});
                check(editor.active_source() == "Native/Second.hpp",
                      "Diagnostic move bypassed source Back history");
                check(move(operation, "next").at("cursor") == cursor(first) &&
                          editor.active_source() == "Native/First.cpp",
                      "Next diagnostic from another source did not choose the first location");
                check(editor.active_text() == source, "Diagnostic navigation changed source text");
            }
            SDL_SetEnvironmentVariable(environment, "FORGE_SEMANTIC_FIXTURE_CASE",
                                       "diagnostic-empty", true);
            start_analysis("diagnostics");
            analysis = settle_analysis();
            const auto empty_source = invoke("code.read", {{"path", "Native/First.cpp"}});
            bool empty_rejected = false;
            try {
                move(analysis.at("operation"), "next");
            } catch (const std::exception&) {
                empty_rejected = true;
            }
            check(empty_rejected &&
                      invoke("code.read", {{"path", "Native/First.cpp"}}) == empty_source,
                  "Empty diagnostics navigated source");
            SDL_UnsetEnvironmentVariable(environment, "FORGE_SEMANTIC_FIXTURE_CASE");
            editor.undo(false);
            check(editor.active_text() == draft,
                  "Diagnostic navigation added a source Undo record");
        }
        {
            const std::string source =
                "// café\n\tfloat speed = 1; // speed\nfloat speed_copy = speed;\n";
            editor.replace_text(source);
            editor.open(files.document, "Native/First.cpp", 1, 1);
            const auto arguments = [&] {
                const auto observed = invoke("code.read", {{"path", "Native/First.cpp"}});
                return forge::Json{{"path", observed.at("path")},
                                   {"session", observed.at("session")},
                                   {"state", observed.at("state")}};
            };
            const auto find = [&](const std::string& query, const char* direction) {
                auto args = arguments();
                args.update({{"query", query}, {"direction", direction}});
                return invoke("code.find", args);
            };
            const auto replace = [&](const std::string& query, const std::string& value,
                                     const char* mode) {
                auto args = arguments();
                args.update({{"query", query}, {"replacement", value}, {"mode", mode}});
                return invoke("code.replace_matches", args);
            };
            const auto first = source.find("speed"), last = source.rfind("speed");
            check(find("speed", "next").at("begin").get<std::size_t>() == first,
                  "Literal find did not select its first UTF8/tab-aware match");
            check(find("speed", "previous").at("begin").get<std::size_t>() == last,
                  "Previous literal find did not wrap from the selected match");
            check(find("speed", "next").at("begin").get<std::size_t>() == first,
                  "Next literal find did not wrap to the first match");
            auto changed = replace("speed", "rate", "next");
            auto expected = source;
            expected.replace(first, 5, "rate");
            check(changed.at("matches").get<std::size_t>() == 1 &&
                      changed.at("changed").get<bool>() && editor.active_text() == expected,
                  "Replace next skipped the widget's selected literal match");
            editor.undo(false);
            check(editor.active_text() == source, "Literal replacement was not one Undo");
            editor.undo(true);
            check(editor.active_text() == expected, "Literal replacement Redo failed");
            editor.undo(false);
            const auto caret = forge::ui::cpp_source_coordinate(source, last + 2);
            editor.open(files.document, "Native/First.cpp", caret.first + 1, caret.second + 1);
            changed = replace("speed", "vitesse_é", "all");
            expected = source;
            for (std::size_t from = 0, match;
                 (match = expected.find("speed", from)) != expected.npos;) {
                expected.replace(match, 5, "vitesse_é");
                from = match + std::string("vitesse_é").size();
            }
            const auto mapped = forge::ui::cpp_source_coordinate(
                expected, expected.rfind("vitesse_é") + std::string("vitesse_é").size());
            check(changed.at("matches").get<std::size_t>() == 4 &&
                      editor.active_text() == expected &&
                      changed.at("source").at("cursor") ==
                          forge::Json{{"line", mapped.first + 1}, {"column", mapped.second + 1}},
                  "Replace all lost its Unicode result, match count or current-caret mapping");
            editor.undo(false);
            check(editor.active_text() == source, "Replace all required multiple Undo steps");
            const auto before_noop = invoke("code.read", {{"path", "Native/First.cpp"}});
            check(!replace("speed", "speed", "all").at("changed").get<bool>() &&
                      !replace("absent", "value", "all").at("changed").get<bool>() &&
                      !find("absent", "next").at("found").get<bool>() &&
                      invoke("code.read", {{"path", "Native/First.cpp"}}) == before_noop,
                  "No-op or absent literal operation changed source/caret/history");
            for (const auto& query : {std::string{}, std::string(256, 'x'), std::string("a\nb"),
                                      std::string("bad\xFF", 4)}) {
                bool rejected = false;
                try {
                    replace(query, "x", "all");
                } catch (const std::exception&) {
                    rejected = true;
                }
                check(rejected &&
                          invoke("code.read", {{"path", "Native/First.cpp"}}) == before_noop,
                      "Invalid literal query partially changed source");
            }
            editor.undo(false);
            check(editor.active_text() == draft,
                  "Literal operations lost preceding source history");
            for (const auto& [input, query, value, expected, count] : std::vector<
                     std::tuple<std::string, std::string, std::string, std::string, std::size_t>>{
                     {"aaaaa\n", "aa", "a", "aaa\n", 2},
                     {"go go\n", "go", "go go", "go go go go\n", 2},
                     {"// café\n\tgo go\n", "go", "é\n\t🚀", "// café\n\té\n\t🚀 é\n\t🚀\n", 2},
                     {"go go\n", "go", "", " \n", 2},
                     {"go\n", "go", "", "\n", 1},
                     {"ééé\n", "éé", "🚀", "🚀é\n", 1},
                     {std::string(32768, 'x') + "\n", "x", "z", std::string(32768, 'z') + "\n",
                      32768},
                     {std::string(255, 'x') + "\n", std::string(255, 'x'), "ok", "ok\n", 1},
                     {"go\n", "go", std::string(4096, 'x'), std::string(4096, 'x') + "\n", 1}}) {
                editor.replace_text(input);
                const auto result = replace(query, value, "all");
                check(result.at("matches").get<std::size_t>() == count &&
                          editor.active_text() == expected,
                      "Literal bulk replacement lost non-overlap, UTF8/LF, empty or boundary text");
                editor.undo(false);
                check(editor.active_text() == input,
                      "Boundary literal replacement did not restore one source Undo");
                editor.undo(true);
                check(editor.active_text() == expected, "Boundary literal Redo was not exact");
                editor.undo(false);
                editor.undo(false);
                check(editor.active_text() == draft,
                      "Boundary literal replacement damaged preceding source history");
            }
            editor.replace_text("go\n");
            const auto before_invalid = invoke("code.read", {{"path", "Native/First.cpp"}});
            for (const auto& replacement : {std::string(4097, 'x'), std::string("\r\n"),
                                            std::string("a\0b", 3), std::string("bad\xFF", 4)}) {
                bool rejected = false;
                try {
                    replace("go", replacement, "all");
                } catch (const std::exception&) {
                    rejected = true;
                }
                check(rejected &&
                          invoke("code.read", {{"path", "Native/First.cpp"}}) == before_invalid,
                      "Invalid replacement partially changed source/caret/history");
            }
            editor.undo(false);
            for (const auto& input :
                 {std::string(32769, 'x') + "\n", std::string(1024, 'x') + "\n"}) {
                editor.replace_text(input);
                const auto before_limit = invoke("code.read", {{"path", "Native/First.cpp"}});
                bool rejected = false;
                try {
                    replace("x", input.size() > 32768 ? "z" : std::string(4096, 'z'), "all");
                } catch (const std::exception&) {
                    rejected = true;
                }
                check(rejected &&
                          invoke("code.read", {{"path", "Native/First.cpp"}}) == before_limit,
                      "Literal match/output budget allowed partial replacement");
                editor.undo(false);
                check(editor.active_text() == draft,
                      "Rejected literal replacement changed the source Undo history");
            }
        }
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
