#pragma once
#include "../game_export.hpp"
#include "../self_executable.hpp"
#include "../ui_inspection.hpp"
#include "content_imports.hpp"
#include "file_dialog.hpp"
#include "help.hpp"
#include "ui_probe.hpp"
#include <future>
namespace forge::ui {
// On-demand task, not an authored document. The source project retains its own
// Save/history; exporting reads saved content and publishes another directory.
class GameExportTask {
    bool open_ = false, pending_ = false;
    std::array<char, 2048> destination_{}, kit_{}, module_kits_{};
    std::future<Json> job_;
    std::stop_source stop_;
    std::shared_ptr<ProjectLease const> writer_;
    GameExportRequest request_;
    struct Progress {
        std::mutex mutex;
        GameExportProgress value;
    };
    std::shared_ptr<Progress> progress_ = std::make_shared<Progress>();
    FileDialog dialog_;
    int browse_ = 0;
    std::string error_, output_;

  public:
    ~GameExportTask() { stop_.request_stop(); }
    bool busy() const { return pending_ || job_.valid(); }
    bool writing() const { return job_.valid(); }
    const std::string& output() const { return output_; }
    const std::string& error() const { return error_; }
    void open(bool native_modules = false) {
        open_ = true;
        if (!kit_[0])
            SDL_strlcpy(
                kit_.data(),
                path_utf8(self_executable().parent_path() /
                          (native_modules ? "runtime-kits/shared-native-sdk" : "runtime-kit"))
                    .c_str(),
                kit_.size());
    }
    void cancel() {
        stop_.request_stop();
        if (pending_) {
            pending_ = false;
            writer_.reset();
        }
    }
    void poll(ContentImports& imports, const std::function<void()>& refresh) {
        if (pending_ && imports.quiescent()) {
            pending_ = false;
            auto writer = writer_;
            auto request = request_;
            auto progress = progress_;
            const auto token = stop_.get_token();
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
                                                       root / "resources/ui/LatoLatin-Regular.ttf",
                                                       writer->root(), id, stop);
                    },
                    [progress](const GameExportProgress& value) {
                        std::lock_guard lock(progress->mutex);
                        progress->value = value;
                    },
                    token);
            });
        }
        if (job_.valid() && job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                auto result = job_.get();
                output_ = result.at("destination");
                error_ = result.at("cleanup_warning");
                if (editor_context)
                    editor_context->problems.resolve("game-export");
            } catch (const std::exception& e) {
                error_ = e.what();
                report_error("game-export", error_);
            }
            writer_.reset();
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
                    request_ = {};
                    request_.destination = std::filesystem::u8path(destination_.data());
                    request_.runtime_kit = std::filesystem::u8path(kit_.data());
                    request_.inspection_runtime = self_executable().parent_path() / "forge_runtime";
#ifdef _WIN32
                    request_.inspection_runtime += ".exe";
#endif
                    request_.reference_schema = scene.schema();
                    for (const auto& module :
                         document.settings().document().value("modules", Json::array()))
                        if (module.is_object()) {
                            if (!module_kits_[0])
                                throw std::runtime_error("Choose the Module kits folder");
                            const auto id = module.at("id").get<std::string>();
                            request_.module_kits.emplace(
                                id, std::filesystem::u8path(module_kits_.data()) / id);
                        }
                    writer_ = document.writer_guard();
                    stop_ = std::stop_source{};
                    pending_ = true;
                    error_.clear();
                    output_.clear();
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
