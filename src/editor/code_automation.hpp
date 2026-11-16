#pragma once
#include "../asset_bytes.hpp"
#include "../utf8_boundary.hpp"
#include "authored_components.hpp"
#include "automation_permissions.hpp"
#include "automation_schema.hpp"
#include "cpp_source_editor.hpp"
#include "files.hpp"
#include "sdk_build.hpp"

namespace forge {
// Providers borrow the editor's currently selected exact kit/build owner across
// approved project switches. They are not another build or source manager.
struct CodeAutomationProviders {
    std::function<SdkBuild&()> build;
    std::function<std::filesystem::path()> sdk;
    std::function<std::pair<std::string, std::string>()> compiler_tools;
    std::function<std::string()> unavailable;
};
struct CodeAutomation {
    static std::string hash(std::string_view value) { return ui::cpp_source_state(value); }
    static Json text_page(const std::string& text, const Json& request) {
        auto start = std::min(request.value("offset", std::size_t{}), text.size());
        const auto continuation = [&](std::size_t i) {
            return (static_cast<unsigned char>(text[i]) & 0xc0) == 0x80;
        };
        while (start < text.size() && continuation(start))
            ++start;
        const auto end =
            detail::utf8_range_end(text, start, start + request.value("limit", std::size_t(65536)));
        const auto safe = automation_text_value(text.substr(start, end - start));
        return {
            {"text", safe}, {"offset", start}, {"next_offset", end}, {"total_bytes", text.size()}};
    }
    static void available(EditorFiles& files, const CodeAutomationProviders& providers) {
        files.document.check_ownership();
        if (providers.unavailable) {
            const auto reason = providers.unavailable();
            if (!reason.empty())
                throw AuthoringError("code_busy", reason);
        }
    }
    static SdkBuild& builder(EditorFiles& files, const CodeAutomationProviders& providers) {
        auto& build = providers.build();
        if (build.project_ != files.document.project())
            throw AuthoringError("not_ready", "Wait for the current project's build controller");
        return build;
    }
    static Json tab_state(const ui::CppSourceEditor::Tab& tab) {
        const auto cursor = tab.editor.GetCursorPosition();
        const auto selection = tab.editor.GetSelectedText();
        return {{"path", tab.locator},
                {"session", tab.session},
                {"state", hash(tab.editor.GetText())},
                {"dirty", tab.dirty},
                {"external", tab.external},
                {"cursor", {{"line", cursor.mLine + 1}, {"column", cursor.mColumn + 1}}},
                {"selection",
                 {{"active", tab.editor.HasSelection()},
                  {"bytes", selection.size()},
                  {"state", hash(selection)}}},
                {"can_undo", tab.editor.CanUndo()},
                {"can_redo", tab.editor.CanRedo()}};
    }
    static void observe_tab(const ui::CppSourceEditor::Tab& source, const Json& request) {
        if (request.at("session") != source.session ||
            request.at("state") != hash(source.editor.GetText()))
            throw AuthoringError("stale_source", "C++ source draft/session changed; read it again");
    }
    static Json navigation_state(const ui::CppSourceEditor& editor,
                                 const std::filesystem::path& project) {
        Json result{{"revision", nullptr},       {"can_back", false},  {"can_forward", false},
                    {"back_count", 0},           {"forward_count", 0}, {"back_target", nullptr},
                    {"forward_target", nullptr}, {"source", nullptr}};
        if (editor.root_ != project)
            return result;
        const auto target = [](const auto& locations) -> Json {
            if (locations.empty())
                return nullptr;
            const auto& value = locations.back();
            return {{"path", value.path}, {"line", value.line}, {"column", value.column}};
        };
        result.update({{"revision", editor.navigation_revision_},
                       {"can_back", editor.can_navigate(false)},
                       {"can_forward", editor.can_navigate(true)},
                       {"back_count", editor.back_locations_.size()},
                       {"forward_count", editor.forward_locations_.size()},
                       {"back_target", target(editor.back_locations_)},
                       {"forward_target", target(editor.forward_locations_)},
                       {"source", editor.active_ ? tab_state(*editor.active_) : Json{}}});
        return result;
    }
    static ui::CppSourceEditor::Tab& tab(ui::CppSourceEditor& editor, EditorFiles& files,
                                         const Json& request) {
        files.document.check_ownership();
        const auto path = automation_relative_path(request.at("path").get<std::string>());
        (void)ui::cpp_source_path(files.document.project(), path);
        auto* source =
            editor.root_ == files.document.project() ? editor.find(path_utf8(path)) : nullptr;
        if (!source)
            throw AuthoringError("not_open", "Open this C++ source in the shared editor first");
        observe_tab(*source, request);
        if (editor.close_pending())
            throw AuthoringError("code_busy", "Settle the pending source close request");
        return *source;
    }
    static void source_ready(const ui::CppSourceEditor& editor) {
        if (editor.close_pending())
            throw AuthoringError("code_busy", "Settle the pending source close request");
    }
    static std::filesystem::path sdk(const CodeAutomationProviders& providers) {
        const auto root = std::filesystem::canonical(providers.sdk());
        SdkBuild::check_sdk(root);
        return root;
    }
    static Json build_state(EditorFiles& files, const CodeAutomationProviders& providers) {
        auto& build = builder(files, providers);
        return {{"managed", build.managed()},
                {"busy", build.busy()},
                {"operation", build.operation_id_},
                {"testing", build.testing()},
                {"compiler_ready", build.compiler_ready()},
                {"status", build.status()},
                {"error", build.error()},
                {"log_revision", build.log_revision()}};
    }
    static Json search_state(EditorFiles& files, ui::CppSourceEditor& editor, const Json& request) {
        files.document.check_ownership();
        editor.poll_project_search();
        if (editor.search_project_ != files.document.project())
            return {{"operation", nullptr},
                    {"busy", false},
                    {"matches", Json::array()},
                    {"complete", false},
                    {"total", 0}};
        Json result{{"operation", editor.search_operation_},
                    {"busy", editor.project_search_.busy()},
                    {"query", editor.search_options_.query},
                    {"match_case", editor.search_options_.match_case},
                    {"whole_word", editor.search_options_.whole_word},
                    {"complete", false},
                    {"total", 0},
                    {"matches", Json::array()}};
        if (!editor.project_matches_)
            return result;
        const auto& found = *editor.project_matches_;
        result.update({{"complete", found.complete()},
                       {"cancelled", found.cancelled},
                       {"limited", found.limited},
                       {"skipped", found.skipped},
                       {"errors", found.errors},
                       {"files", found.files},
                       {"bytes", found.bytes},
                       {"total", found.matches.size()}});
        const auto offset = request.value("offset", std::size_t{}),
                   limit = request.value("limit", std::size_t(128));
        for (std::size_t i = offset; i < found.matches.size() && i - offset < limit; ++i) {
            const auto& match = found.matches[i];
            result["matches"].push_back({{"path", match.path},
                                         {"state", match.state},
                                         {"line", match.line},
                                         {"column", match.column},
                                         {"offset", match.offset},
                                         {"length", match.length},
                                         {"excerpt", automation_text_value(match.excerpt)},
                                         {"open", match.draft}});
        }
        return result;
    }
    static std::vector<LiveAuthoringMethod>
    methods(Scene& scene, EditorFiles& files, ui::CppSourceEditor& editor,
            AuthoredComponents& authored, PrefabEditor& prefab, CodeAutomationProviders providers,
            const ui::AutomationPermissions& permissions) {
        std::vector<LiveAuthoringMethod> methods;
        methods.push_back(automation_method("code.status",
                                            "Read the actual C++ source tabs and managed build "
                                            "owner status. Source files have no AssetIds.",
                                            "code.read", automation_object(), false,
                                            [&files, &editor, providers](const Json&) {
                                                files.document.check_ownership();
                                                Json tabs = Json::array();
                                                if (editor.root_ == files.document.project())
                                                    for (const auto& tab : editor.tabs_)
                                                        tabs.push_back(tab_state(*tab));
                                                auto status = build_state(files, providers);
                                                status["tabs"] = std::move(tabs);
                                                return status;
                                            }));
        methods.push_back(automation_method(
            "code.sources",
            "Page the same supported Native source/header discovery used by Content.", "code.read",
            automation_object(
                {{"offset", automation_integer()}, {"limit", automation_integer(1, 128)}}),
            false, [&files](const Json& request) {
                files.document.check_ownership();
                const auto sources = ui::cpp_project_sources(files.document.project());
                Json page = Json::array();
                const auto offset = request.value("offset", std::size_t{}),
                           limit = request.value("limit", std::size_t(128));
                for (std::size_t i = offset; i < sources.size() && i - offset < limit; ++i)
                    page.push_back(sources[i]);
                return Json{{"sources", std::move(page)}, {"total", sources.size()}};
            }));
        methods.push_back(automation_method(
            "code.search.start",
            "Start background literal search of supported Native files and open draft snapshots. "
            "No files, history or editor focus change. One job; at most 32 MiB and 512 matches. "
            "ASCII case folding, UTF-8 exact non-ASCII text; no regular expressions. Poll "
            "code.search.status; incomplete results never claim a full search.",
            "code.read",
            automation_object({{"query", automation_text(255, 1)},
                               {"match_case", {{"type", "boolean"}}},
                               {"whole_word", {{"type", "boolean"}}}},
                              {"query"}),
            false, [&files, &editor](const Json& request) {
                editor.search_project(files.document,
                                      {request.at("query").get<std::string>(),
                                       request.value("match_case", true),
                                       request.value("whole_word", false)},
                                      false);
                return search_state(files, editor, Json::object());
            }));
        methods.back().idempotent = false;
        methods.push_back(
            automation_method("code.search.status",
                              "Page current search snapshots. Use each result's path, state, "
                              "line and column with code.open; rerun after source changes.",
                              "code.read",
                              automation_object({{"offset", automation_integer()},
                                                 {"limit", automation_integer(1, 128)}}),
                              false, [&files, &editor](const Json& request) {
                                  return search_state(files, editor, request);
                              }));
        methods.push_back(automation_method(
            "code.search.cancel",
            "Cancel only the named background source-search job. No source "
            "or build changes. Poll status for any partial results.",
            "code.read", automation_object({{"operation", automation_uuid()}}, {"operation"}),
            false, [&files, &editor](const Json& request) {
                files.document.check_ownership();
                if (editor.search_project_ != files.document.project() ||
                    request.at("operation") != editor.search_operation_)
                    throw AuthoringError("wrong_job",
                                         "Search belongs to another operation/project");
                editor.project_search_.cancel();
                return search_state(files, editor, Json::object());
            }));
        methods.push_back(automation_method(
            "code.read",
            "Read an open C++ draft or a contained Native source file. Text is paged by UTF-8 "
            "bytes; use the returned tab session/digest for edits.",
            "code.read",
            automation_object({{"path", automation_text(2047, 1)},
                               {"offset", automation_integer()},
                               {"limit", automation_integer(1, 262144)}},
                              {"path"}),
            false, [&files, &editor](const Json& request) {
                files.document.check_ownership();
                const auto path = automation_relative_path(request.at("path").get<std::string>());
                const auto file = ui::cpp_source_path(files.document.project(), path);
                const auto* source = editor.root_ == files.document.project()
                                         ? editor.find(path_utf8(path))
                                         : nullptr;
                const auto text = source ? source->editor.GetText() : ui::read_cpp_source(file);
                Json result = text_page(text, request);
                result["path"] = path_utf8(path);
                result["open"] = bool(source);
                if (source)
                    result.update(tab_state(*source));
                else
                    result["state"] = hash(text);
                return result;
            }));
        methods.push_back(automation_method(
            "code.build.log", "Read the existing timestamped compiler log in bounded UTF-8 pages.",
            "code.read",
            automation_object(
                {{"offset", automation_integer()}, {"limit", automation_integer(1, 262144)}}),
            false, [&files, providers](const Json& request) {
                files.document.check_ownership();
                auto& build = builder(files, providers);
                auto result = text_page(build.timed_log(), request);
                result["operation"] = build.operation_id_;
                result["revision"] = build.log_revision();
                return result;
            }));
        methods.push_back(automation_method(
            "code.navigation.status",
            "Read current C++ Back/Forward revision, counts, next locations and active tab "
            "witness. "
            "No tool launch, source edit, file read or history mutation.",
            "code.read", automation_object(), false, [&files, &editor](const Json&) {
                files.document.check_ownership();
                return navigation_state(editor, files.document.project());
            }));
        methods.push_back(automation_method(
            "code.analysis.status",
            "Read the current C++ analysis result in bounded pages. This never launches tooling; "
            "Results belong to the exact queried drafts and compiler context. Call signatures "
            "are read-only; active indices refer to the full result, before pagination. "
            "Diagnostics carry receive date/time, severity, message and a guarded source location. "
            "Symbol information is read-only; file symbols provide a bounded hierarchical outline. "
            "Formatting returns one complete current-source proposal, including its edit count and "
            "formatted text. Use code.analysis.use to apply completion/formatting or navigate a "
            "declaration/definition/implementation/diagnostic/symbol/reference/type. Reference and "
            "implementation scope is the current compiler "
            "context, never an exhaustive project index. Syntax ranges are bounded containing "
            "extents; use explicitly selects one in the existing source widget.",
            "code.read",
            automation_object(
                {{"offset", automation_integer()}, {"limit", automation_integer(1, 8)}}),
            false, [&files, &editor](const Json& request) {
                files.document.check_ownership();
                editor.poll_semantics();
                if (editor.semantic_task_.project() != files.document.project())
                    return Json{{"operation", nullptr},
                                {"busy", false},
                                {"complete", false},
                                {"total", 0},
                                {"items", Json::array()}};
                auto result = editor.semantic_task_.state();
                result["items"] = Json::array();
                const auto& response = editor.semantic_task_.result();
                if (!response.is_null()) {
                    if (response.value("readonly", false))
                        result["readonly"] = true;
                    if (response.contains("occurred_at"))
                        result["occurred_at"] = response.at("occurred_at");
                    if (response.contains("scope")) {
                        result["scope"] = response.at("scope");
                        result["exhaustive"] = response.at("exhaustive");
                    }
                    if (response.at("kind").get<std::string>() == "signature") {
                        result["active_signature"] = response.at("active_signature");
                        result["active_parameter"] = response.at("active_parameter");
                    }
                    const auto& items = response.at("items");
                    const auto offset = request.value("offset", std::size_t{});
                    const auto limit = request.value("limit", std::size_t(8));
                    for (std::size_t i = offset; i < items.size() && i - offset < limit; ++i) {
                        auto item = items.at(i);
                        item["index"] = i;
                        result["items"].push_back(std::move(item));
                    }
                }
                return result;
            }));
        methods.push_back(automation_method(
            "code.analysis.log", "Read the current timestamped C++ analysis log; no tool launch.",
            "code.read",
            automation_object(
                {{"offset", automation_integer()}, {"limit", automation_integer(1, 262144)}}),
            false, [&files, &editor](const Json& request) {
                files.document.check_ownership();
                editor.poll_semantics();
                if (editor.semantic_task_.project() != files.document.project())
                    return Json{{"operation", nullptr}, {"text", ""}, {"total_bytes", 0}};
                auto result = text_page(editor.semantic_task_.log(), request);
                result["operation"] = editor.semantic_task_.operation();
                return result;
            }));
        methods.push_back(automation_method(
            "code.api.list",
            "List installed public headers, templates, examples and guides from the "
            "editor-selected matching SDK. No engine repository paths are exposed.",
            "code.read",
            automation_object(
                {{"offset", automation_integer()}, {"limit", automation_integer(1, 128)}}),
            false, [providers](const Json& request) {
                const auto root = sdk(providers);
                std::vector<std::string> paths;
                const ProjectPaths installed(root);
                std::size_t visited = 0;
                for (const auto* directory : {"include", "template", "sample", "docs"}) {
                    asset_storage::ordinary(root / "sdk" / directory);
                    const auto base = installed.resolve(std::filesystem::path("sdk") / directory);
                    if (!std::filesystem::is_directory(base))
                        continue;
                    for (std::filesystem::recursive_directory_iterator it(base), end; it != end;
                         ++it) {
                        if (it->is_symlink()) {
                            it.disable_recursion_pending();
                            continue;
                        }
                        if (++visited > 10000 || paths.size() >= 8192 || it.depth() > 32)
                            throw AuthoringError("sdk_limit",
                                                 "SDK source listing exceeds its bound");
                        (void)installed.relative(it->path());
                        const auto extension = it->path().extension();
                        if (it->is_regular_file() &&
                            (ui::cpp_source_extension(it->path()) || extension == ".md"))
                            paths.push_back(path_utf8(it->path().lexically_relative(root / "sdk")));
                    }
                }
                std::sort(paths.begin(), paths.end());
                Json page = Json::array();
                const auto offset = request.value("offset", std::size_t{}),
                           limit = request.value("limit", std::size_t(128));
                for (std::size_t i = offset; i < paths.size() && i - offset < limit; ++i)
                    page.push_back(paths[i]);
                return Json{{"files", std::move(page)},
                            {"total", paths.size()},
                            {"build", read_json(root / "build.json")}};
            }));
        methods.push_back(automation_method(
            "code.api.read",
            "Read an installed public SDK header/example/guide from the exact selected kit. Use "
            "only paths advertised by code.api.list.",
            "code.read",
            automation_object({{"path", automation_text(2047, 1)},
                               {"offset", automation_integer()},
                               {"limit", automation_integer(1, 262144)}},
                              {"path"}),
            false, [providers](const Json& request) {
                const auto root = sdk(providers);
                const auto path = automation_relative_path(request.at("path").get<std::string>());
                const auto first = path.begin()->string();
                if ((first != "include" && first != "template" && first != "sample" &&
                     first != "docs") ||
                    (!ui::cpp_source_extension(path) && path.extension() != ".md"))
                    throw AuthoringError("invalid_path",
                                         "Choose an advertised public SDK source/header/guide");
                asset_storage::ordinary(root / "sdk" / path);
                const auto file = ProjectPaths(root).resolve(std::filesystem::path("sdk") / path);
                const auto bytes = asset_detail::read_bytes(file, 2 * 1024 * 1024);
                const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                auto result = text_page(text, request);
                result["path"] = path_utf8(path);
                return result;
            }));
        if (permissions.code) {
            methods.push_back(automation_method(
                "code.setup",
                "Create the managed C++ starter through the existing exact-SDK controller, without "
                "overwriting Native files. This writes source/configuration but does not build or "
                "run game code.",
                "code.write", automation_object(), true, [&files, providers](const Json&) {
                    available(files, providers);
                    auto& build = builder(files, providers);
                    if (build.busy())
                        throw AuthoringError("code_busy", "Wait for the native task");
                    build.create(files.document, sdk(providers));
                    return build_state(files, providers);
                }));
            methods.push_back(automation_method(
                "code.create",
                "Create a managed Component, System, source or header with the existing Content "
                "helpers and registration rules, then open it in C++ Sources. Omit a System's "
                "component for an empty registered startup function for queries or observers.",
                "code.write",
                automation_object(
                    {{"kind",
                      {{"type", "string"}, {"enum", {"component", "system", "source", "header"}}}},
                     {"name", automation_text(120, 1)},
                     {"component", automation_text(120, 1)}},
                    {"kind", "name"}),
                true, [&files, &editor, providers](const Json& request) {
                    available(files, providers);
                    source_ready(editor);
                    const auto kind = request.at("kind").get<std::string>(),
                               name = request.at("name").get<std::string>();
                    if (kind != "system" && request.contains("component"))
                        throw AuthoringError("invalid_argument", "Only a system takes a component");
                    std::string path;
                    if (kind == "component")
                        path = ui::create_cpp_component(files.document.project(), name);
                    else if (kind == "system") {
                        path = ui::create_cpp_system(files.document.project(), name,
                                                     request.value("component", std::string{}));
                    } else {
                        if ((kind == "header" && !name.ends_with(".hpp")) ||
                            (kind == "source" && !name.ends_with(".cpp")))
                            throw AuthoringError("invalid_argument",
                                                 "Use .hpp for a header or .cpp for a source file");
                        ui::create_cpp_source(files.document.project(), name);
                        path = "Native/" + name;
                    }
                    editor.open(files.document, path);
                    return tab_state(*editor.active_);
                }));
            methods.push_back(automation_method(
                "code.open",
                "Open a contained Native source in the actual C++ editor, preserving existing "
                "dirty tabs and source Undo history. Optional one-based visual line/column "
                "navigate "
                "search results. Supply their state digest to reject stale text before navigation.",
                "code.write",
                automation_object({{"path", automation_text(2047, 1)},
                                   {"line", automation_integer(1, 1048576)},
                                   {"column", automation_integer(1, 4194304)},
                                   {"state", automation_digest()}},
                                  {"path"}),
                true, [&files, &editor, providers](const Json& request) {
                    available(files, providers);
                    source_ready(editor);
                    const auto path =
                        automation_relative_path(request.at("path").get<std::string>());
                    try {
                        editor.open(files.document, path_utf8(path), request.value("line", 1),
                                    request.value("column", 1),
                                    request.value("state", std::string{}));
                    } catch (const ui::CppSourceChanged& error) {
                        throw AuthoringError("stale_source", error.what());
                    }
                    return tab_state(*editor.active_);
                }));
            methods.push_back(automation_method(
                "code.navigation.move",
                "Move Back or Forward through the observed C++ navigation revision and active "
                "tab witness. Current Native drafts/history are retained; edited-text positions "
                "are hints clamped to valid current coordinates. Closed files reopen from disk. "
                "No source Undo, Save, build or provider launch.",
                "code.write",
                automation_object(
                    {{"path", automation_text(2047, 1)},
                     {"session", automation_uuid()},
                     {"state", automation_digest()},
                     {"navigation_revision", automation_integer()},
                     {"direction", {{"type", "string"}, {"enum", {"back", "forward"}}}}},
                    {"path", "session", "state", "navigation_revision", "direction"}),
                true, [&files, &editor, providers](const Json& request) {
                    available(files, providers);
                    source_ready(editor);
                    if (editor.root_ != files.document.project() || !editor.active_ ||
                        automation_relative_path(request.at("path").get<std::string>()) !=
                            std::filesystem::u8path(editor.active_->locator))
                        throw AuthoringError("stale_source",
                                             "Active C++ tab changed; read navigation again");
                    // Leaving a cached draft is valid even if its disk file was deleted.
                    // The target still uses the source owner's guarded open/read path.
                    observe_tab(*editor.active_, request);
                    const auto direction = request.at("direction").get<std::string>();
                    if (direction != "back" && direction != "forward")
                        throw AuthoringError("invalid_direction", "Choose Back or Forward");
                    editor.navigate(files.document, direction == "forward",
                                    request.at("navigation_revision").get<std::uint64_t>());
                    return navigation_state(editor, files.document.project());
                }));
            methods.push_back(automation_method(
                "code.navigation.clear",
                "Clear the observed C++ navigation revision only. Source text, Undo, tabs and "
                "files remain owned by the source editor; no build or provider launch.",
                "code.write",
                automation_object({{"navigation_revision", automation_integer()}},
                                  {"navigation_revision"}),
                true, [&files, &editor, providers](const Json& request) {
                    available(files, providers);
                    editor.clear_navigation(files.document,
                                            request.at("navigation_revision").get<std::uint64_t>());
                    return navigation_state(editor, files.document.project());
                }));
            methods.push_back(automation_method(
                "code.analysis.use",
                "Use an available result of the exact analysis operation. Completion changes "
                "the existing source tab with one Undo record. Formatting applies one complete "
                "proposal and maps the current target cursor; declaration/diagnostic opens only a "
                "contained "
                "Native source. Definitions, implementations, file symbols, references and type "
                "definitions use the same "
                "navigation; signatures/symbol "
                "information "
                "are read-only. Syntax ranges select the existing widget without source edits.  "
                "Reject changed drafts, disk inputs or "
                "compiler generation. "
                "Does not save files or execute provider commands.",
                "code.write",
                automation_object(
                    {{"operation", automation_uuid()}, {"index", automation_integer(0, 63)}},
                    {"operation", "index"}),
                true, [&files, &editor, providers](const Json& request) {
                    available(files, providers);
                    source_ready(editor);
                    editor.use_semantic_result(files.document,
                                               request.at("operation").get<std::string>(),
                                               request.at("index").get<std::size_t>());
                    return tab_state(*editor.active_);
                }));
            for (const auto* action : {"replace", "save", "undo", "redo", "close"}) {
                Json properties{{"path", automation_text(2047, 1)},
                                {"session", automation_uuid()},
                                {"state", automation_digest()}};
                Json required{"path", "session", "state"};
                if (std::string_view(action) == "replace") {
                    properties["text"] = automation_text(ui::max_cpp_source_bytes - 1);
                    required.push_back("text");
                }
                if (std::string_view(action) == "close")
                    properties["discard"] = {{"type", "boolean"}};
                methods.push_back(automation_method(
                    std::string("code.") + action,
                    std::string(action) +
                        " the actual C++ source tab at its observed session/content digest. "
                        "Replacement shares the widget's single Undo record and never writes the "
                        "OS clipboard. Save uses its disk baseline; ungranted execution never "
                        "queues Build on Save.",
                    "code.write", automation_object(std::move(properties), std::move(required)),
                    true,
                    [&files, &editor, providers, execute = permissions.execute,
                     action](const Json& request) {
                        available(files, providers);
                        auto& source = tab(editor, files, request);
                        const auto operation = std::string_view(action);
                        if (operation == "close") {
                            if (source.dirty && !request.value("discard", false))
                                throw AuthoringError("unsaved_source",
                                                     "Save or explicitly discard this C++ draft");
                            editor.erase(source.locator);
                            return Json{{"open", false}};
                        }
                        editor.with_source_tab(source, [&] {
                            if (operation == "replace")
                                editor.replace_text(request.at("text").get<std::string>());
                            else if (operation == "save")
                                editor.save(files.document, execute);
                            else if (operation == "undo" || operation == "redo") {
                                if (!(operation == "undo" ? source.editor.CanUndo()
                                                          : source.editor.CanRedo()))
                                    throw AuthoringError("history_empty",
                                                         "No source history in this direction");
                                editor.undo(operation == "redo");
                            }
                        });
                        editor.select_tab_next_frame(source);
                        editor.activate_source(source);
                        return tab_state(source);
                    }));
            }
        }
        if (permissions.execute) {
            methods.push_back(automation_method(
                "code.analysis.start",
                "Query completion, declaration, call signatures, diagnostics, symbol information "
                "file symbols, definitions, current-context implementations and references, type "
                "definitions, syntax selection or reviewed "
                "whole-source formatting at "
                "a UTF-8 "
                "byte offset in the observed open "
                "C++ tab, using all current open drafts and the existing configured CMake context. "
                "Requires optional clangd 23.1.0 and a project build this session. Optional tool "
                "overrides only this launch. Tooling runs with this account's OS access; no "
                "provider command or workspace edit is accepted. Poll code.analysis.status/log.",
                "code.execute",
                automation_object({{"path", automation_text(2047, 1)},
                                   {"session", automation_uuid()},
                                   {"state", automation_digest()},
                                   {"kind", {{"type", "string"}, {"enum", cpp_semantic_kinds}}},
                                   {"offset", automation_integer(0, ui::max_cpp_source_bytes)},
                                   {"tool", automation_text(4096, 1)}},
                                  {"path", "session", "state", "kind", "offset"}),
                true, [&files, &editor, providers](const Json& request) {
                    available(files, providers);
                    const auto& source = tab(editor, files, request);
                    editor.start_semantics(files.document, source.locator,
                                           request.at("kind").get<std::string>(),
                                           request.at("offset").get<std::size_t>(),
                                           request.value("tool", std::string{}));
                    return editor.semantic_task_.state();
                }));
            methods.push_back(automation_method(
                "code.analysis.cancel", "Cancel the exact current C++ analysis query.",
                "code.execute",
                automation_object({{"operation", automation_uuid()}}, {"operation"}), true,
                [&files, &editor](const Json& request) {
                    files.document.check_ownership();
                    if (editor.semantic_task_.project() != files.document.project())
                        throw AuthoringError("wrong_job", "Select this project's analysis query");
                    editor.semantic_task_.cancel(request.at("operation").get<std::string>());
                    return editor.semantic_task_.state();
                },
                true));
            for (bool test : {false, true})
                methods.push_back(automation_method(
                    test ? "code.compiler.test" : "code.build",
                    test
                        ? "Run the existing exact-SDK compiler readiness probe in a separate "
                          "process. It does not publish project code/configuration; poll "
                          "code.status/log."
                        : "Build saved managed project code through SdkBuild and isolated "
                          "admission. Native code/build recipes run with this account's OS access. "
                          "Poll code.status/log; failed candidates retain the last good module.",
                    "code.execute", automation_object(), true,
                    [&files, &editor, &authored, providers, test](const Json&) {
                        available(files, providers);
                        source_ready(editor);
                        auto& build = builder(files, providers);
                        if (build.busy() || editor.dirty() || authored.busy() ||
                            authored.migration_open_)
                            throw AuthoringError(
                                "code_busy",
                                "Save C++ sources and settle the existing task before building");
                        const auto [cmake, ninja] = providers.compiler_tools();
                        build.build(files.document, sdk(providers), cmake, ninja, test);
                        return build_state(files, providers);
                    }));
            methods.push_back(automation_method(
                "code.build.cancel",
                "Cancel the exact pending native compiler task through its existing supervisor; "
                "published modules stay published.",
                "code.execute",
                automation_object({{"operation", automation_uuid()}}, {"operation"}), true,
                [&files, providers](const Json& request) {
                    files.document.check_ownership();
                    auto& build = builder(files, providers);
                    if (!build.busy() || request.at("operation") != build.operation_id_)
                        throw AuthoringError("wrong_job", "Select the exact active native task");
                    build.cancel();
                    return build_state(files, providers);
                },
                true));
        }
        auto reflection =
            reflection_methods(scene, files, editor, authored, prefab, providers, permissions);
        methods.insert(methods.end(), std::make_move_iterator(reflection.begin()),
                       std::make_move_iterator(reflection.end()));
        for (auto& method : methods)
            method.executes_native = method.capability == "code.execute" ||
                                     (permissions.execute && method.name == "code.save");
        return methods;
    }
    static Json migration_state(const AuthoredComponents& owner, const EditorFiles& files) {
        if (owner.root_ != files.document.project() || !owner.migration_open_)
            return {{"open", false}};
        Json choices = Json::array();
        for (std::size_t i = 0; i < owner.choices_.size(); ++i) {
            const auto [old_index, new_index] = owner.choices_[i];
            choices.push_back({{"index", i},
                               {"from", owner.prior_.at(old_index)},
                               {"to", owner.active_.at("components").at(new_index)}});
        }
        Json state{{"session", owner.migration_session_},
                   {"choice", owner.choice_},
                   {"rules", owner.rules_},
                   {"operation", owner.operation_id_},
                   {"prepared", !owner.returned_.is_null()},
                   {"generation", owner.document_generation_},
                   {"scene_revision", owner.scene_revision_}};
        return {{"open", true},
                {"session", owner.migration_session_},
                {"state", hash(state.dump())},
                {"target", owner.prefab_target_ ? "prefab" : "scene"},
                {"choices", std::move(choices)},
                {"choice", owner.choice_},
                {"rules", owner.rules_},
                {"busy", owner.busy()},
                {"operation", owner.operation_id_},
                {"prepared", !owner.returned_.is_null()},
                {"values", owner.returned_.is_null() ? std::size_t{} : owner.returned_.size()},
                {"status", owner.status_},
                {"error", owner.error_}};
    }
    static std::vector<LiveAuthoringMethod>
    reflection_methods(Scene& scene, EditorFiles& files, ui::CppSourceEditor& editor,
                       AuthoredComponents& authored, PrefabEditor& prefab,
                       CodeAutomationProviders providers,
                       const ui::AutomationPermissions& permissions) {
        std::vector<LiveAuthoringMethod> result;
        result.push_back(automation_method(
            "code.schemas",
            "Read copied metadata/status from the actual authored-component inspection owner. The "
            "editor never loads project libraries.",
            "code.read", automation_object(), false, [&files, &authored](const Json&) {
                files.document.check_ownership();
                if (authored.root_ != files.document.project())
                    return Json{{"ready", false}};
                return Json{{"ready", !authored.active_.is_null()}, {"busy", authored.busy()},
                            {"operation", authored.operation_id_},  {"schemas", authored.active_},
                            {"status", authored.status_},           {"error", authored.error_}};
            }));
        result.push_back(automation_method(
            "code.migration.read",
            "Inspect the existing scene/prefab authored-schema migration review, available schema "
            "pairs, rules and worker result status.",
            "code.read", automation_object(), false, [&files, &authored](const Json&) {
                files.document.check_ownership();
                return migration_state(authored, files);
            }));
        if (!permissions.execute)
            return result;
        result.push_back(automation_method(
            "code.inspect",
            "Run existing isolated authored-component inspection with the selected exact SDK. Poll "
            "code.schemas; admitted types then join ordinary Scene schema/component tools.",
            "code.execute", automation_object(), true,
            [&files, &authored, &editor, providers](const Json&) {
                available(files, providers);
                if (editor.dirty() || authored.busy() || authored.migration_open_)
                    throw AuthoringError(
                        "code_busy", "Save source and settle the current component task/review");
                if (authored.root_ != files.document.project())
                    throw AuthoringError("not_ready",
                                         "Wait for the component owner to follow this project");
                authored.inspect(files.document, SdkBuild::runtime_path(sdk(providers)));
                return Json{{"operation", authored.operation_id_}, {"busy", authored.busy()}};
            }));
        result.push_back(automation_method(
            "code.inspect.cancel",
            "Cancel the exact current inspection/migration worker; admitted schemas and authored "
            "values remain unchanged.",
            "code.execute", automation_object({{"operation", automation_uuid()}}, {"operation"}),
            true,
            [&files, &authored](const Json& request) {
                files.document.check_ownership();
                if (authored.root_ != files.document.project() || !authored.busy() ||
                    request.at("operation") != authored.operation_id_)
                    throw AuthoringError("wrong_job",
                                         "Select the current component worker operation");
                authored.cancel_.request_stop();
                return Json{{"cancel_requested", true}};
            },
            true));
        result.push_back(automation_method(
            "code.migration.open",
            "Begin the actual explicit authored-schema migration review for the current scene or "
            "open prefab draft. This prepares no values and does not publish a source.",
            "code.execute",
            automation_object({{"document", {{"type", "string"}, {"enum", {"scene", "prefab"}}}}},
                              {"document"}),
            true, [&scene, &files, &authored, &prefab, providers](const Json& request) {
                available(files, providers);
                if (authored.busy() || authored.migration_open_)
                    throw AuthoringError("code_busy", "Settle the current component task/review");
                const bool target_prefab = request.at("document") == "prefab";
                authored.open_migration(scene, files.document,
                                        target_prefab ? prefab.migration_document(files.document)
                                                      : scene.document(),
                                        target_prefab, false);
                return migration_state(authored, files);
            }));
        for (const auto* action : {"prepare", "apply", "close"}) {
            Json properties{{"state", automation_digest()}};
            Json required{"state"};
            if (std::string_view(action) == "prepare") {
                properties["choice"] = automation_integer(0, 1023);
                required.push_back("choice");
                properties["rules"] = {{"type", "object"}};
                required.push_back("rules");
            }
            result.push_back(automation_method(
                std::string("code.migration.") + action,
                std::string(action) +
                    " through the existing migration owner at its exact review digest. Prepare "
                    "validates rules/values in an isolated worker. Apply rechecks "
                    "scene/prefab/module baselines; scene migration is undoable and prefab "
                    "publication remains separate.",
                "code.execute", automation_object(std::move(properties), std::move(required)), true,
                [&scene, &files, &authored, &prefab, providers, action](const Json& request) {
                    available(files, providers);
                    const auto observed = migration_state(authored, files);
                    if (!observed.at("open").get<bool>() ||
                        observed.at("state") != request.at("state"))
                        throw AuthoringError("stale_migration",
                                             "Migration review/session changed; inspect it again");
                    if (authored.busy())
                        throw AuthoringError("code_busy", "Wait for the migration worker");
                    const auto operation = std::string_view(action);
                    if (operation == "prepare") {
                        const auto choice = request.at("choice").get<std::size_t>();
                        if (choice >= authored.choices_.size())
                            throw AuthoringError("invalid_choice",
                                                 "Choose an advertised schema migration pair");
                        const auto rules = request.at("rules").dump();
                        if (rules.size() > 65536)
                            throw AuthoringError("migration_limit", "Migration rules exceed64KiB");
                        authored.choice_ = choice;
                        authored.rules_ = rules;
                        authored.returned_ = Json();
                        authored.prepare(files.document);
                    } else if (operation == "apply") {
                        if (!authored.migration_ || authored.returned_.is_null())
                            throw AuthoringError("not_prepared",
                                                 "Prepare and review valid migrated values first");
                        if (files.document.generation() != authored.document_generation_ ||
                            !detail::json_value_equal(files.document.settings().document(),
                                                      authored.settings_))
                            throw AuthoringError("stale_migration",
                                                 "The target document or module settings changed");
                        if (authored.prefab_target_)
                            prefab.accept_migration(
                                authored.migration_->before(),
                                authored.migration_->candidate(authored.returned_), scene.schema());
                        else
                            authored.migration_->apply(scene, authored.returned_,
                                                       authored.scene_revision_);
                        authored.migration_open_ = false;
                        authored.status_ =
                            authored.prefab_target_
                                ? "Prefab draft migrated; publish it separately."
                                : "Scene migrated; Undo restores the original values.";
                    } else {
                        authored.migration_open_ = authored.open_popup_ = false;
                        authored.migration_.reset();
                        authored.returned_ = Json();
                    }
                    return migration_state(authored, files);
                }));
        }
        return result;
    }
};
} // namespace forge
