#pragma once
#include "../game_export.hpp"
#include "../install_paths.hpp"
#include "../self_executable.hpp"
#include "../standalone_manifest.hpp"
#include "../ui_inspection.hpp"
#include "build_command.hpp"
#include "content_imports.hpp"
#include "file_dialog.hpp"
#include "help.hpp"
#include "ui_probe.hpp"
#include <future>
#include <optional>
#include <vector>
namespace forge {
struct ExportAutomation;
}
namespace forge::ui {
// On-demand task, not an authored document. The source project retains its own
// Save/history; exporting reads saved content and publishes another directory.
class GameExportTask {
    friend struct ::forge::ExportAutomation;
    bool open_ = false, pending_ = false;
    std::array<char, 2048> destination_{}, kit_{}, module_kits_{};
    std::future<Json> job_;
    std::stop_source stop_;
    std::shared_ptr<ProjectLease const> writer_;
    GameExportRequest request_;
    std::filesystem::path inspection_runtime_;
    struct Progress {
        std::mutex mutex;
        GameExportProgress value;
    };
    std::shared_ptr<Progress> progress_ = std::make_shared<Progress>();
    FileDialog dialog_;
    int browse_ = 0;
    std::string error_, output_;
    std::string operation_;
    std::filesystem::path project_;
    Json result_;
    BuildCommand game_;
    bool preparing_run_ = false, running_ = false, startup_verification_ = false;
    std::optional<int> exit_code_;
    TimedTextLog run_log_;

  public:
    ~GameExportTask() { stop_.request_stop(); }
    bool busy() const { return pending_ || job_.valid() || running_; }
    bool writing() const { return job_.valid(); }
    const std::string& output() const { return output_; }
    const std::string& error() const { return error_; }
    static GameExportRequest request_for(SceneDocument& document, Scene& scene,
                                         std::filesystem::path destination,
                                         std::filesystem::path runtime_kit,
                                         const std::filesystem::path& module_kits,
                                         std::filesystem::path inspection_runtime) {
        GameExportRequest request;
        request.destination = std::move(destination);
        request.runtime_kit = std::move(runtime_kit);
        request.inspection_runtime = std::move(inspection_runtime);
        request.reference_schema = scene.schema();
        for (const auto& module : document.settings().document().value("modules", Json::array()))
            if (module.is_object()) {
                if (module_kits.empty())
                    throw std::runtime_error("Choose the Module kits folder");
                const auto id = module.at("id").get<std::string>();
                request.module_kits.emplace(id, module_kits / id);
            }
        return request;
    }
    GameExportRequest request_from_selection(SceneDocument& document, Scene& scene,
                                             const std::filesystem::path& destination,
                                             const std::filesystem::path& default_kit,
                                             const std::filesystem::path& managed_kits,
                                             const std::filesystem::path& runtime) const {
        return request_for(document, scene, destination,
                           kit_[0] ? std::filesystem::u8path(kit_.data()) : default_kit,
                           !managed_kits.empty() ? managed_kits
                           : module_kits_[0]     ? std::filesystem::u8path(module_kits_.data())
                                                 : std::filesystem::path{},
                           runtime);
    }
    void begin(SceneDocument& document, GameExportRequest request) {
        document.check_ownership();
        if (busy() || dialog_.busy())
            throw std::runtime_error("Finish the current export or game test first");
        auto writer = document.writer_guard();
        auto operation = AssetId::generate().str();
        project_ = document.project();
        request_ = std::move(request);
        writer_ = std::move(writer);
        operation_ = std::move(operation);
        stop_ = std::stop_source{};
        pending_ = true;
        preparing_run_ = false;
        startup_verification_ = false;
        error_.clear();
        output_.clear();
        result_ = Json();
        exit_code_.reset();
        {
            std::lock_guard lock(progress_->mutex);
            progress_->value = {};
        }
    }
    void run(SceneDocument& document, bool verify_startup = false) {
        document.check_ownership();
        if (busy() || document.project() != project_ || output_.empty() || result_.is_null())
            throw std::runtime_error("Export this project successfully before testing its game");
#ifndef _WIN32
        throw std::runtime_error("The exported graphical game requires Windows / D3D12");
#else
        const auto root = std::filesystem::u8path(output_);
        const auto expected = result_.at("manifest");
        auto writer = document.writer_guard();
        stop_ = std::stop_source{};
        const auto token = stop_.get_token();
        job_ = std::async(std::launch::async, [root, expected, token] {
            const auto verified =
                verify_distribution_files(root, "forge.standalone.json", "forge.standalone", token);
            if (verified != expected)
                throw std::runtime_error("The completed export changed; rebuild it before running");
            const auto executable = ProjectPaths(root).resolve(
                std::filesystem::u8path(verified.at("executable").get<std::string>()));
            return Json{{"executable", path_utf8(executable)}};
        });
        writer_ = std::move(writer);
        operation_ = AssetId::generate().str();
        preparing_run_ = true;
        startup_verification_ = verify_startup;
        exit_code_.reset();
        run_log_.clear();
        error_.clear();
#endif
    }
    void open(bool native_modules = false, const std::filesystem::path& managed_kits = {},
              const std::filesystem::path& inspection_runtime = {}) {
        open_ = true;
        inspection_runtime_ = inspection_runtime;
        if (!managed_kits.empty())
            SDL_strlcpy(module_kits_.data(), path_utf8(managed_kits).c_str(), module_kits_.size());
        if (!kit_[0])
            SDL_strlcpy(
                kit_.data(),
                path_utf8(installation_root(self_executable().parent_path()) /
                          (native_modules ? "runtime-kits/shared-native-sdk" : "runtime-kit"))
                    .c_str(),
                kit_.size());
    }
    void cancel() {
        stop_.request_stop();
        if (running_) {
            game_.close();
            running_ = false;
        }
        if (pending_) {
            pending_ = false;
            writer_.reset();
        }
    }
    void poll(ContentImports& imports, const std::function<void()>& refresh) {
        if (running_) {
            try {
                std::string output;
                int code = 0;
                const bool finished = game_.pump(output, code);
                run_log_.append(output);
                if (finished) {
                    running_ = false;
                    exit_code_ = code;
                    if (code)
                        error_ = "Exported game exited with code " + std::to_string(code);
                }
            } catch (const std::exception& e) {
                running_ = false;
                game_.close();
                error_ = e.what();
            }
        }
        if (pending_ && imports.quiescent()) {
            pending_ = false;
            auto writer = writer_;
            auto request = request_;
            auto progress = progress_;
            const auto token = stop_.get_token();
            try {
                job_ = std::async(std::launch::async, [writer, request, progress, token] {
                    auto root = self_executable().parent_path();
                    auto worker = root / "forge_ui_inspect";
#ifdef _WIN32
                    worker += ".exe";
#endif
                    return export_standalone_game(
                        *writer, request,
                        [&](AssetId id, std::stop_token stop) {
                            return inspect_ui_dependencies(worker,
                                                           installation_root(root) /
                                                               "resources/ui/LatoLatin-Regular.ttf",
                                                           writer->root(), id, stop);
                        },
                        [progress](const GameExportProgress& value) {
                            std::lock_guard lock(progress->mutex);
                            progress->value = value;
                        },
                        token);
                });
            } catch (const std::exception& e) {
                writer_.reset();
                error_ = e.what();
                report_error("game-export", error_);
            }
        }
        if (job_.valid() && job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                auto result = job_.get();
                if (preparing_run_) {
                    writer_->check();
                    if (stop_.stop_requested())
                        throw std::runtime_error("Game test cancelled");
                    std::vector<std::string> arguments{result.at("executable").get<std::string>()};
                    if (startup_verification_) {
                        // Existing finite graphical diagnostic: actual shipped game,
                        // WARP/offline audio and measured presentation statistics.
                        arguments.emplace_back("--verify-startup");
                        arguments.emplace_back("--show-fps");
                    }
                    game_.start(arguments, nullptr, std::filesystem::u8path(output_), 0);
                    running_ = true;
                } else {
                    output_ = result.at("destination");
                    error_ = result.at("cleanup_warning");
                    result_ = std::move(result);
                }
                if (editor_context)
                    editor_context->problems.resolve("game-export");
            } catch (const std::exception& e) {
                error_ = e.what();
                report_error("game-export", error_);
            }
            writer_.reset();
            preparing_run_ = false;
            refresh();
        }
    }
    void draw(SDL_Window* window, SceneDocument& document, Scene& scene, bool locked,
              bool dirty_drafts, const std::function<void()>& project_settings) {
        if (auto result = dialog_.take()) {
            if (!result->error.empty())
                error_ = result->error;
            else if (!result->path.empty()) {
                auto& field = browse_ == 1 ? destination_ : browse_ == 2 ? kit_ : module_kits_;
                const auto path = browse_ == 1
                                      ? path_utf8(std::filesystem::u8path(result->path) / "Game")
                                      : result->path;
                SDL_strlcpy(field.data(), path.c_str(), field.size());
            }
        }
        if (!open_)
            return;
        ImGui::SetNextWindowSize({650 * interface_scale, 500 * interface_scale},
                                 ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Export Game", &open_)) {
            if (button("Close Export", "Hide this task; an active export continues and remains "
                                       "available from Run > Export Game."))
                open_ = false;
            ImGui::TextWrapped("Development standalone · Windows / D3D12");
            help("Uses the selected exact runtime kit. Logical assets remain backend-neutral. Only "
                 "Development standalone is supported.");
            ImGui::TextWrapped("Exports saved project content. Unsaved scene, prefab and settings "
                               "drafts must be saved or discarded first.");
            if (button("Project Settings", "Set Startup Scene and Game defaults before exporting."))
                project_settings();
            ImGui::BeginDisabled(locked || busy() || dialog_.busy());
            auto path_field = [&](const char* label, auto& buffer, int which, const char* tip) {
                IdScope id(label);
                ImGui::InputText(label, buffer.data(), buffer.size());
                help(tip);
                FORGE_UI_PROBE(std::string("export:") + label);
                if (button("Browse...",
                           which == 1 ? "Choose a parent folder; a Game subfolder is proposed."
                                      : "Select the prepared kit folder.")) {
                    browse_ = which;
                    dialog_.show(FileDialog::Kind::ProjectParent, window, buffer.data());
                }
            };
            path_field("Destination", destination_, 1,
                       "New game folder outside the source project and runtime kit. Rebuilding a "
                       "validated export preserves the previous output until promotion.");
            path_field("Runtime kit", kit_, 2,
                       "Exact installed GameRuntime kit: executable, runtime DLLs, fonts and "
                       "notices. No editor or SDK is copied into the game.");
            if (document.settings().requires_native_sdk())
                path_field("Module kits", module_kits_, 3,
                           "Parent directory containing one SDK-generated deployment folder per "
                           "configured module ID.");
            ImGui::BeginDisabled(dirty_drafts || !destination_[0] || !kit_[0]);
            if (button("Export / Rebuild",
                       "Validate and rebuild the complete declared closure in staging, then "
                       "replace only a recognized previous export. Selected cooked artifacts must "
                       "already be current.")) {
                try {
                    auto runtime = self_executable().parent_path() / "forge_runtime";
#ifdef _WIN32
                    runtime += ".exe";
#endif
                    if (document.settings().requires_native_sdk()) {
                        if (inspection_runtime_.empty())
                            throw std::runtime_error(
                                "Select the matching Native SDK in Build before export.");
                        runtime = inspection_runtime_;
                    }
                    auto request =
                        request_for(document, scene, std::filesystem::u8path(destination_.data()),
                                    std::filesystem::u8path(kit_.data()),
                                    std::filesystem::u8path(module_kits_.data()), runtime);
                    begin(document, std::move(request));
                } catch (const std::exception& e) {
                    error_ = e.what();
                    report_error("game-export", error_);
                }
            }
            FORGE_UI_PROBE("export:start");
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            if (dirty_drafts)
                ImGui::TextWrapped("Save or discard unsaved authoring drafts to export.");
            if (busy()) {
                GameExportProgress value;
                {
                    std::lock_guard lock(progress_->mutex);
                    value = progress_->value;
                }
                ImGui::TextWrapped("%s", pending_ ? "Waiting for imports to finish..."
                                                  : value.stage.c_str());
                ImGui::ProgressBar(float(value.completed) / float(value.total));
                help("Measured export stages; byte-level ETA is not estimated.");
                if (button("Cancel export", "Cancel preparation. Once final promotion starts, "
                                            "FORGE completes or recovers it before returning."))
                    cancel();
                FORGE_UI_PROBE("export:cancel");
            }
            if (!error_.empty())
                field_error(error_);
            if (!output_.empty()) {
                ImGui::TextWrapped("Exported to %s", output_.c_str());
                if (button("Reveal output",
                           "Open the completed game folder in the system file manager."))
                    SDL_OpenURL(local_file_url(std::filesystem::u8path(output_)).c_str());
                FORGE_UI_PROBE("export:reveal");
            }
        }
        ImGui::End();
    }
};
} // namespace forge::ui
