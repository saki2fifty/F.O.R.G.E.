#pragma once
#include "automation_permissions.hpp"
#include "automation_schema.hpp"
#include "files.hpp"
#include "game_export.hpp"

namespace forge {
struct ExportAutomationProviders {
    std::function<GameExportRequest(const std::filesystem::path&)> request;
    std::function<std::string()> unavailable;
};
struct ExportAutomation {
    static Json state(const ui::GameExportTask& task, EditorFiles& files) {
        files.document.check_ownership();
        if (task.project_ != files.document.project())
            return {{"started", false}, {"busy", false}};
        GameExportProgress progress;
        {
            std::lock_guard lock(task.progress_->mutex);
            progress = task.progress_->value;
        }
        return {{"started", true},
                {"busy", task.busy()},
                {"operation", task.operation_},
                {"stage", task.preparing_run_ ? "Verifying exported game before launch"
                          : task.running_     ? "Exported game running"
                          : task.pending_     ? "Waiting for imports"
                                              : progress.stage},
                {"completed", progress.completed},
                {"total", progress.total},
                {"output", task.output_},
                {"error", task.error_},
                {"cancel_requested", task.stop_.stop_requested()},
                {"running", task.running_},
                {"exit_code", task.exit_code_ ? Json(*task.exit_code_) : Json()}};
    }
    static std::vector<LiveAuthoringMethod> methods(EditorFiles& files, ui::GameExportTask& task,
                                                    ExportAutomationProviders providers,
                                                    const ui::AutomationPermissions& permissions) {
        std::vector<LiveAuthoringMethod> result;
        result.push_back(automation_method(
            "export.status",
            "Read the owning export task's measured progress, operation and "
            "completed output for this project. Saved source and output ownership "
            "remain separate; cancellation can follow a completed promotion.",
            "export.read", automation_object(), false,
            [&files, &task](const Json&) { return state(task, files); }));
        result.push_back(automation_method(
            "export.run.log", "Read the existing supervised exported-game test's timestamped log.",
            "export.read", automation_object(), false, [&files, &task](const Json&) {
                auto value = state(task, files);
                value["text"] =
                    task.project_ == files.document.project() ? task.run_log_.text() : "";
                return value;
            }));
        if (!permissions.export_game || permissions.export_root.empty())
            return result;
        const auto root = std::filesystem::canonical(permissions.export_root);
        result.push_back(automation_method(
            "export.start",
            "Validate and export saved project content using the editor-selected "
            "runtime/module kits and actual export owner. Choose a named folder "
            "inside the separately granted output root. Replacing an existing "
            "recognized export requires replace:true; unrelated folders are never "
            "adopted. Poll export.status for worker validation and promotion.",
            "export.write",
            automation_object(
                {{"path", automation_text(2047, 1)}, {"replace", {{"type", "boolean"}}}}, {"path"}),
            true,
            [&files, &task, providers, root, execute = permissions.execute](const Json& request) {
                files.document.check_ownership();
                if (files.document.settings().requires_native_sdk() && !execute)
                    throw AuthoringError("execution_required",
                                         "Exporting project code requires native inspection; "
                                         "enable the execution grant");
                if (providers.unavailable) {
                    const auto reason = providers.unavailable();
                    if (!reason.empty())
                        throw AuthoringError("export_busy", reason);
                }
                const ProjectPaths output(root);
                if (output.root() != root)
                    throw AuthoringError("invalid_path", "The output grant folder changed");
                const auto path = automation_relative_path(request.at("path").get<std::string>());
                asset_storage::ordinary(root / path);
                const auto destination = output.resolve(path);
                if (destination == root ||
                    !std::filesystem::is_directory(destination.parent_path()))
                    throw AuthoringError("invalid_path",
                                         "Choose a named game folder with an existing parent");
                if (std::filesystem::exists(destination) && !request.value("replace", false))
                    throw AuthoringError("output_exists",
                                         "Inspect the output and explicitly request replacement");
                task.begin(files.document, providers.request(destination));
                return state(task, files);
            }));
        result.push_back(automation_method(
            "export.cancel",
            "Cancel the exact current export/test through its existing owner. "
            "Promotion already committed remains committed and recoverable.",
            "export.write", automation_object({{"operation", automation_uuid()}}, {"operation"}),
            true,
            [&files, &task](const Json& request) {
                files.document.check_ownership();
                if (task.project_ != files.document.project() || !task.busy() ||
                    request.at("operation") != task.operation_)
                    throw AuthoringError("wrong_job",
                                         "Select the current project's exact export/test");
                task.cancel();
                return state(task, files);
            },
            true));
        if (permissions.execute)
            result.push_back(automation_method(
                "export.run",
                "Verify and launch this task's completed Windows game using the "
                "existing process supervisor. Native execution has the account's OS "
                "access. No arbitrary path, arguments or shell command is accepted. "
                "Poll status/log or stop with export.cancel.",
                "export.execute",
                automation_object({{"operation", automation_uuid()}}, {"operation"}), true,
                [&files, &task, providers, root](const Json& request) {
                    files.document.check_ownership();
                    if (providers.unavailable) {
                        const auto reason = providers.unavailable();
                        if (!reason.empty())
                            throw AuthoringError("export_busy", reason);
                    }
                    if (task.project_ != files.document.project() ||
                        request.at("operation") != task.operation_)
                        throw AuthoringError("wrong_job",
                                             "Read this project's completed export first");
                    const ProjectPaths output(root);
                    if (output.root() != root || task.output_.empty())
                        throw AuthoringError("invalid_path", "The granted output is unavailable");
                    (void)output.relative(std::filesystem::u8path(task.output_));
                    task.run(files.document);
                    return state(task, files);
                }));
        for (auto& method : result)
            method.executes_native = method.name == "export.start" || method.name == "export.run";
        return result;
    }
};
} // namespace forge
