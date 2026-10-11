#pragma once
#include "../animation_states_authoring.hpp"
#include "../texture_authoring.hpp"
#include "document.hpp"
#include "editor_state.hpp"
#include "property_drawer.hpp"
#include <forge/authoring.hpp>
#include <functional>
namespace forge {
class AnimationStatesEditor {
  public:
    bool close_cancelled = false;
    bool is_open() const { return bool(document_) || open_error_; }
    bool pending() const { return job_ != 0; }
    bool dirty() const { return document_ && (document_->dirty() || needs_publish_ || pending()); }
    bool can_undo() const { return document_ && !pending() && document_->can_undo(); }
    bool can_redo() const { return document_ && !pending() && document_->can_redo(); }
    const AnimationStatesDocument* document() const { return document_.get(); }
    std::shared_ptr<const AssetCatalog> take_catalog() { return std::exchange(published_, {}); }
    void request_save() { save_ = true; }
    void request_close() {
        close_ = true;
        if (!dirty())
            finish_close();
    }
    void undo() {
        if (can_undo()) {
            document_->undo();
            changed();
        }
    }
    void redo() {
        if (can_redo()) {
            document_->redo();
            changed();
        }
    }
    void open(SceneDocument& project, const std::filesystem::path& source) {
        if (dirty()) {
            next_source_ = source;
            request_close();
            return;
        }
        try {
            load(project, source);
        } catch (const std::exception& e) {
            error_ = e.what();
            open_error_ = true;
        }
    }
    void content(SceneDocument& project, bool locked) {
        ImGui::BeginDisabled(locked || dirty());
        if (ui::button("New animation states...", "Create a named clip set controlled by C++."))
            ImGui::OpenPopup("New animation states");
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal("New animation states", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Project file", path_, sizeof(path_));
            ImGui::BeginDisabled(locked);
            if (ui::button("Create",
                           "Create the source document; choose its skeleton and clips.")) {
                try {
                    auto created = AnimationStatesDocument::create(project.writer_guard(),
                                                                   std::filesystem::u8path(path_));
                    const auto source = created->locator();
                    created.reset();
                    load(project, source);
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& e) {
                    error_ = e.what();
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::button("Cancel", "Close without creating a file."))
                ImGui::CloseCurrentPopup();
            if (!error_.empty())
                ui::field_error(error_);
            ImGui::EndPopup();
        }
    }
    void poll(SceneDocument& project, std::string& message) {
        if (document_ && document_->project() != project.project()) {
            finish_close();
            next_source_.reset();
        }
        if (service_)
            for (auto& result : service_->poll()) {
                if (result.job.id != job_)
                    continue;
                job_ = 0;
                if (result.published) {
                    published_ = std::make_shared<const AssetCatalog>(result.publication->catalog);
                    needs_publish_ = false;
                    error_.clear();
                    message = "Animation states saved and published.";
                } else {
                    needs_publish_ = true;
                    close_ = false;
                    error_ = result.diagnostic;
                }
            }
        if (close_ && !dirty())
            finish_close();
        if (!document_ && next_source_) {
            auto source = std::exchange(next_source_, {});
            open(project, *source);
        }
    }
    void draw(SceneDocument& project, Scene& scene, bool locked,
              const std::function<void()>& preview = {}) {
        if (!document_) {
            if (open_error_) {
                if (ImGui::Begin("Animation States###Animation States", &open_error_)) {
                    ui::field_error(error_);
                    if (ui::button("Close", "Close the failed source document."))
                        request_close();
                }
                ImGui::End();
            }
            return;
        }
        if (save_ && !locked && !pending()) {
            save_ = false;
            publish();
        }
        bool opened = true;
        const bool expanded = ImGui::Begin("Animation States###Animation States", &opened);
        if (!opened)
            request_close();
        if (ui::editor_context && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            ui::editor_context->task.focus_document("animation_states",
                                                    path_utf8(document_->locator()));
        if (expanded && document_) {
            ImGui::TextWrapped("Name your clips here. C++ selects a stable state ID; the incoming "
                               "state's fade controls the transition.");
            ImGui::TextUnformatted(path_utf8(document_->locator()).c_str());
            ImGui::BeginDisabled(locked || pending());
            if (ui::button(
                    "Save / Publish",
                    "Save and cook the clip set. Failed publication keeps the last good version."))
                request_save();
            ImGui::SameLine();
            if (ui::button("Undo", "Undo state source changes.") && can_undo())
                undo();
            ImGui::SameLine();
            if (ui::button("Redo", "Redo state source changes.") && can_redo())
                redo();
            auto draft = document_->source().document;
            Json skeleton = draft.at("skeleton");
            if (asset_ref_picker(*catalog_, skeleton, SkeletonAsset::type, "Skeleton", false))
                mutate("Choose skeleton", [&](Json& d) { d["skeleton"] = skeleton; });
            ImGui::SeparatorText("States");
            for (const auto& state : draft.at("states")) {
                const auto id = state.at("id").get<std::uint32_t>();
                const auto label =
                    state.at("name").get<std::string>() + " (" + std::to_string(id) + ")";
                if (ImGui::Selectable(label.c_str(), selected_ == id))
                    select(id);
                FORGE_UI_PROBE("animation_states:state:" + std::to_string(id));
            }
            if (asset_ref_picker(*catalog_, new_clip_, AnimationClipAsset::type, "New state clip",
                                 false)) {
            }
            ImGui::BeginDisabled(new_clip_.is_null() || draft.at("states").size() >= 32 ||
                                 draft.at("next_id").get<std::uint64_t>() > UINT32_MAX);
            if (ui::button("Add state",
                           "Allocate a new stable ID. Deleted IDs are never reused.")) {
                const auto id = draft.at("next_id").get<std::uint64_t>();
                mutate("Add animation state", [&](Json& d) {
                    d["states"].push_back({{"id", id},
                                           {"name", "State " + std::to_string(id)},
                                           {"clip", new_clip_},
                                           {"loop", true},
                                           {"speed", 1},
                                           {"transition_seconds", .25}});
                    d["next_id"] = id + 1;
                    if (d.at("entry") == 0)
                        d["entry"] = id;
                });
                select(std::uint32_t(id));
            }
            ImGui::EndDisabled();
            draft = document_->source().document;
            for (std::size_t i = 0; i < draft.at("states").size(); ++i) {
                const auto state = draft.at("states")[i];
                if (state.at("id") != selected_)
                    continue;
                ImGui::SeparatorText("Selected state");
                ImGui::Text("Stable ID: %u", selected_);
                ImGui::InputText("Name", name_, sizeof(name_));
                FORGE_UI_PROBE("animation_states:name");
                if (ImGui::IsItemDeactivatedAfterEdit())
                    mutate_state("Rename state", "name", std::string(name_));
                auto clip = state.at("clip");
                if (asset_ref_picker(*catalog_, clip, AnimationClipAsset::type, "Clip", false) &&
                    !clip.is_null())
                    mutate_state("Choose clip", "clip", clip);
                bool loop = state.at("loop");
                float speed = state.at("speed"), fade = state.at("transition_seconds");
                if (ImGui::Checkbox("Loop", &loop))
                    mutate_state("Set state loop", "loop", loop);
                if (ImGui::SliderFloat("State speed", &speed, 0, 4))
                    mutate_state("Set state speed", "speed", speed);
                if (ImGui::SliderFloat("Fade in (seconds)", &fade, 0, 10, "%.3f"))
                    mutate_state("Set state fade", "transition_seconds", fade);
                FORGE_UI_PROBE("animation_states:fade");
                if (ui::button("Make entry state", "Use this state when Animator state is zero."))
                    mutate("Choose entry", [&](Json& d) { d["entry"] = selected_; });
                ImGui::SameLine();
                ImGui::BeginDisabled(i == 0);
                if (ui::button("Move up", "Reorder without changing IDs."))
                    mutate("Reorder states",
                           [&](Json& d) { std::swap(d["states"][i], d["states"][i - 1]); });
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(i + 1 == draft.at("states").size());
                if (ui::button("Move down", "Reorder without changing IDs."))
                    mutate("Reorder states",
                           [&](Json& d) { std::swap(d["states"][i], d["states"][i + 1]); });
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ui::button("Remove state", "References to this ID become an explicit error.")) {
                    mutate("Remove state", [&](Json& d) {
                        auto& states = d["states"];
                        states.erase(states.begin() + i);
                        if (d.at("entry") == selected_)
                            d["entry"] = states.empty() ? Json(0) : states.front().at("id");
                    });
                    selected_ = 0;
                }
                break;
            }
            ImGui::SeparatorText("Scene object");
            ImGui::BeginDisabled(needs_publish_ || document_->dirty());
            if (ui::button("Attach to selected Animator",
                           "Assign this set and entry state in one Scene Undo step."))
                attach(scene);
            ImGui::SameLine();
            if (preview && ui::button("Open Animation Preview",
                                      "Preview the selected object's state switches."))
                preview();
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            if (pending())
                ImGui::TextUnformatted("Publishing animation states...");
            else if (needs_publish_)
                ImGui::TextUnformatted("Source changes need Save / Publish.");
            if (!error_.empty())
                ui::field_error(error_);
        }
        ImGui::End();
        if (close_ && dirty())
            ImGui::OpenPopup("Pending animation state changes");
        if (ImGui::BeginPopupModal("Pending animation state changes", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Save and publish, discard the draft, or keep editing?");
            ImGui::BeginDisabled(locked || pending());
            if (ui::button("Save", "Publish before closing.")) {
                request_save();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ui::button("Discard", "Close without saving this draft.")) {
                finish_close();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::button("Keep editing", "Cancel closing.")) {
                close_ = false;
                next_source_.reset();
                close_cancelled = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

  private:
    std::unique_ptr<AnimationStatesDocument> document_;
    std::unique_ptr<AssetImportService> service_;
    std::shared_ptr<const AssetCatalog> catalog_, published_;
    std::optional<std::filesystem::path> next_source_;
    AssetJobId job_ = 0;
    bool needs_publish_ = false, save_ = false, close_ = false, open_error_ = false;
    std::uint32_t selected_ = 0;
    Json new_clip_ = nullptr;
    char path_[256] = "Assets/Character.states.json", name_[65]{};
    std::string error_;
    void finish_close() {
        document_.reset();
        service_.reset();
        catalog_.reset();
        new_clip_ = nullptr;
        job_ = 0;
        selected_ = 0;
        needs_publish_ = save_ = close_ = open_error_ = false;
    }
    void load(SceneDocument& project, const std::filesystem::path& source) {
        auto doc = std::make_unique<AnimationStatesDocument>(project.writer_guard(), source);
        auto catalog =
            std::make_shared<const AssetCatalog>(AssetCatalog::open_project(project.project()));
        auto service = std::make_unique<AssetImportService>(
            project.writer_guard(), animation_states_import_registry(), desktop_texture_target());
        const auto at = catalog->records().find(doc->source().asset());
        const auto bytes = asset_detail::read_bytes(ProjectPaths(project.project()).resolve(source),
                                                    animation_states_byte_limit);
        const bool needs = at == catalog->records().end() ||
                           !at->second.metadata.contains("forge.import") ||
                           at->second.metadata.at("forge.import").at("source_digest") !=
                               asset_detail::content_digest(bytes);
        finish_close();
        document_ = std::move(doc);
        service_ = std::move(service);
        catalog_ = std::move(catalog);
        needs_publish_ = needs;
        if (!document_->source().document.at("states").empty())
            select(document_->source().document.at("states").front().at("id"));
        error_.clear();
    }
    void changed() {
        needs_publish_ = true;
        if (selected_)
            select(selected_);
    }
    void select(std::uint32_t id) {
        selected_ = id;
        for (const auto& s : document_->source().document.at("states"))
            if (s.at("id") == id) {
                const auto name = s.at("name").get<std::string>();
                std::snprintf(name_, sizeof(name_), "%s", name.c_str());
                return;
            }
        selected_ = 0;
    }
    template <class F> void mutate(std::string label, F edit) {
        try {
            document_->edit(document_->revision(), std::move(label), edit);
            changed();
            error_.clear();
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
    void mutate_state(std::string label, const char* field, const Json& value) {
        mutate(std::move(label), [&](Json& d) {
            for (auto& s : d["states"])
                if (s.at("id") == selected_)
                    s[field] = value;
        });
    }
    void publish() {
        try {
            document_->save();
            job_ = service_->submit(
                service_->prepare(document_->locator(), {}, document_->source().asset()),
                [](auto& c, const auto& p, const auto&) {
                    prepare_animation_states_publication(c, p);
                },
                [](const auto&, const auto&) {});
        } catch (const std::exception& e) {
            needs_publish_ = true;
            close_ = false;
            error_ = e.what();
        }
    }
    void attach(Scene& scene) {
        try {
            if (!ui::editor_context)
                throw std::runtime_error("Choose a scene object with an Animator");
            const auto id = ui::editor_context->selection.entity();
            auto actor = scene.entity(id);
            if (!actor || !actor.has<Animator>())
                throw std::runtime_error("Choose a scene object with an Animator");
            auto commands = Json::array();
            const Json values{{"states", document_->source().asset()},
                              {"state", 0},
                              {"blend_clip", nullptr},
                              {"blend_weight", 0}};
            // Clear weight before clearing its clip; each staged property must remain valid.
            for (const auto* field : {"blend_weight", "blend_clip", "states", "state"}) {
                const auto& value = values.at(field);
                commands.push_back({{"operation", "property.set"},
                                    {"arguments",
                                     {{"entity", id},
                                      {"component", "forge.animator"},
                                      {"field", field},
                                      {"value", value}}}});
            }
            apply_authoring(scene, commands, scene.revision());
            error_.clear();
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
};
} // namespace forge
