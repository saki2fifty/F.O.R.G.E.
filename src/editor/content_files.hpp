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
    const AssetFileService* operation() const { return service_.get(); }
    bool begin(const AssetRecord& record, AssetFileAction action) {
        if (busy() || record.subasset)
            return false;
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
