#pragma once
#include "../asset_import_service.hpp"
#include "../asset_reimport.hpp"
#include "diagnostic_source.hpp"
#include "document.hpp"
#include "import_settings.hpp"
#include <utility>
namespace forge {
struct ImportEditorProfile {
    std::string id, title, source_example, source_help, summary, import_button;
    ImportTarget target;
    std::function<std::shared_ptr<const AssetImporterRegistry>()> registry;
    std::function<void(AssetPublicationCandidate&, const AssetImportPlan&, const AssetCatalog&,
                       std::span<const SubassetIdentityDecision>)>
        publish;
    // Only authored asset documents own UUIDs before first catalog publication.
    std::function<AssetId(const std::filesystem::path&, const std::filesystem::path&)>
        source_identity;
    std::function<void(ui::Problem&, const std::filesystem::path&, const std::filesystem::path&)>
        diagnostic_location;
};
class AssetImportEditor {
  public:
    explicit AssetImportEditor(ImportEditorProfile profile) : profile_(std::move(profile)) {
        if (profile_.source_example.size() >= sizeof(source_))
            throw std::runtime_error("Import source example exceeds editor field capacity");
        std::copy(profile_.source_example.begin(), profile_.source_example.end(), source_);
    }
    std::function<void(SceneDocument&, bool)> draw_extension;
    // Specialized importer controls still edit the same typed settings draft.
    std::function<void(AssetImportDraft&, std::string&)> draw_settings;
    bool preview_before_settings = false;
    AssetId selected_asset() const { return draft_ ? draft_->ticket.owner : AssetId{}; }
    std::filesystem::path selected_source() const {
        return draft_ ? draft_->request.source : std::filesystem::path{};
    }
    std::uint64_t selection_generation() const { return selection_generation_; }
    bool pending() const { return job_ != 0; }
    void edit_setting(std::string_view key, std::optional<Json> value) {
        if (!draft_ || job_)
            throw std::runtime_error("Import settings require an idle source draft.");
        draft_->request.settings =
            draft_->importer->settings().edit(draft_->request.settings, key, std::move(value));
    }
    const std::string& diagnostic() const { return error_; }
    std::vector<AssetReimportRoute> automatic_routes() const {
        const auto registry = profile_.registry();
        const auto prepare = profile_.publish;
        std::vector<AssetReimportRoute> result;
        for (const auto& descriptor : registry->descriptors())
            result.push_back({descriptor.id, profile_.target, registry,
                              [prepare](auto& c, const auto& p, const auto& catalog) {
                                  prepare(c, p, catalog, {});
                              }});
        return result;
    }
    void source_published(AssetId id) {
        if (!open_ || !draft_ || selected_asset() != id || dirty())
            return;
        const bool focus = focus_;
        load(draft_->request.source);
        focus_ = focus; // Background reimport must not steal document focus.
    }
    const std::vector<SubassetIdentityConflict>& identity_conflicts() const { return conflicts_; }
    void decide_identity(const std::string& address, std::optional<AssetId> previous) {
        if (pending())
            throw std::runtime_error("Cannot change identity decisions during an import.");
        const bool allowed = std::any_of(conflicts_.begin(), conflicts_.end(), [&](const auto& c) {
            return std::find(c.observations.begin(), c.observations.end(), address) !=
                       c.observations.end() &&
                   (!previous ||
                    std::find(c.previous.begin(), c.previous.end(), *previous) != c.previous.end());
        });
        if (!allowed || conflict_key_.empty())
            throw std::runtime_error(
                "Identity decision does not belong to the reviewed candidate.");
        auto found = std::find_if(decisions_.begin(), decisions_.end(),
                                  [&](const auto& d) { return d.address == address; });
        if (found == decisions_.end())
            decisions_.push_back({address, previous});
        else
            found->previous = previous;
    }
    bool close_cancelled = false;
    bool is_open() const { return open_; }
    std::shared_ptr<const AssetCatalog> take_catalog() {
        return std::exchange(published_catalog_, {});
    }
    bool dirty() const {
        return job_ || !decisions_.empty() ||
               (draft_ && (!saved_ || Json(draft_->request.settings) != baseline_));
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
            report(error_);
        }
    }
    void content(SceneDocument& document, bool locked) {
        ImGui::BeginDisabled(locked);
        if (ui::button(profile_.import_button.c_str(), profile_.source_help.c_str())) {
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
            published_catalog_.reset();
            service_.reset(); // Cancels/joins old work before releasing its writer lease.
            draft_.reset();
            conflicts_.clear();
            decisions_.clear();
            conflict_key_.clear();
            ++selection_generation_;
            job_ = 0;
            open_ = false;
            close_ = false;
            save_ = false;
            pending_source_.reset();
            return;
        }
        for (auto& result : service_->poll()) {
            if (result.job.id != job_)
                continue;
            job_ = 0;
            if (result.published) {
                ++selection_generation_;
                published_catalog_ =
                    std::make_shared<const AssetCatalog>(result.publication->catalog);
                baseline_ = draft_->request.settings;
                conflicts_.clear();
                decisions_.clear();
                saved_ = true;
                error_.clear();
                try {
                    draft_ = service_->prepare(draft_->request.source);
                } catch (const std::exception& e) {
                    error_ = std::string("Asset published; source refresh failed: ") + e.what();
                    report(error_);
                }
                message = result.cache_hit ? "Asset imported using verified cached data."
                                           : "Asset imported.";
                if (!result.diagnostic.empty()) {
                    message += " " + result.diagnostic;
                    report(result.diagnostic);
                }

            } else {
                if (conflict_key_ != result.job.build_key || result.identity_conflicts.empty())
                    decisions_.clear();
                conflicts_ = result.identity_conflicts;
                conflict_key_ = conflicts_.empty() ? std::string{} : result.job.build_key;
                message = error_ = result.diagnostic;
                report(error_);
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
        const auto title =
            std::string(dirty() ? "* " : "") + profile_.title + "###" + profile_.title;
        bool visible = true;
        if (ImGui::Begin(title.c_str(), &visible)) {
            if (ui::editor_context && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
                ui::editor_context->task.focus_document(profile_.id, profile_.title);
            ui::heading("Source", profile_.summary.c_str());
            if (!draft_) {
                ui::property_label_row(
                    "Project file",
                    "Source path inside this project. Copy external files into Assets first.");
                ImGui::InputText("##source", source_, sizeof(source_));
                ui::help(profile_.source_help.c_str());
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
                        "Validate and publish this asset and its settings. Failure preserves the "
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
                ImGui::EndDisabled();
                if (draw_extension && preview_before_settings)
                    draw_extension(document, locked || job_ || dirty());
                ImGui::BeginDisabled(locked || job_);
                const bool show_settings =
                    !preview_before_settings || ImGui::CollapsingHeader("Import settings");
                if (preview_before_settings)
                    ui::help("Edit source cooking settings. These remain a draft until Import "
                             "succeeds.");
                if (show_settings) {
                    if (!preview_before_settings)
                        ui::heading(
                            "Import settings",
                            "These fields come from the selected importer schema. Changes remain a "
                            "draft until Import succeeds.");
                    if (draw_settings)
                        draw_settings(*draft_, error_);
                    else
                        ui::import_settings_fields(draft_->importer->settings(),
                                                   draft_->request.settings, error_);
                    if (draft_->importer->settings().rules().empty()) {
                        ImGui::TextWrapped("This importer has no configurable settings.");
                        ui::help("Import still validates the source and publishes a complete "
                                 "candidate; no conversion options are exposed by this recipe.");
                    }
                }
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
                        "Cancel this pending candidate and keep the selected asset revision."))
                    service_->cancel(job_);
            }
            draw_conflicts(locked || job_);
            if (!error_.empty())
                ui::field_error(error_);
            if (draw_extension && !preview_before_settings)
                draw_extension(document, locked || job_ || dirty());
        }
        ImGui::End();
        if (!visible)
            request_close();
        if (save_ && !locked && !job_ && draft_) {
            save_ = false;
            try {
                job_ = service_->submit(
                    *draft_,
                    [prepare = profile_.publish, decisions = decisions_,
                     key = conflict_key_](auto& candidate, const auto& plan, const auto& catalog) {
                        if (!decisions.empty() && plan.input.key() != key)
                            throw std::runtime_error(
                                "Source/settings changed after identity review. Reimport and "
                                "review the new correspondence before publishing.");
                        prepare(candidate, plan, catalog, decisions);
                    },
                    [](const auto&, const auto&) {});
                error_.clear();
            } catch (const std::exception& e) {
                error_ = e.what();
                report(error_);
            }
        }
        if (close_ && dirty())
            ImGui::OpenPopup(("Pending " + profile_.title).c_str());
        if (ImGui::IsPopupOpen(("Pending " + profile_.title).c_str()))
            ui::draft_window_size({500 * ui::interface_scale, 220 * ui::interface_scale});
        if (ImGui::BeginPopupModal(("Pending " + profile_.title).c_str(), nullptr,
                                   ImGuiWindowFlags_None)) {
            if (!dirty())
                ImGui::CloseCurrentPopup();
            ImGui::TextWrapped("Apply or discard the pending import before continuing.");
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
    void report(const std::string& text) const {
        if (!ui::editor_context)
            return;
        ui::Problem problem{profile_.id, "Error", text, {}, {}, {}, selected_asset()};
        if (draft_) {
            problem.source = path_utf8(draft_->request.source);
            problem.source_navigation = ui::diagnostic_text_source(draft_->request.source);
            if (profile_.diagnostic_location)
                try {
                    profile_.diagnostic_location(problem, project_, draft_->request.source);
                } catch (const std::exception&) {
                    // Source navigation is optional; never hide the original failure.
                }
        }
        ui::editor_context->problems.report(std::move(problem));
    }
    std::shared_ptr<const AssetCatalog> published_catalog_;
    ImportEditorProfile profile_;
    std::filesystem::path project_;
    std::vector<SubassetIdentityConflict> conflicts_;
    std::vector<SubassetIdentityDecision> decisions_;
    std::string conflict_key_;
    std::uint64_t selection_generation_ = 0;
    std::unique_ptr<AssetImportService> service_;
    std::optional<AssetImportDraft> draft_;
    std::optional<std::filesystem::path> pending_source_;
    Json baseline_;
    AssetJobId job_ = 0;
    bool open_ = false, saved_ = false, save_ = false, close_ = false, focus_ = false;
    char source_[1024]{};
    std::string error_;
    void draw_conflicts(bool locked) {
        if (conflicts_.empty())
            return;
        ui::heading("Subasset identity needs review",
                    "Choose which existing logical asset each new member represents, or explicitly "
                    "create a new identity. No replacement is published until the complete mapping "
                    "passes validation.");
        ImGui::BeginDisabled(locked);
        unsigned index = 0;
        for (const auto& conflict : conflicts_) {
            ImGui::PushID(int(index++));
            ImGui::TextWrapped("%s", conflict.diagnostic.c_str());
            ui::help("Ambiguous identity must be decided explicitly. Source order and names do not "
                     "silently retarget existing scene references.");
            for (const auto& address : conflict.observations) {
                ImGui::PushID(address.c_str());
                auto current = std::find_if(decisions_.begin(), decisions_.end(),
                                            [&](const auto& d) { return d.address == address; });
                const auto preview = current == decisions_.end()
                                         ? std::string("Choose correspondence")
                                     : current->previous ? current->previous->str()
                                                         : std::string("New logical asset");
                ui::property_label_row(
                    address.c_str(),
                    "Candidate-local member address; it is not a durable AssetId.");
                if (ImGui::BeginCombo("##identity", preview.c_str())) {
                    auto choose = [&](std::optional<AssetId> value) {
                        decide_identity(address, value);
                        current = std::find_if(decisions_.begin(), decisions_.end(),
                                               [&](const auto& d) { return d.address == address; });
                    };
                    if (ImGui::Selectable("New logical asset",
                                          current != decisions_.end() && !current->previous))
                        choose({});
                    for (const auto id : conflict.previous) {
                        if (ImGui::Selectable(id.str().c_str(), current != decisions_.end() &&
                                                                    current->previous == id))
                            choose(id);
                        ui::help("Reuse this previous same-type identity. Publication rejects "
                                 "duplicate claims or a changed source revision.");
                    }
                    ImGui::EndCombo();
                }
                ui::help("Choose New logical asset only when this member should receive a new "
                         "AssetId. Existing references are never redirected by name.");
                ImGui::PopID();
            }
            ImGui::PopID();
        }
        ImGui::EndDisabled();
    }
    void ensure(SceneDocument& document) {
        if (service_ && project_ == document.project())
            return;
        service_.reset();
        auto registry = profile_.registry();
        service_ = std::make_unique<AssetImportService>(document.writer_guard(), registry,
                                                        profile_.target);
        project_ = document.project();
    }
    void load(std::filesystem::path source) {
        std::optional<AssetImportDraft> next;
        if (!source.empty())
            next = service_->prepare(
                source, {},
                profile_.source_identity ? profile_.source_identity(project_, source) : AssetId{});
        draft_ = std::move(next);
        ++selection_generation_;
        saved_ = draft_ && draft_->ticket.sidecar_bytes.has_value();
        baseline_ = draft_ ? Json(draft_->request.settings) : Json();
        open_ = true;
        focus_ = true;
        close_ = false;
        save_ = false;
        error_.clear();
        conflicts_.clear();
        decisions_.clear();
        conflict_key_.clear();
    }
    void finish_close() {
        open_ = false;
        close_ = false;
        save_ = false;
        draft_.reset();
        conflicts_.clear();
        decisions_.clear();
        conflict_key_.clear();
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
