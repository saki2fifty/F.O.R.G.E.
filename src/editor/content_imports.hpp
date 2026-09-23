#pragma once
#include "../asset_reimport.hpp"
#include "diagnostic_source.hpp"
#include "document.hpp"
#include "editor_state.hpp"
namespace forge {
class ContentImports {
  public:
    std::function<std::vector<AssetReimportRoute>()> routes;
    std::function<bool(AssetId)> blocked;
    std::function<void(AssetId, std::shared_ptr<const AssetCatalog>)> published;
    void suspend(bool value) {
        suspended_ = value;
        if (service_)
            service_->suspend(value);
    }
    bool quiescent() const { return !service_ || service_->quiescent(); }
    void reimport(const std::vector<AssetId>& assets) {
        if (!service_)
            throw std::runtime_error("Source import service is not ready");
        service_->reimport(assets);
    }
    void rescan() {
        if (service_)
            service_->rescan();
        else
            attempted_ = false;
    }
    std::map<AssetId, AssetJobState> activity() const {
        return service_ ? service_->activity() : std::map<AssetId, AssetJobState>{};
    }
    std::shared_ptr<const SourceSnapshot> sources() const {
        return service_ ? service_->sources() : nullptr;
    }
    void catalog_changed(std::shared_ptr<const AssetCatalog> catalog) {
        if (service_)
            service_->catalog_changed(std::move(catalog));
    }
    void poll(SceneDocument& project, std::string& message) {
        if (project_ != project.project()) {
            service_.reset();
            project_ = project.project();
            attempted_ = false;
            error_.clear();
            observed_ = 0;
        }
        try {
            if (!service_ && !attempted_) {
                attempted_ = true;
                SourceScanOptions options;
                options.include_project_root = true;
                service_ = std::make_unique<AssetReimportService>(project.writer_guard(), routes(),
                                                                  options);
                service_->suspend(suspended_);
                service_->blocked = [this](auto id) { return blocked && blocked(id); };
            }
            if (!service_)
                return;
            for (auto& result : service_->poll()) {
                const auto key = "reimport:" + result.job.asset.str();
                if (result.published) {
                    message = "Asset updated from source.";
                    if (ui::editor_context)
                        ui::editor_context->problems.resolve(key);
                    if (published)
                        published(result.job.asset, service_->catalog());
                } else {
                    message = result.diagnostic;
                    if (ui::editor_context) {
                        ui::Problem problem{key, "Error", result.diagnostic, {},
                                            {},  {},      result.job.asset};
                        const auto catalog = service_->catalog();
                        if (const auto found = catalog->records().find(result.job.asset);
                            found != catalog->records().end()) {
                            problem.source = path_utf8(found->second.source);
                            problem.source_navigation =
                                ui::diagnostic_text_source(found->second.source);
                        }
                        ui::editor_context->problems.report(std::move(problem));
                    }
                }
            }
            if (service_->generation() != observed_) {
                observed_ = service_->generation();
                if (const auto source = service_->sources(); source && !source->complete) {
                    error_ = "Source scan is incomplete; automatic publication is waiting.";
                    for (const auto& diagnostic : source->diagnostics)
                        if (diagnostic.error) {
                            error_ +=
                                " " + path_text(diagnostic.source) + ": " + diagnostic.message;
                            break;
                        }
                    ui::report_error("source_watch", error_);
                } else {
                    error_.clear();
                    if (ui::editor_context)
                        ui::editor_context->problems.resolve("source_watch");
                }
            }
        } catch (const std::exception& e) {
            if (error_ != e.what()) {
                error_ = e.what();
                ui::report_error("source_watch", error_);
            }
        }
    }
    void status() {
        if (ui::button("Source updates",
                       "Automatic reimport status. Project sources are checked "
                       "in the background; dirty asset drafts delay their updates."))
            ImGui::OpenPopup("source-updates");
        if (!ImGui::BeginPopup("source-updates"))
            return;
        ImGui::TextUnformatted(service_ ? service_->scanning() ? "Checking source files..."
                                                               : "Watching project sources"
                                        : "Source watching unavailable");
        ui::help("Portable background content-hash polling. It does not depend only on timestamps "
                 "or require an editor restart.");
        if (service_) {
            ImGui::Text("Queued updates: %zu", service_->queued());
            ui::help("Registered assets waiting for validation. Dirty documents retain their "
                     "drafts; new source files use explicit Import actions.");
            for (const auto& job : service_->jobs())
                if (job.state == AssetJobState::Queued || job.state == AssetJobState::Running ||
                    job.state == AssetJobState::Ready) {
                    ImGui::TextWrapped("%s", job.stage.c_str());
                    ImGui::ProgressBar(float(job.progress));
                    ui::help("The previous published asset remains usable until the entire "
                             "candidate passes validation.");
                }
        }
        if (!error_.empty())
            ui::field_error(error_);
        if (ui::button("Rescan / retry failed", "Rescan the project and retry failed source "
                                                "updates without discarding document drafts."))
            rescan();
        ImGui::EndPopup();
    }

  private:
    std::filesystem::path project_;
    std::unique_ptr<AssetReimportService> service_;
    std::uint64_t observed_ = 0;
    bool attempted_ = false, suspended_ = false;
    std::string error_;
};
} // namespace forge
