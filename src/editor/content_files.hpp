#pragma once
#include "../asset_file_service.hpp"
#include "content_imports.hpp"
#include "icons.hpp"
#include "widgets.hpp"
#include <array>
namespace forge {
class ContentFiles {
  public:
    std::function<std::string(const AssetRecord&, AssetFileAction)> unavailable;
    std::function<void(const AssetFileReview&, const AssetFileCommit&,
                       std::shared_ptr<const AssetCatalog>)>
        adopted;
    bool busy() const { return selected_.has_value(); }
    const std::filesystem::path& project() const { return operation_project_; }
    Json automation_status(std::size_t offset = 0, std::size_t limit = 128) const {
        if (!limit || limit > 128)
            throw AuthoringError("invalid_arguments", "File review page limit must be 1..128");
        if (!selected_)
            return last_operation_;
        Json result = {{"job", serial_},
                       {"open", true},
                       {"asset", selected_->id.str()},
                       {"diagnostic", error_},
                       {"state", stage_ == Stage::Drain ? "draining" : "choose"}};
        if (!service_) {
            if (!error_.empty())
                result["state"] = "failed";
            return result;
        }
        static constexpr const char* states[] = {"idle",      "preparing", "review",   "committing",
                                                 "succeeded", "failed",    "cancelled"};
        result["state"] = states[unsigned(service_->state())];
        if (const auto review = service_->review()) {
            result["review"] = review->revision;
            result["action"] = unsigned(review->plan.request.action);
            result["destination"] = path_utf8(review->plan.request.destination);
            result["result"] = review->plan.result.str();
            result["warnings"] = review->plan.warnings;
            result["reference_count"] = review->impact.references.size();
            result["uninspected_count"] = review->impact.uninspected.size();
            result["references"] = Json::array();
            for (std::size_t i = std::min(offset, review->impact.references.size());
                 i < review->impact.references.size() && i - offset < limit; ++i) {
                const auto& hit = review->impact.references[i];
                result["references"].push_back({{"source", path_utf8(hit.source)},
                                                {"owner", hit.owner},
                                                {"property", hit.property},
                                                {"kind", hit.kind},
                                                {"asset", hit.target.str()}});
            }
            result["uninspected"] = Json::array();
            for (std::size_t i = std::min(offset, review->impact.uninspected.size());
                 i < review->impact.uninspected.size() && i - offset < limit; ++i)
                result["uninspected"].push_back(review->impact.uninspected[i]);
        }
        if (const auto& receipt = service_->receipt(); receipt)
            result["receipt"] = {{"transaction", receipt->transaction},
                                 {"retained_files", path_utf8(receipt->retained_files)},
                                 {"diagnostic", receipt->cleanup_diagnostic}};
        return result;
    }
    void automation_prepare(const SceneDocument& document, const AssetRecord& asset,
                            AssetFileAction action, const std::filesystem::path& destination) {
        document.check_ownership();
        if (!begin(asset, action))
            throw AuthoringError("busy", "This asset file operation is unavailable");
        request_open_ = false;
        operation_project_ = document.project();
        const auto text = path_utf8(destination);
        if (text.size() >= destination_.size()) {
            selected_.reset();
            throw AuthoringError("invalid_path", "Asset destination exceeds the path limit");
        }
        destination_.fill(0);
        std::copy(text.begin(), text.end(), destination_.begin());
        prepare_review();
    }
    void automation_commit(std::uint64_t job, const std::string& review, bool acknowledge) {
        check_job(job);
        if (!service_ || service_->state() != AssetFileState::Review || !service_->review() ||
            service_->review()->revision != review)
            throw AuthoringError("stale_review", "Read the current prepared file review first");
        if (action_ == AssetFileAction::Delete && !acknowledge)
            throw AuthoringError("confirmation_required",
                                 "Delete requires explicit acknowledgment of the reviewed impact");
        service_->commit();
    }
    void automation_cancel(std::uint64_t job) {
        check_job(job);
        if (service_)
            service_->cancel();
        else {
            stage_ = Stage::Result;
            error_ = "Operation cancelled";
        }
    }
    void automation_close(std::uint64_t job, ContentImports& imports) {
        check_job(job);
        if (service_ && (service_->state() == AssetFileState::Preparing ||
                         service_->state() == AssetFileState::Committing))
            throw AuthoringError("busy",
                                 "Cancel and drain the running file operation before closing");
        last_operation_ = automation_status();
        last_operation_["open"] = false;
        if (service_ && service_->busy())
            service_->cancel();
        service_.reset();
        selected_.reset();
        imports.suspend(false);
    }
    const AssetFileService* operation() const { return service_.get(); }
    bool begin(const AssetRecord& record, AssetFileAction action) {
        if (busy() || record.subasset)
            return false;
        if (serial_ == 9007199254740991ULL)
            throw std::runtime_error("Asset file operation identities exhausted");
        ++serial_;
        const auto reason = unavailable ? unavailable(record, action) : std::string{};
        if (!reason.empty()) {
            ui::report_error("content_files", reason);
            return false;
        }
        const auto source = path_text(record.source);
        if (source.size() >= destination_.size()) {
            ui::report_error("content_files",
                             "Source filename exceeds the dialog's 2047-byte field limit.");
            return false;
        }
        selected_ = record;
        action_ = action;
        destination_.fill(0);
        std::copy(source.begin(), source.end(), destination_.begin());
        stage_ = Stage::Choose;
        error_.clear();
        acknowledged_ = false;
        request_open_ = true;
        return true;
    }
    void prepare_review() {
        if (!busy() || stage_ != Stage::Choose)
            throw std::runtime_error("Choose a source operation before preparing review");
        error_.clear();
        stage_ = Stage::Drain;
    }
    void menu(const AssetRecord& record, bool locked) {
        const bool owner = !record.subasset;
        for (const auto& [label, action] :
             {std::pair{"Rename / Move...", AssetFileAction::Move},
              std::pair{"Duplicate source...", AssetFileAction::Duplicate},
              std::pair{"Delete source...", AssetFileAction::Delete}}) {
            if (ImGui::MenuItem(label, nullptr, false, !locked && owner && !busy()))
                begin(record, action);
            ui::help(owner ? "Review source-file changes and known reference impact before "
                             "committing. These actions are separate from scene Undo."
                           : "Operate on the containing source asset; generated members are not "
                             "independent source files.");
        }
    }
    void poll(SceneDocument& project, Scene& scene, ContentImports& imports, std::string& message) {
        imports.suspend(busy());
        if (!busy())
            return;
        operation_project_ = project.project();
        if (stage_ == Stage::Drain && imports.quiescent()) {
            try {
                service_ = std::make_unique<AssetFileService>(project.writer_guard());
                std::vector<AssetReferenceDocument> drafts{
                    {project.path().empty() ? std::filesystem::path("<untitled scene>")
                                            : project.path().lexically_relative(project.project()),
                     scene.document()}};
                service_->prepare(*selected_,
                                  {action_, selected_->id,
                                   action_ == AssetFileAction::Delete
                                       ? std::filesystem::path{}
                                       : std::filesystem::u8path(destination_.data())},
                                  scene.schema(), std::move(drafts));
                stage_ = Stage::Work;
            } catch (const std::exception& e) {
                error_ = e.what();
                stage_ = Stage::Choose;
            }
        }
        if (!service_ || stage_ != Stage::Work)
            return;
        service_->poll();
        if (service_->state() == AssetFileState::Complete) {
            stage_ = Stage::Result;
            message = "Asset source operation completed.";
            try {
                imports.catalog_changed(service_->catalog());
                if (adopted)
                    adopted(*service_->review(), *service_->receipt(), service_->catalog());
                if (!service_->receipt()->cleanup_diagnostic.empty())
                    error_ = service_->receipt()->cleanup_diagnostic;
            } catch (const std::exception& e) {
                error_ = "Files were committed, but editor refresh needs attention: " +
                         std::string(e.what());
            }
            if (!error_.empty())
                ui::report_error("content_files", error_);
        } else if (service_->state() == AssetFileState::Failed ||
                   service_->state() == AssetFileState::Cancelled) {
            error_ = service_->diagnostic();
            if (error_.empty())
                error_ = "Operation cancelled; no source change committed.";
            stage_ = Stage::Result;
            message = error_;
            ui::report_error("content_files", error_);
        }
    }
    void draw(ContentImports& imports) {
        if (request_open_) {
            ImGui::OpenPopup("Asset source files");
            request_open_ = false;
        }
        const auto available = ImGui::GetMainViewport()->WorkSize;
        ImGui::SetNextWindowSize({std::min(700.f * ui::interface_scale, available.x - 30),
                                  std::min(570.f * ui::interface_scale, available.y - 30)},
                                 ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("Asset source files", nullptr))
            return;
        const char* operation = action_ == AssetFileAction::Delete      ? "Delete source"
                                : action_ == AssetFileAction::Duplicate ? "Duplicate source"
                                                                        : "Rename / Move";
        ImGui::TextUnformatted(operation);
        ui::help("File operations have their own recoverable disk commit. Scene Undo does not undo "
                 "them.");
        ImGui::TextWrapped("%s", selected_ ? path_text(selected_->source).c_str() : "");
        if (stage_ == Stage::Choose) {
            if (action_ != AssetFileAction::Delete) {
                ui::property_label_row(
                    "Destination",
                    "New project-relative source filename, including its extension.");
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##destination", destination_.data(), destination_.size());
                ui::help("New project-relative path, including the same file extension. Its folder "
                         "must already exist; existing files are not overwritten.");
            }
            ImGui::TextWrapped(
                "Prepare checks source files and known references. An authored scene or prefab may "
                "be registered using its existing identity. Files change only after confirmation.");
            if (ui::button("Prepare review",
                           "Validate identities and scan reference impact in the background.")) {
                prepare_review();
            }
        } else if (stage_ == Stage::Drain) {
            ImGui::TextWrapped("Waiting for automatic imports to cancel safely...");
        } else if (stage_ == Stage::Work && service_) {
            const auto state = service_->state();
            if (state == AssetFileState::Review) {
                const auto review = service_->review();
                ImGui::Text("%zu assets affected; %zu catalog dependents; %zu known references",
                            review->plan.affected.size(), review->plan.catalog_dependents.size(),
                            review->impact.references.size());
                ui::help(
                    "Imported members belong to their source. Known references include saved "
                    "documents and the current scene draft; both can appear for the same scene.");
                if (ImGui::BeginChild("Impact",
                                      {0, std::max(60.f, ImGui::GetContentRegionAvail().y -
                                                             125.f * ui::interface_scale)},
                                      ImGuiChildFlags_Borders)) {
                    for (const auto& warning : review->plan.warnings)
                        ImGui::TextWrapped("%s", warning.c_str());
                    for (const auto id : review->plan.catalog_dependents)
                        ImGui::TextWrapped("Catalog dependent: %s", id.str().c_str());
                    for (const auto& ref : review->impact.references)
                        ImGui::TextWrapped("%s / %s / %s (%s)", path_text(ref.source).c_str(),
                                           ref.owner.c_str(), ref.property.c_str(),
                                           ref.kind.c_str());
                    if (!review->impact.uninspected.empty()) {
                        ImGui::TextWrapped(
                            "%zu opaque/unknown fields or documents cannot be checked as "
                            "references. No unknown payload is rewritten.",
                            review->impact.uninspected.size());
                        if (ImGui::TreeNode("Uninspected data")) {
                            for (const auto& item : review->impact.uninspected)
                                ImGui::TextWrapped("%s", item.c_str());
                            ImGui::TreePop();
                        }
                        ui::help("Only known source formats and typed reflected references can be "
                                 "analyzed. A UUID-looking string in opaque data is not assumed to "
                                 "be an AssetRef.");
                    }
                }
                ImGui::EndChild();
                if (action_ == AssetFileAction::Delete) {
                    ImGui::Checkbox("I understand the deletion impact", &acknowledged_);
                    ui::help("Delete retains original source/sidecar bytes in the project recovery "
                             "folder. References are not retargeted. Scene Undo cannot restore "
                             "these files.");
                }
                ImGui::BeginDisabled(action_ == AssetFileAction::Delete && !acknowledged_);
                if (ui::button("Confirm file changes",
                               "Recheck reviewed inputs, then commit. Any stale review fails "
                               "without changing the source files.")) {
                    try {
                        service_->commit();
                    } catch (const std::exception& e) {
                        error_ = e.what();
                    }
                }
                ImGui::EndDisabled();
            } else {
                ImGui::TextWrapped(state == AssetFileState::Preparing
                                       ? "Inspecting source files and references..."
                                       : "Rechecking and committing source files...");
            }
        } else if (stage_ == Stage::Result) {
            ImGui::TextWrapped(service_ && service_->state() == AssetFileState::Complete
                                   ? "Source files updated."
                                   : "Source operation did not complete.");
            if (service_ && service_->receipt() && !service_->receipt()->retained_files.empty()) {
                ImGui::TextWrapped("Backup: %s",
                                   path_text(service_->receipt()->retained_files).c_str());
                ui::help("Verified original files and their manifest are retained for recovery. "
                         "This is not a scene Undo entry.");
            }
        }
        if (!error_.empty())
            ui::field_error(error_);
        const bool running = service_ && (service_->state() == AssetFileState::Preparing ||
                                          service_->state() == AssetFileState::Committing);
        if (running) {
            if (ui::button("Cancel job", "Request cancellation at a safe boundary. A completed "
                                         "commit remains completed."))
                service_->cancel();
        } else if (ui::button(stage_ == Stage::Result ? "Close" : "Cancel",
                              "Close this file operation and resume source updates.")) {
            if (service_ && service_->busy())
                service_->cancel();
            service_.reset();
            selected_.reset();
            imports.suspend(false);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

  private:
    void check_job(std::uint64_t job) const {
        if (!selected_ || serial_ != job)
            throw AuthoringError("wrong_job", "File operation is no longer active");
    }
    std::uint64_t serial_ = 0;
    Json last_operation_ = {{"state", "idle"}};
    std::filesystem::path operation_project_;
    enum class Stage { Choose, Drain, Work, Result };
    Stage stage_ = Stage::Choose;
    std::optional<AssetRecord> selected_;
    AssetFileAction action_ = AssetFileAction::Move;
    std::array<char, 2048> destination_{};
    bool request_open_ = false, acknowledged_ = false;
    std::string error_;
    std::unique_ptr<AssetFileService> service_;
};
} // namespace forge
