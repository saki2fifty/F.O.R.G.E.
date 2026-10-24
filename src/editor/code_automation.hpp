#pragma once
#include "../asset_bytes.hpp"
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
    static std::string hash(std::string_view value) {
        return asset_detail::content_digest(std::as_bytes(std::span(value.data(), value.size())));
    }
    static Json text_page(const std::string& text, const Json& request) {
        auto start = std::min(request.value("offset", std::size_t{}), text.size());
        const auto continuation = [&](std::size_t i) {
            return (static_cast<unsigned char>(text[i]) & 0xc0) == 0x80;
        };
        while (start < text.size() && continuation(start))
            ++start;
        auto end = std::min(text.size(), start + request.value("limit", std::size_t(65536)));
        while (end > start && end < text.size() && continuation(end))
            --end;
        const auto safe = Json::parse(Json(text.substr(start, end - start))
                                          .dump(-1, ' ', false, Json::error_handler_t::replace));
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
        return {{"path", tab.locator},
                {"session", tab.session},
                {"state", hash(tab.editor.GetText())},
                {"dirty", tab.dirty},
                {"external", tab.external},
                {"can_undo", tab.editor.CanUndo()},
                {"can_redo", tab.editor.CanRedo()}};
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
        if (request.at("session") != source->session ||
            request.at("state") != hash(source->editor.GetText()))
            throw AuthoringError("stale_source", "C++ source draft/session changed; read it again");
        if (editor.close_all_ || editor.close_popup_ || !editor.close_target_.empty())
            throw AuthoringError("code_busy", "Settle the pending source close request");
        return *source;
    }
    static void source_ready(const ui::CppSourceEditor& editor) {
        if (editor.close_all_ || editor.close_popup_ || !editor.close_target_.empty())
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
                "helpers and registration rules, then open it in C++ Sources.",
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
                        if (!request.contains("component"))
                            throw AuthoringError("invalid_argument",
                                                 "Systems require an existing component name");
                        path = ui::create_cpp_system(files.document.project(), name,
                                                     request.at("component").get<std::string>());
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
                "dirty tabs and source Undo history.",
                "code.write", automation_object({{"path", automation_text(2047, 1)}}, {"path"}),
                true, [&files, &editor, providers](const Json& request) {
                    available(files, providers);
                    source_ready(editor);
                    const auto path =
                        automation_relative_path(request.at("path").get<std::string>());
                    editor.open(files.document, path_utf8(path));
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
                        editor.active_ = &source;
                        const auto operation = std::string_view(action);
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
                        } else {
                            if (source.dirty && !request.value("discard", false))
                                throw AuthoringError("unsaved_source",
                                                     "Save or explicitly discard this C++ draft");
                            editor.erase(source.locator);
                            return Json{{"open", false}};
                        }
                        return tab_state(source);
                    }));
            }
        }
        if (permissions.execute) {
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
                                authored.migration_->candidate(authored.returned_));
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
