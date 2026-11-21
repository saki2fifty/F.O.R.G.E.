#pragma once
#include "automation_permissions.hpp"
#include "automation_schema.hpp"
#include "files.hpp"
#include "game_export.hpp"
#include <algorithm>

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
        return {
            {"started", true},
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
            {"startup_verification", task.startup_verification_},
            {"gpu_profiling_supported",
             !task.result_.is_null() &&
                 standalone_gpu_profile_supported(task.result_.at("manifest").at("engine"))},
            {"gpu_profile_requested", task.gpu_profile_.has_value()},
            {"gpu_live_profiling_supported",
             !task.result_.is_null() &&
                 standalone_gpu_profile_supported(task.result_.at("manifest").at("engine"), true)},
            {"gpu_profile_live_requested", task.gpu_profile_ && task.gpu_profile_->live_enabled()},
            {"gpu_profile_live_ready",
             task.gpu_profile_ && !task.gpu_profile_->live_report().is_null()},
            {"gpu_profile_live_snapshot",
             task.gpu_profile_ ? task.gpu_profile_->live_snapshot() : ""},
            {"gpu_profile_ready", task.gpu_profile_ && !task.gpu_profile_->report().is_null()},
            {"gpu_profile_error", task.gpu_profile_ ? task.gpu_profile_->error() : ""},
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
                value["text"] = automation_text_value(
                    task.project_ == files.document.project() ? task.run_log_.text() : "");
                return value;
            }));
        result.push_back(automation_method(
            "export.gpu_profile",
            "Read up to128 native GPU duration observations from this operation's successfully "
            "retired graphical game. GPU frames are presentation submissions; pending queries "
            "and skipped samples remain explicit. Software verification is not physical hardware "
            "timing. Set live:true for explicitly provisional readings from a stream_gpu_profile "
            "run. The first page returns a snapshot token; supply it for later offsets. A newer "
            "observation makes that token stale; restart at offset0. Live readings disappear on "
            "retirement. Default reads still require successful exit. Read export.status first; "
            "no file or process is opened by this read.",
            "export.read",
            automation_object({{"operation", automation_uuid()},
                               {"offset", automation_integer(0, 512)},
                               {"live", {{"type", "boolean"}}},
                               {"snapshot", automation_text(256)},
                               {"limit", automation_integer(1, 128)}},
                              {"operation"}),
            false, [&files, &task](const Json& request) {
                files.document.check_ownership();
                if (task.project_ != files.document.project() ||
                    request.at("operation") != task.operation_)
                    throw AuthoringError("wrong_job", "Read this project's current export first");
                if (!task.gpu_profile_)
                    throw AuthoringError("not_requested", "Run the game with collect_gpu_profile");
                const bool live = request.value("live", false);
                if (!live && request.contains("snapshot"))
                    throw AuthoringError("invalid_arguments", "Snapshot tokens require live:true");
                if (live && !task.gpu_profile_->live_enabled())
                    throw AuthoringError("not_requested", "Run the game with stream_gpu_profile");
                const auto& report =
                    live ? task.gpu_profile_->live_report() : task.gpu_profile_->report();
                if (report.is_null()) {
                    if (!task.gpu_profile_->error().empty())
                        throw AuthoringError("gpu_profile_unavailable", task.gpu_profile_->error());
                    throw AuthoringError("not_ready",
                                         live ? "Wait for a running game's live GPU observation"
                                              : "Wait for successful graphical game exit");
                }
                const auto& samples = report.at("gpu").at("samples");
                const auto offset = request.value("offset", std::size_t{});
                const auto limit = request.value("limit", std::size_t(64));
                const auto snapshot = request.value("snapshot", std::string{});
                if (live) {
                    if (offset && snapshot.empty())
                        throw AuthoringError("invalid_arguments",
                                             "Later live pages require their snapshot token");
                    if (!snapshot.empty() && snapshot != task.gpu_profile_->live_snapshot())
                        throw AuthoringError("stale_snapshot",
                                             "Live GPU snapshot changed; restart at offset0");
                }
                if (offset > samples.size())
                    throw AuthoringError("invalid_cursor", "GPU sample offset exceeds this report");
                auto page = live ? task.gpu_profile_->live_page(snapshot, offset, limit)
                                 : task.gpu_profile_->page(offset, limit);
                page["operation"] = task.operation_;
                return page;
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
                "Set verify_startup:true for the existing finite D3D12 WARP/offline-audio "
                "diagnostic, which exits after verifying startup and frame statistics. "
                "Optional collect_gpu_profile requests a bounded GPU duration report on the "
                "same owned output channel; requires gpu_profiling_supported in export.status. "
                "Read pages after successful exit with export.gpu_profile. Cancelled/failed "
                "processes do not establish an accepted report. Optional stream_gpu_profile "
                "also enables collection and adds provisional at-most1Hz observations while "
                "running; requires gpu_live_profiling_supported. Read with live:true and guarded "
                "snapshot pages. Finite verification waits for at least three live observations. "
                "Poll status/log or stop with "
                "export.cancel.",
                "export.execute",
                automation_object({{"operation", automation_uuid()},
                                   {"verify_startup", {{"type", "boolean"}}},
                                   {"collect_gpu_profile", {{"type", "boolean"}}},
                                   {"stream_gpu_profile", {{"type", "boolean"}}}},
                                  {"operation"}),
                true, [&files, &task, providers, root](const Json& request) {
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
                    task.run(files.document, request.value("verify_startup", false),
                             request.value("collect_gpu_profile", false),
                             request.value("stream_gpu_profile", false));
                    return state(task, files);
                }));
        for (auto& method : result)
            method.executes_native = method.name == "export.start" || method.name == "export.run";
        return result;
    }
};
} // namespace forge
