#pragma once
#include "../texture_authoring.hpp"
#include "document.hpp"
#include "import_settings.hpp"
namespace forge {
class TextureImportEditor {
  public:
    explicit TextureImportEditor(std::filesystem::path worker) : worker_(std::move(worker)) {}
    bool close_cancelled = false;
    bool is_open() const { return open_; }
    bool dirty() const {
        return job_ || (draft_ && (!saved_ || Json(draft_->request.settings) != baseline_));
    }
    void request_save() { save_ = true; }
    void request_close() {
        close_ = true;
        if (!dirty())
            finish_close();
    }
    void open(SceneDocument& document, const std::filesystem::path& source = {}) {
        try {
            ensure(document);
            if (dirty()) {
                pending_source_ = source;
                request_close();
            } else {
                load(source);
            }
        } catch (const std::exception& e) {
            open_ = true;
            error_ = e.what();
            ui::report_error("texture_import", error_);
        }
    }
    void content(SceneDocument& document, bool locked) {
        ImGui::BeginDisabled(locked);
        if (ui::button("Import texture...",
                       "Open texture source and import settings. Supported images and DDS/KTX "
                       "containers are prepared in a separate worker.")) {
            try {
                open(document);
            } catch (const std::exception& e) {
                error_ = e.what();
            }
        }
        ImGui::EndDisabled();
    }
    void poll(SceneDocument& document, std::string& message) {
        if (!service_)
            return;
        if (project_ != document.project()) {
            service_.reset(); // Cancels/joins old work before releasing its writer lease.
            draft_.reset();
            job_ = 0;
            open_ = false;
            close_ = false;
            pending_source_.reset();
            return;
        }
        for (auto& result : service_->poll()) {
            if (result.job.id != job_)
                continue;
            job_ = 0;
            if (result.published) {
                baseline_ = draft_->request.settings;
                saved_ = true;
                error_.clear();
                try {
                    draft_ = service_->prepare(draft_->request.source);
                } catch (const std::exception& e) {
                    error_ = std::string("Texture published; source refresh failed: ") + e.what();
                    ui::report_error("texture_import", error_);
                }
                message = result.cache_hit ? "Texture imported using verified cached data."
                                           : "Texture imported.";
                if (!result.diagnostic.empty()) {
                    message += " " + result.diagnostic;
                    ui::report_error("texture_import", result.diagnostic);
                }

            } else {
                message = error_ = result.diagnostic;
                ui::report_error("texture_import", error_);
            }
        }
    }
    void draw(SceneDocument& document, bool locked) {
        if (!open_)
            return;
        ui::draft_window_size({680 * ui::interface_scale, 620 * ui::interface_scale});
        if (focus_) {
            ImGui::SetNextWindowFocus();
            focus_ = false;
        }
        const auto title = std::string(dirty() ? "* " : "") + "Texture import###Texture import";
        bool visible = true;
        if (ImGui::Begin(title.c_str(), &visible)) {
            if (ui::editor_context && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
                ui::editor_context->task.focus_document("texture_import", "Texture import");
            ui::heading(
                "Source",
                "One texture AssetId can provide independent color, data and normal variants.");
            if (!draft_) {
                ui::property_label_row(
                    "Project file",
                    "Source path inside this project. Copy external images into Assets first.");
                ImGui::InputText("##source", source_, sizeof(source_));
                ui::help(
                    "PNG, JPEG, TGA, BMP, WebP, HDR/RGBE, DDS, KTX or KTX2 inside this project.");
                ImGui::BeginDisabled(locked);
                if (ui::button("Review settings", "Recognize this source and load its saved import "
                                                  "settings without changing the catalog.")) {
                    try {
                        ensure(document);
                        load(std::filesystem::u8path(source_));
                    } catch (const std::exception& e) {
                        error_ = e.what();
                    }
                }
                ImGui::EndDisabled();
            } else {
                ImGui::TextWrapped("%s", path_text(draft_->request.source).c_str());
                ui::help("Original source remains unchanged. Settings are saved beside it only "
                         "after successful import.");
                ImGui::BeginDisabled(locked || job_);
                if (ui::button(
                        "Import / Reimport",
                        "Validate and publish this texture and its settings. Failure preserves the "
                        "previous usable asset; this is separate from scene Undo."))
                    save_ = true;
                ui::next_text_button("Reload saved settings");
                if (ui::button(
                        "Reload saved settings",
                        "Discard this settings draft and reread the saved source metadata.")) {
                    try {
                        load(draft_->request.source);
                    } catch (const std::exception& e) {
                        error_ = e.what();
                    }
                }
                if (ui::button("Reset to defaults", "Remove explicit settings overrides from this "
                                                    "draft. Import applies the change."))
                    draft_->request.settings =
                        draft_->importer->settings().reset(draft_->request.settings);
                ui::heading("Import settings",
                            "These fields come from the selected importer schema. Changes remain a "
                            "draft until Import succeeds.");
                ui::import_settings_fields(draft_->importer->settings(), draft_->request.settings,
                                           error_);
                ImGui::EndDisabled();
            }
            if (job_) {
                for (const auto& job : service_->jobs())
                    if (job.id == job_) {
                        ImGui::TextWrapped("%s", job.stage.c_str());
                        ImGui::ProgressBar(float(job.progress));
                        ui::help("Source hashing and cooking run in bounded jobs; publication "
                                 "occurs on the editor owner.");
                    }
                if (ui::button(
                        "Cancel import",
                        "Cancel this pending candidate and keep the selected texture revision."))
                    service_->cancel(job_);
            }
            if (!error_.empty())
                ui::field_error(error_);
        }
        ImGui::End();
        if (!visible)
            request_close();
        if (save_ && !locked && !job_ && draft_) {
            save_ = false;
            try {
                job_ = service_->submit(
                    *draft_,
                    [](auto& candidate, const auto& plan, const auto&) {
                        prepare_texture_publication(candidate, plan);
                    },
                    [](const auto&, const auto&) {});
                error_.clear();
            } catch (const std::exception& e) {
                error_ = e.what();
                ui::report_error("texture_import", error_);
            }
        }
        if (close_ && dirty())
            ImGui::OpenPopup("Pending texture import");
        if (ImGui::IsPopupOpen("Pending texture import"))
            ui::draft_window_size({500 * ui::interface_scale, 220 * ui::interface_scale});
        if (ImGui::BeginPopupModal("Pending texture import", nullptr, ImGuiWindowFlags_None)) {
            if (!dirty())
                ImGui::CloseCurrentPopup();
            ImGui::TextWrapped("Apply or discard the pending texture import before continuing.");
            ImGui::BeginDisabled(locked || job_ || !draft_);
            if (ui::button(
                    "Apply",
                    "Import the current draft, then continue only after successful publication."))
                save_ = true;
            ImGui::EndDisabled();
            ui::next_text_button("Discard");
            if (ui::button("Discard", "Cancel outstanding work and discard this settings draft. "
                                      "Existing selected assets remain unchanged.")) {
                if (job_) {
                    service_->cancel(job_);
                    job_ = 0;
                }
                draft_.reset();
                ImGui::CloseCurrentPopup();
                finish_close();
            }
            ui::next_text_button("Keep editing");
            if (ui::button("Keep editing",
                           "Cancel the close or project switch and keep this draft open.")) {
                close_ = false;
                pending_source_.reset();
                close_cancelled = true;
                ImGui::CloseCurrentPopup();
            }
            if (!error_.empty())
                ui::field_error(error_);
            ImGui::EndPopup();
        }
        if (close_ && !dirty())
            finish_close();
    }

  private:
    std::filesystem::path worker_, project_;
    std::unique_ptr<AssetImportService> service_;
    std::optional<AssetImportDraft> draft_;
    std::optional<std::filesystem::path> pending_source_;
    Json baseline_;
    AssetJobId job_ = 0;
    bool open_ = false, saved_ = false, save_ = false, close_ = false, focus_ = false;
    char source_[1024] = "Assets/texture.png";
    std::string error_;
    void ensure(SceneDocument& document) {
        if (service_ && project_ == document.project())
            return;
        service_.reset();
        auto registry = texture_import_registry(worker_);
        service_ = std::make_unique<AssetImportService>(document.writer_guard(), registry,
                                                        desktop_texture_target());
        project_ = document.project();
    }
    void load(std::filesystem::path source) {
        std::optional<AssetImportDraft> next;
        if (!source.empty())
            next = service_->prepare(source);
        draft_ = std::move(next);
        saved_ = draft_ && draft_->ticket.sidecar_bytes.has_value();
        baseline_ = draft_ ? Json(draft_->request.settings) : Json();
        open_ = true;
        focus_ = true;
        close_ = false;
        save_ = false;
        error_.clear();
    }
    void finish_close() {
        open_ = false;
        close_ = false;
        save_ = false;
        draft_.reset();
        if (pending_source_) {
            auto source = *pending_source_;
            pending_source_.reset();
            try {
                load(source);
            } catch (const std::exception& e) {
                error_ = e.what();
                open_ = true;
            }
        }
    }
};
} // namespace forge
