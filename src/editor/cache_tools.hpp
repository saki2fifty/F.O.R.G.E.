#pragma once
#include "../cache_maintenance.hpp"
#include "content_imports.hpp"
#include "ui_probe.hpp"
#include "widgets.hpp"
#include <future>
namespace forge {
class CacheTools {
  public:
    ~CacheTools() { stop_.request_stop(); }
    bool busy() const { return pending_.has_value() || job_.valid(); }
    void open() { open_ = true; }
    void poll(SceneDocument& document, ContentImports& imports) {
        if (pending_ && imports.quiescent()) {
            const auto operation = *pending_;
            pending_.reset();
            try {
                const auto writer = document.writer_guard();
                stop_ = std::stop_source{};
                job_ =
                    std::async(std::launch::async, [writer, operation, stop = stop_.get_token()] {
                        return maintain_asset_cache(*writer, operation, {}, 0, stop);
                    });
            } catch (const std::exception& e) {
                error_ = e.what();
                ui::report_error("asset_cache", error_);
            }
        }
        if (job_.valid() && job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                result_ = job_.get();
                error_.clear();
                if (!result_.value("ok", false))
                    ui::report_error(
                        "asset_cache",
                        "Derived cache verification found errors; inspect Assets > Derived cache.");
            } catch (const std::exception& e) {
                error_ = e.what();
                ui::report_error("asset_cache", error_);
            }
        }
    }
    void draw(bool locked) {
        if (!open_)
            return;
        ImGui::SetNextWindowSize({520 * ui::interface_scale, 360 * ui::interface_scale},
                                 ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Derived cache", &open_)) {
            ImGui::TextWrapped("Imported data can be rebuilt from source. Clearing it does not "
                               "delete assets or change scene Undo. Loaded resources keep their "
                               "existing revision until replaced; Reimport rebuilds cleared data.");
            ImGui::BeginDisabled(locked || busy());
            if (ui::button("Statistics", "Count disposable artifact entries and bytes."))
                pending_ = CacheMaintenance::Statistics;
            FORGE_UI_PROBE("cache:statistics");
            ImGui::SameLine();
            if (ui::button("Verify", "Check artifact integrity; report corrupt or missing data."))
                pending_ = CacheMaintenance::Verify;
            FORGE_UI_PROBE("cache:verify");
            if (ui::button("Clear cache...",
                           "Review before removing all disposable imported artifacts."))
                ImGui::OpenPopup("Clear derived cache?");
            if (ImGui::BeginPopup("Clear derived cache?")) {
                ImGui::TextWrapped("Remove all derived cache artifacts? Reimport is required to "
                                   "rebuild them. Source assets remain intact.");
                if (ui::button("Clear derived data", "This operation has no scene Undo.")) {
                    pending_ = CacheMaintenance::ClearAll;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ui::button("Cancel", "Keep existing cache artifacts."))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            ImGui::EndDisabled();
            if (busy()) {
                ImGui::TextUnformatted(pending_ ? "Waiting for imports to finish..."
                                                : "Working...");
                if (ui::button("Cancel operation",
                               "Cancel before the next cache operation boundary. Already deleted "
                               "disposable entries cannot be restored.")) {
                    pending_.reset();
                    stop_.request_stop();
                }
            }
            ImGui::BeginChild("results", {0, -ImGui::GetFrameHeightWithSpacing()});
            if (!error_.empty())
                ui::field_error(error_);
            if (result_.contains("statistics")) {
                const auto& stats = result_.at("statistics");
                if (!busy() && result_.value("ok", false)) {
                    ImGui::TextUnformatted("Ready");
                    FORGE_UI_PROBE("cache:complete");
                }
                ImGui::TextWrapped("%s", result_.value("ok", false)
                                             ? "Operation completed."
                                             : "Some entries need attention. Reimport affected "
                                               "sources to rebuild missing or corrupt data.");
                ImGui::Text("Artifacts: %llu", stats.at("entries").get<unsigned long long>());
                ImGui::Text("Stored data: %.2f MiB",
                            stats.at("bytes").get<double>() / (1024 * 1024));
                ImGui::Text("Quarantined entries: %llu",
                            stats.at("quarantined").get<unsigned long long>());
                if (ImGui::TreeNode("Details")) {
                    const auto details = result_.dump(2);
                    ImGui::TextUnformatted(details.c_str());
                    ImGui::TreePop();
                }
                ui::help("Artifact-level integrity and cleanup results; source format validation "
                         "remains with each importer.");
            }
            ImGui::EndChild();
            if (ui::button("Close", "Close this tool. A running operation keeps its project "
                                    "ownership until completion."))
                open_ = false;
            FORGE_UI_PROBE("cache:close");
        }
        ImGui::End();
    }

  private:
    bool open_ = false;
    std::optional<CacheMaintenance> pending_;
    std::future<Json> job_;
    std::stop_source stop_;
    Json result_;
    std::string error_;
};
} // namespace forge
