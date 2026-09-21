#pragma once
#include "../asset_storage.hpp"
#include "../model_importer.hpp"
#include "../source_copy.hpp"
#include "content_imports.hpp"
#include "file_dialog.hpp"
#include "widgets.hpp"
#include <fstream>
#include <future>
#include <misc/cpp/imgui_stdlib.h>
namespace forge {
class SourceImport {
  public:
    std::function<std::vector<AssetReimportRoute>()> routes;
    std::function<void(AssetId, std::shared_ptr<const AssetCatalog>)> published;
    ~SourceImport() { stop_.request_stop(); }
    bool busy() const { return dialog_.busy() || open_; }
    bool review_ready() const { return state_ == State::Review; }
    bool finished() const { return state_ == State::Done; }
    const std::string& diagnostic() const { return error_; }
    std::size_t published_count() const {
        return std::count_if(results_.begin(), results_.end(), [](const auto& r) { return r.ok; });
    }
    void copy_sources(bool import_with_defaults) {
        if (!review_ready())
            throw std::runtime_error("Prepare and review source files first.");
        cook_ = import_with_defaults;
        cancelled_ = false;
        state_ = State::Drain;
    }
    void close() {
        if (prepare_.valid() || copy_.valid() || active_ || state_ == State::Drain ||
            state_ == State::Importing)
            throw std::runtime_error("Cancel and wait for source work before closing.");
        open_ = false;
    }
    void picker(SceneDocument& project, SDL_Window* window) {
        if (busy())
            return;
        project.check_ownership();
        dialog_project_ = project.project();
        dialog_.show(FileDialog::Kind::ImportFiles, window, path_text(project.project()));
    }
    void select(SceneDocument& project, std::vector<std::filesystem::path> paths) {
        if (open_ || prepare_.valid() || copy_.valid())
            throw std::runtime_error("Finish the current source import first.");
        project.check_ownership();
        if (paths.empty() || paths.size() > 256)
            throw std::runtime_error("Select between 1 and 256 source files.");
        project_ = project.project();
        selected_ = std::move(paths);
        profiles_ = routes ? routes() : std::vector<AssetReimportRoute>{};
        // Authored asset documents own persistent identities and dependency
        // graphs. Their document/file workflows, not raw source copying, own duplication.
        std::erase_if(profiles_, [](const auto& profile) {
            return profile.importer != "forge.model.gltf" &&
                   profile.importer != "forge.audio.wav" &&
                   profile.importer != "forge.texture.image" &&
                   profile.importer != "forge.texture.container";
        });
        destination_ = "Assets/Imported";
        for (unsigned i = 2; std::filesystem::exists(project_ / destination_) && i < 10000; ++i)
            destination_ = "Assets/Imported" + std::to_string(i);
        state_ = State::Choose;
        open_ = popup_ = true;
        cancelled_ = copied_ = false;
        error_.clear();
        results_.clear();
        prepared_.reset();
        services_.clear();
        next_ = 0;
        active_.reset();
    }
    void poll(SceneDocument& project, ContentImports& imports, std::string& message) {
        if (auto result = dialog_.take()) {
            if (!result->error.empty())
                ui::report_error("source_import", result->error);
            else if (!result->paths.empty()) {
                try {
                    if (project.project() != dialog_project_)
                        throw std::runtime_error("Source picker belongs to a previous project.");
                    std::vector<std::filesystem::path> paths;
                    for (const auto& path : result->paths)
                        paths.push_back(std::filesystem::u8path(path));
                    select(project, std::move(paths));
                } catch (const std::exception& e) {
                    ui::report_error("source_import", e.what());
                }
            }
        }
        if (!open_)
            return;
        try {
            if (project.project() != project_) {
                cancel();
                if ((prepare_.valid() &&
                     prepare_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) ||
                    (copy_.valid() &&
                     copy_.wait_for(std::chrono::seconds(0)) != std::future_status::ready))
                    return;
                if (prepare_.valid()) {
                    try {
                        (void)prepare_.get();
                    } catch (const std::exception&) {
                    }
                }
                if (copy_.valid()) {
                    try {
                        copy_.get();
                        copied_ = true;
                    } catch (const std::exception&) {
                    }
                }
                if (active_) {
                    bool drained = false;
                    for (const auto& outcome : services_.at(active_->first)->poll())
                        if (outcome.job.id == active_->second)
                            drained = true;
                    if (!drained)
                        return;
                    active_.reset();
                }
                error_ = "Source import cancelled because its project changed. Check the original "
                         "project for completed copies/imports.";
                state_ = State::Done;
                return;
            }
            if (prepare_.valid() &&
                prepare_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                prepared_ = prepare_.get();
                if (stop_.stop_requested())
                    throw std::runtime_error("Source review cancelled.");
                state_ = State::Review;
            }
            if (state_ == State::Drain && imports.quiescent()) {
                const auto lease = project.writer_guard();
                const auto plan = prepared_->plan;
                stop_ = std::stop_source{};
                const auto stop = stop_.get_token();
                copy_ = std::async(std::launch::async,
                                   [lease, plan, stop] { commit_source_copy(*lease, plan, stop); });
                state_ = State::Copying;
            }
            if (copy_.valid() &&
                copy_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                copy_.get();
                copied_ = true;
                state_ = cook_ && !cancelled_ ? State::Importing : State::Done;
                message = "Source files copied. Asset publication is a separate step.";
            }
            if (state_ == State::Importing) {
                if (active_) {
                    auto& service = *services_.at(active_->first);
                    for (auto& outcome : service.poll())
                        if (outcome.job.id == active_->second) {
                            Result result{prepared_->plan.destination /
                                              prepared_->plan.sources[next_],
                                          profiles_[active_->first].importer, outcome.published,
                                          outcome.diagnostic};
                            if (outcome.published) {
                                auto catalog = std::make_shared<const AssetCatalog>(
                                    outcome.publication->catalog);
                                imports.catalog_changed(catalog);
                                try {
                                    if (published)
                                        published(outcome.job.asset, catalog);
                                } catch (const std::exception& e) {
                                    result.message +=
                                        std::string(" Asset published; editor refresh failed: ") +
                                        e.what();
                                }
                            }
                            results_.push_back(std::move(result));
                            active_.reset();
                            ++next_;
                            break;
                        }
                }
                if (!active_) {
                    if (cancelled_ || next_ == prepared_->plan.sources.size()) {
                        state_ = State::Done;
                        message = cancelled_
                                      ? "Batch cancelled. Already published assets and copied "
                                        "sources remain."
                                      : "Source import batch finished. Review per-file results.";
                    } else {
                        const auto route = prepared_->route_indices[next_];
                        auto& service = services_[route];
                        try {
                            if (!service)
                                service = std::make_unique<AssetImportService>(
                                    project.writer_guard(), profiles_[route].registry,
                                    profiles_[route].target, 1);
                            auto draft = service->prepare(prepared_->plan.destination /
                                                              prepared_->plan.sources[next_],
                                                          profiles_[route].importer);
                            active_ = {
                                {route, service->submit(std::move(draft), profiles_[route].prepare,
                                                        [](const auto&, const auto&) {})}};
                        } catch (const std::exception& e) {
                            results_.push_back(
                                {prepared_->plan.destination / prepared_->plan.sources[next_],
                                 profiles_[route].importer, false, e.what()});
                            ++next_;
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            error_ = e.what();
            ui::report_error("source_import", error_);
            state_ = copied_ ? State::Done : State::Choose;
        }
    }
    void prepare() {
        if (state_ != State::Choose || selected_.empty())
            return;
        copied_ = cancelled_ = false;
        stop_ = std::stop_source{};
        const auto stop = stop_.get_token();
        prepare_ = std::async(std::launch::async, [project = project_,
                                                   folder = std::filesystem::u8path(destination_),
                                                   paths = selected_, profiles = profiles_, stop] {
            Prepared result;
            for (const auto& path : paths) {
                if (stop.stop_requested())
                    throw std::runtime_error("Source review cancelled.");
                asset_storage::ordinary(std::filesystem::absolute(path).lexically_normal());
                std::ifstream stream(path, std::ios::binary);
                if (!stream)
                    throw std::runtime_error("Cannot open selected source: " + path_text(path));
                std::vector<std::byte> prefix(65536);
                stream.read(reinterpret_cast<char*>(prefix.data()), prefix.size());
                if (stream.bad())
                    throw std::runtime_error("Cannot read selected source prefix.");
                prefix.resize(std::size_t(stream.gcount()));
                std::set<std::size_t> matches;
                for (std::size_t i = 0; i < profiles.size(); ++i)
                    for (const auto& candidate : profiles[i].registry->candidates(
                             {path.filename(), prefix}, profiles[i].target))
                        if (candidate.importer->descriptor().id == profiles[i].importer)
                            matches.insert(i);
                if (matches.size() != 1)
                    throw std::runtime_error("No unique supported importer for " +
                                             path_text(path.filename()) +
                                             ". Remove this entry; use the type's Content "
                                             "registration workflow if available.");
                result.route_indices.push_back(*matches.begin());
            }
            result.plan = prepare_source_copy(project, folder, paths,
                                              asset_detail::model_cook_extensions(), stop);
            return result;
        });
        error_.clear();
        state_ = State::Preparing;
    }
    void cancel() {
        cancelled_ = true;
        stop_.request_stop();
        if (active_)
            services_.at(active_->first)->cancel(active_->second);
        if (state_ == State::Drain)
            state_ = State::Review;
    }
    void draw(bool locked) {
        if (popup_) {
            ImGui::OpenPopup("Import source files");
            popup_ = false;
        }
        if (!open_) {
            if (ImGui::BeginPopupModal("Import source files", nullptr)) {
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            return;
        }
        const auto available = ImGui::GetMainViewport()->WorkSize;
        ImGui::SetNextWindowSize({std::min(720.f * ui::interface_scale, available.x - 30),
                                  std::min(590.f * ui::interface_scale, available.y - 30)},
                                 ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("Import source files", nullptr))
            return;
        ImGui::BeginChild("Import review", {0, std::max(80.f, ImGui::GetContentRegionAvail().y -
                                                                  45.f * ui::interface_scale)});
        ImGui::TextWrapped("Copy selected raw sources into a new project folder, then import each "
                           "asset. Existing files are never overwritten. Scene Undo does not undo "
                           "source copying or asset publication.");
        ui::help("glTF buffers/images beneath the selected source's folder are included. Sidecars "
                 "and existing scene/prefab/material identities are not copied. Each selected "
                 "source has an independent subfolder.");
        if (state_ == State::Choose) {
            ui::property_label_row("New project folder",
                                   "Choose an unused folder below Assets; its parent must exist.");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##folder", &destination_);
            ui::help(
                "Each input is placed in Source-1, Source-2 and so on within this new folder.");
            for (std::size_t i = 0; i < selected_.size(); ++i) {
                ui::IdScope id(std::to_string(i).c_str());
                ImGui::TextWrapped("%s", path_text(selected_[i]).c_str());
                if (ui::button("Remove", "Remove this entry before preparing the batch.")) {
                    selected_.erase(selected_.begin() + i);
                    break;
                }
            }
            ImGui::BeginDisabled(locked || selected_.empty());
            if (ui::button("Prepare import", "Read bounded sources, resolve dependencies and "
                                             "prepare a review without copying or importing.")) {
                try {
                    prepare();
                } catch (const std::exception& e) {
                    error_ = e.what();
                }
            }
            ImGui::EndDisabled();
        } else if (state_ == State::Review) {
            ImGui::TextWrapped("%zu sources, %zu files, %.1f MiB to %s",
                               prepared_->plan.sources.size(), prepared_->plan.files.size(),
                               double(prepared_->plan.bytes) / (1024 * 1024),
                               path_text(prepared_->plan.destination).c_str());
            for (const auto& path : prepared_->plan.sources)
                ImGui::TextWrapped("%s", path_text(path).c_str());
            ImGui::Checkbox("Import with defaults after copying", &cook_);
            ui::help("Cooking large models or textures can take time. Turn this off to copy first, "
                     "then open each source's settings from Content before importing.");
            ImGui::BeginDisabled(locked);
            if (ui::button("Copy sources",
                           "Recheck reviewed bytes and atomically create the new folder. Each "
                           "subsequent asset import publishes independently."))
                copy_sources(cook_);
            ImGui::EndDisabled();
            if (ui::button("Back", "Change files or destination; prepare again before copying."))
                state_ = State::Choose;
        } else if (state_ == State::Preparing || state_ == State::Copying ||
                   state_ == State::Drain) {
            ImGui::TextUnformatted(
                state_ == State::Preparing ? "Reading source files and dependencies..."
                : state_ == State::Drain   ? "Waiting for other imports to stop safely..."
                                           : "Copying reviewed source bytes...");
            ui::help("Work runs in the background. Cancel preserves existing project files.");
        } else if (state_ == State::Importing) {
            ImGui::Text("Importing %zu of %zu", next_ + 1, prepared_->plan.sources.size());
            if (active_)
                for (const auto& job : services_.at(active_->first)->jobs())
                    if (job.id == active_->second) {
                        ImGui::TextWrapped("%s", job.stage.c_str());
                        ImGui::ProgressBar(float(job.progress));
                        ui::help("Each asset publishes only after validation. Earlier successful "
                                 "imports remain if another source fails.");
                    }
        } else if (copied_ && prepared_) {
            ImGui::TextWrapped(
                "Copied sources remain in %s. Successful imports are available in Content. Failed "
                "sources can be opened through their import settings and retried.",
                path_text(prepared_->plan.destination).c_str());
            ui::help("A batch is a series of independent validated asset publications, not one "
                     "scene or cross-asset Undo transaction.");
            ImGui::Text("Published: %zu of %zu", published_count(), prepared_->plan.sources.size());
        }
        for (const auto& result : results_) {
            ImGui::TextWrapped("%s: %s", result.ok ? "Imported" : "Not imported",
                               path_text(result.source).c_str());
            if (!result.message.empty())
                ImGui::TextWrapped("%s", result.message.c_str());
        }
        if (!error_.empty())
            ui::field_error(error_);
        ImGui::EndChild();
        const bool working = state_ == State::Preparing || state_ == State::Copying ||
                             state_ == State::Importing || state_ == State::Drain;
        if (working) {
            if (ui::button("Cancel batch", "Cancel pending work. Completed source copying and "
                                           "successful asset publications remain."))
                cancel();
        } else if (ui::button("Close", "Close this import review. Copied sources and completed "
                                       "imports remain in the project.")) {
            close();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

  private:
    enum class State { Choose, Preparing, Review, Drain, Copying, Importing, Done };
    struct Prepared {
        SourceCopyPlan plan;
        std::vector<std::size_t> route_indices;
    };
    struct Result {
        std::filesystem::path source;
        std::string importer;
        bool ok;
        std::string message;
    };
    State state_ = State::Choose;
    FileDialog dialog_;
    std::filesystem::path project_, dialog_project_;
    std::vector<std::filesystem::path> selected_;
    std::string destination_, error_;
    bool open_ = false, popup_ = false, cook_ = true, cancelled_ = false, copied_ = false;
    std::vector<AssetReimportRoute> profiles_;
    std::optional<Prepared> prepared_;
    std::vector<Result> results_;
    std::map<std::size_t, std::unique_ptr<AssetImportService>> services_;
    std::optional<std::pair<std::size_t, AssetJobId>> active_;
    std::size_t next_ = 0;
    std::stop_source stop_;
    std::future<Prepared> prepare_;
    std::future<void> copy_;
};
} // namespace forge
