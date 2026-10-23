#pragma once
#include "../asset_bytes.hpp"
#include "files.hpp"
#include "project_settings.hpp"
#include "source_document_automation.hpp"
#include <set>

namespace forge {
struct ProjectAutomation {
    static Json recovery_state(SceneDocument& document, bool untitled) {
        document.check_ownership();
        const auto path = document.recovery_path(untitled);
        if (!std::filesystem::exists(path))
            return {{"available", false}, {"untitled", untitled}};
        const auto bytes = asset_detail::read_bytes(path, 64 * 1024 * 1024);
        return {{"available", true},
                {"untitled", untitled},
                {"review", asset_detail::content_digest(bytes)},
                {"bytes", bytes.size()}};
    }
    static std::vector<LiveAuthoringMethod>
    project_methods(EditorFiles& files, const ui::AutomationPermissions& permissions) {
        std::vector<LiveAuthoringMethod> methods;
        methods.push_back(automation_method(
            "document.recovery.read",
            "Inspect current or untitled recovery availability and its exact content digest; never "
            "restore implicitly.",
            "document.read", automation_object({{"untitled", {{"type", "boolean"}}}}), false,
            [&files](const Json& request) {
                return recovery_state(files.document, request.value("untitled", false));
            }));
        if (permissions.assets)
            for (const auto* action : {"restore", "discard"})
                methods.push_back(automation_method(
                    std::string("document.recovery.") + action,
                    std::string(action) +
                        " the exact reviewed recovery through the existing file/document owner. "
                        "Restore keeps its ordinary scene history/reset semantics; it does not "
                        "silently save the scene.",
                    "document.write",
                    automation_object(
                        {{"untitled", {{"type", "boolean"}}}, {"review", automation_digest()}},
                        {"review"}),
                    true,
                    [&files, action](const Json& request) {
                        const bool untitled = request.value("untitled", false);
                        const auto current = recovery_state(files.document, untitled);
                        if (!current.at("available").get<bool>() ||
                            current.at("review") != request.at("review"))
                            throw AuthoringError("stale_review",
                                                 "Recovery changed; inspect its current digest");
                        const auto generation = files.document.generation();
                        files.automation_recovery(std::string_view(action) == "restore", untitled);
                        return Json{{"completed", true},
                                    {"rediscover", generation != files.document.generation()}};
                    },
                    true));
        if (permissions.workspace_root.empty())
            return methods;
        const auto root = std::filesystem::canonical(permissions.workspace_root);
        const auto initial = files.document.project();
        auto path = [root](const std::string& text) {
            if (ProjectPaths(root).root() != root)
                throw AuthoringError("workspace_changed",
                                     "Restart the connection after changing the workspace folder");
            const auto locator = automation_relative_path(text);
            const auto resolved = ProjectPaths(root).resolve(locator);
            if (resolved == root)
                throw AuthoringError("invalid_path",
                                     "Choose a project subfolder inside the approved workspace");
            return resolved;
        };
        methods.push_back(automation_method(
            "project.workspace.read",
            "Read the explicitly approved workspace scope and initial project. Paths for project "
            "tools are relative to this workspace.",
            "document.read", automation_object(), false, [root, initial](const Json&) {
                return Json{{"workspace", path_utf8(root)},
                            {"initial_project", path_utf8(initial)}};
            }));
        methods.push_back(automation_method(
            "project.list",
            "Page immediate project folders inside the explicitly approved workspace, including "
            "diagnostic information for invalid manifests.",
            "document.read",
            automation_object(
                {{"offset", automation_integer()}, {"limit", automation_integer(1, 128)}}),
            false, [root](const Json& request) {
                if (ProjectPaths(root).root() != root)
                    throw AuthoringError(
                        "workspace_changed",
                        "Restart the connection after changing the workspace folder");
                std::map<std::string, Json> projects;
                std::size_t scanned = 0;
                for (const auto& entry : std::filesystem::directory_iterator(root)) {
                    if (++scanned > 10000)
                        throw AuthoringError(
                            "workspace_limit",
                            "Choose a workspace with at most 10000 immediate entries");
                    try {
                        if (!entry.is_directory() || entry.is_symlink())
                            continue;
                        const auto candidate = ProjectPaths(root).resolve(entry.path().filename());
                        if (!std::filesystem::is_regular_file(candidate / "forge.project.json") &&
                            !std::filesystem::is_regular_file(candidate / "main.scene.json"))
                            continue;
                        const auto locator = path_utf8(entry.path().filename());
                        Json item{{"path", locator}};
                        try {
                            item["name"] = ProjectSettings(candidate).document().at("name");
                            item["valid"] = true;
                        } catch (const std::exception& error) {
                            item["valid"] = false;
                            item["diagnostic"] = error.what();
                        }
                        projects.emplace(locator, std::move(item));
                    } catch (const std::filesystem::filesystem_error&) { /* An unreadable entry is
                                                                            not a project grant. */
                    }
                }
                Json page = Json::array();
                std::size_t index = 0;
                const auto offset = request.value("offset", std::size_t{}),
                           limit = request.value("limit", std::size_t(128));
                for (const auto& [name, item] : projects) {
                    (void)name;
                    if (index++ < offset)
                        continue;
                    if (page.size() == limit)
                        break;
                    page.push_back(item);
                }
                return Json{{"projects", std::move(page)}, {"total", projects.size()}};
            }));
        if (!permissions.assets)
            return methods;
        methods.push_back(automation_method(
            "project.create",
            "Create and open a new game project in the approved workspace through EditorFiles. "
            "Existing destinations are rejected; dirty scenes need explicit discard and other "
            "drafts must settle.",
            "document.write",
            automation_object({{"path", automation_text(1024, 1)},
                               {"name", automation_text(120, 1)},
                               {"discard", {{"type", "boolean"}}}},
                              {"path", "name"}),
            true, [&files, path](const Json& request) {
                files.automation_project({EditorFiles::Command::CreateProject,
                                          path(request.at("path").get<std::string>()),
                                          request.at("name").get<std::string>()},
                                         request.value("discard", false));
                return Json{{"opened", true}, {"rediscover", true}};
            }));
        methods.push_back(automation_method(
            "project.open",
            "Open an existing approved-workspace project, or initial=true to return to the "
            "initially granted project. Existing lease/dirty/recovery guards remain; rediscover "
            "afterward.",
            "document.write",
            automation_object({{"path", automation_text(1024, 1)},
                               {"initial", {{"type", "boolean"}}},
                               {"discard", {{"type", "boolean"}}}}),
            true, [&files, path, initial](const Json& request) {
                if (request.contains("path") == request.value("initial", false))
                    throw AuthoringError("invalid_argument",
                                         "Provide a workspace path or initial=true");
                const auto destination = request.value("initial", false)
                                             ? initial
                                             : path(request.at("path").get<std::string>());
                files.automation_project({EditorFiles::Command::OpenProject, destination, {}},
                                         request.value("discard", false));
                return Json{{"opened", true}, {"rediscover", true}};
            }));
        return methods;
    }
    static std::string digest(const Json& value) {
        const auto text = value.dump();
        return asset_detail::content_digest(std::as_bytes(std::span(text)));
    }
    static Json settings_state(ProjectSettingsEditor& editor, SceneDocument& document) {
        document.check_ownership();
        const auto& draft = editor.loaded_ && editor.root_ == document.project()
                                ? editor.draft_
                                : document.settings().document();
        return {
            {"draft", draft},
            {"dirty", editor.loaded_ && editor.root_ == document.project() && editor.dirty()},
            {"open", editor.open_ && editor.root_ == document.project()},
            {"state", digest(Json{{"project", path_utf8(document.project())}, {"draft", draft}})},
            {"diagnostic", editor.root_ == document.project() ? editor.error_ : std::string{}}};
    }
    static void settings_check(ProjectSettingsEditor& editor, SceneDocument& document,
                               const Json& request) {
        document.check_ownership();
        if (request.at("state") != settings_state(editor, document).at("state"))
            throw AuthoringError("stale_settings",
                                 "Project settings draft changed; inspect it again");
        if (editor.save_requested_ || editor.close_requested_)
            throw AuthoringError("document_busy", "Settle the project settings save/close request");
        if (editor.loaded_ && editor.root_ != document.project() && editor.dirty())
            throw AuthoringError("document_busy", "Settle the previous project's settings draft");
        if (!editor.loaded_ || editor.root_ != document.project()) {
            editor.draft_ = editor.baseline_ = document.settings().document();
            editor.root_ = document.project();
            editor.loaded_ = true;
        }
    }
    static std::vector<LiveAuthoringMethod>
    settings_methods(ProjectSettingsEditor& editor, SceneDocument& document,
                     const ui::AutomationPermissions& permissions) {
        std::vector<LiveAuthoringMethod> methods;
        methods.push_back(automation_method(
            "project.settings.read",
            "Read the actual project settings draft and its content digest. Native module/build "
            "configuration remains with the native code owner.",
            "document.read", automation_object(), false,
            [&editor, &document](const Json&) { return settings_state(editor, document); }));
        if (!permissions.assets)
            return methods;
        for (const auto* action : {"edit", "save", "close"}) {
            Json properties{{"state", automation_digest()}};
            Json required{"state"};
            if (std::string_view(action) == "edit") {
                properties["patch"] = automation_array(
                    automation_object(
                        {{"op",
                          {{"type", "string"}, {"enum", {"add", "remove", "replace", "test"}}}},
                         {"path", automation_text(2048)},
                         {"value", Json::object()}},
                        {"op", "path"}),
                    128, 1);
                required.push_back("patch");
            } else if (std::string_view(action) == "close")
                properties["discard"] = {{"type", "boolean"}};
            methods.push_back(automation_method(
                std::string("project.settings.") + action,
                std::string(action) + " the existing project settings draft at the exact observed "
                                      "digest. Edit supports name, simulation frequency, startup "
                                      "scene, input, physics and game configuration. Save uses "
                                      "ordinary baseline checks; settings have no Scene Undo.",
                "document.write", automation_object(std::move(properties), std::move(required)),
                true, [&editor, &document, action](const Json& request) {
                    settings_check(editor, document, request);
                    if (std::string_view(action) == "edit") {
                        auto candidate =
                            AssetDocumentAutomation::patch(editor.draft_, request.at("patch"));
                        static const std::set<std::string> admitted{
                            "name", "simulation_hz", "startup_scene", "input", "physics", "game"};
                        const auto unchanged = [&](const Json& before, const Json& after) {
                            for (const auto& [key, value] : before.items())
                                if (!admitted.contains(key) &&
                                    (!after.contains(key) || after.at(key) != value))
                                    throw AuthoringError("unsupported_setting",
                                                         "Use the owning native/source controller "
                                                         "for this project field: " +
                                                             key);
                        };
                        unchanged(editor.draft_, candidate);
                        unchanged(candidate, editor.draft_);
                        ProjectSettings::validate(candidate);
                        editor.draft_ = std::move(candidate);
                        editor.open();
                    } else if (std::string_view(action) == "save") {
                        std::string message;
                        if (!editor.save(document, message))
                            throw AuthoringError("settings_conflict", message);
                    } else {
                        if (editor.dirty() && !request.value("discard", false))
                            throw AuthoringError("unsaved_document",
                                                 "Save settings or explicitly discard their draft");
                        std::string message;
                        editor.resolve_close(ui::DraftResolution::Discard, document, message);
                    }
                    return settings_state(editor, document);
                }));
        }
        return methods;
    }
};
} // namespace forge
