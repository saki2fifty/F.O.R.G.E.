#pragma once
#include "../animation_states_authoring.hpp"
#include "../texture_authoring.hpp"
#include "animation_state_graph.hpp"
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
    bool dirty() const {
        return document_ &&
               (document_->dirty() || graph_.draft_dirty() || needs_publish_ || pending());
    }
    bool can_undo() const {
        return document_ && !pending() && (graph_.draft_dirty() || document_->can_undo());
    }
    bool can_redo() const {
        return document_ && !pending() && !graph_.draft_dirty() && document_->can_redo();
    }
    const AnimationStatesDocument* document() const { return document_.get(); }
    std::shared_ptr<const AssetCatalog> take_catalog() { return std::exchange(published_, {}); }
    void request_save() { save_ = true; }
    void request_close() {
        if (document_)
            graph_.flush(graph_edit());
        close_ = true;
        if (!dirty())
            finish_close();
    }
    void undo() {
        if (graph_.draft_dirty()) {
            graph_.cancel();
            return;
        }
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
                    catalog_ = published_;
                    refresh_owner();
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
            if (graph_.flush(graph_edit()))
                publish();
        }
        bool opened = true;
        const bool expanded = ImGui::Begin("Animation States###Animation States", &opened);
        if (!opened)
            request_close();
        if (document_ && ui::editor_context &&
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            ui::editor_context->task.focus_document("animation_states",
                                                    path_utf8(document_->locator()));
        if (expanded && document_) {
            ImGui::TextWrapped("Name your clips here. C++ or an exit transition selects a stable "
                               "state ID; the incoming state's fade controls the transition.");
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
            ImGui::EndDisabled();
            graph_.draw(draft, document_->revision(), selected_, locked || pending(), graph_edit(),
                        [&](std::uint32_t id) { select(id); });
            ImGui::BeginDisabled(locked || pending());
            draft = document_->source().document;
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
                                 false, owner_)) {
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
                if (asset_ref_picker(*catalog_, clip, AnimationClipAsset::type, "Clip", false,
                                     owner_) &&
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
                draw_exit(draft, state);
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
                    if (mutate("Remove state", [&](Json& d) {
                            auto& states = d["states"];
                            states.erase(states.begin() + i);
                            asset_detail::remove_state_graph_position(d, selected_);
                            if (d.at("entry") == selected_)
                                d["entry"] = states.empty() ? Json(0) : states.front().at("id");
                        }))
                        selected_ = 0;
                }
                if (selected_)
                    draw_markers();
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
    AnimationStateGraph graph_;
    std::optional<AssetId> owner_;
    std::optional<std::filesystem::path> next_source_;
    AssetJobId job_ = 0;
    bool needs_publish_ = false, save_ = false, close_ = false, open_error_ = false;
    std::uint32_t selected_ = 0, marker_selected_ = 0;
    char marker_name_[65]{};
    Json new_clip_ = nullptr;
    char path_[256] = "Assets/Character.states.json", name_[65]{};
    std::string error_;
    void finish_close() {
        graph_.reset();
        document_.reset();
        service_.reset();
        catalog_.reset();
        owner_ = AssetId{};
        new_clip_ = nullptr;
        job_ = 0;
        selected_ = marker_selected_ = 0;
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
        refresh_owner();
        if (!document_->source().document.at("states").empty())
            select(document_->source().document.at("states").front().at("id"));
        error_.clear();
    }
    void changed() {
        needs_publish_ = true;
        refresh_owner();
        if (selected_)
            select(selected_);
    }
    void refresh_owner() {
        owner_ = AssetId{};
        if (!document_ || !catalog_)
            return;
        const auto& skeleton = document_->source().document.at("skeleton");
        if (skeleton.is_null())
            return;
        const auto resolved = catalog_->resolve(skeleton.get<AssetId>(), SkeletonAsset::type);
        if (resolved.state == AssetState::Available && resolved.record->subasset)
            owner_ = resolved.record->subasset->owner;
    }
    AnimationStateGraph::Edit graph_edit() {
        return [this](std::uint64_t revision, std::string label,
                      const std::function<void(Json&)>& mutation) {
            try {
                if (!document_ || pending())
                    return false;
                document_->edit(revision, std::move(label), mutation);
                changed();
                error_.clear();
                return true;
            } catch (const std::exception& e) {
                error_ = e.what();
                return false;
            }
        };
    }
    void select(std::uint32_t id) {
        if (selected_ != id)
            marker_selected_ = 0;
        selected_ = id;
        for (const auto& s : document_->source().document.at("states"))
            if (s.at("id") == id) {
                const auto name = s.at("name").get<std::string>();
                std::snprintf(name_, sizeof(name_), "%s", name.c_str());
                select_marker(marker_selected_);
                return;
            }
        selected_ = 0;
    }
    template <class F> bool mutate(std::string label, F edit) {
        try {
            document_->edit(document_->revision(), std::move(label), edit);
            changed();
            error_.clear();
            return true;
        } catch (const std::exception& e) {
            error_ = e.what();
            return false;
        }
    }
    void draw_exit(const Json& draft, const Json& state) {
        const bool versioned = draft.at("version") == 2;
        bool enabled = versioned && state.contains("exit") && !state.at("exit").is_null();
        const auto other = std::find_if(draft.at("states").begin(), draft.at("states").end(),
                                        [&](const auto& row) { return row.at("id") != selected_; });
        ImGui::SeparatorText("Exit transition");
        ImGui::BeginDisabled(other == draft.at("states").end());
        if (ImGui::Checkbox("Advance automatically", &enabled))
            mutate("Set automatic transition", [&](Json& d) {
                d["version"] = 2;
                for (auto& row : d["states"])
                    if (row.at("id") == selected_)
                        row["exit"] = enabled ? Json{{"state", other->at("id")}, {"phase", 1}}
                                              : Json(nullptr);
            });
        FORGE_UI_PROBE("animation_states:exit_enabled");
        ImGui::EndDisabled();
        if (!versioned)
            ImGui::TextWrapped("Enabling converts this source to version 2 through Undo history. "
                               "Legacy extension data must be renamed if it uses the exit field.");
        if (!versioned || !state.contains("exit") || state.at("exit").is_null())
            return;
        auto exit = state.at("exit");
        std::string label;
        for (const auto& row : draft.at("states"))
            if (row.at("id") == exit.at("state"))
                label = row.at("name").template get<std::string>() + " (" +
                        std::to_string(row.at("id").template get<std::uint32_t>()) + ")";
        if (ImGui::BeginCombo("Next state", label.c_str())) {
            for (const auto& row : draft.at("states")) {
                if (row.at("id") == selected_)
                    continue;
                const auto id = row.at("id").template get<std::uint32_t>();
                const auto choice =
                    row.at("name").template get<std::string>() + " (" + std::to_string(id) + ")";
                if (ImGui::Selectable(choice.c_str(), exit.at("state") == id)) {
                    exit["state"] = id;
                    mutate_state("Choose exit target", "exit", exit);
                }
                FORGE_UI_PROBE("animation_states:exit_choice:" + std::to_string(id));
            }
            ImGui::EndCombo();
        }
        FORGE_UI_PROBE("animation_states:exit_target");
        float phase = exit.at("phase");
        if (ImGui::SliderFloat("Exit time", &phase, .001f, 1, "%.3f")) {
            exit["phase"] = phase;
            mutate_state("Set exit time", "exit", exit);
        }
        FORGE_UI_PROBE("animation_states:exit_phase");
        ImGui::TextWrapped("Normalized clip time: 1 is the end. Changes occur at fixed ticks. "
                           "The next state's Fade in controls blending.");
    }
    void mutate_state(std::string label, const char* field, const Json& value) {
        mutate(std::move(label), [&](Json& d) {
            for (auto& s : d["states"])
                if (s.at("id") == selected_)
                    s[field] = value;
        });
    }
    void select_marker(std::uint32_t id) {
        marker_selected_ = 0;
        marker_name_[0] = 0;
        for (const auto& state : document_->source().document.at("states"))
            if (state.at("id") == selected_ && state.contains("markers"))
                for (const auto& marker : state.at("markers"))
                    if (marker.at("id") == id) {
                        marker_selected_ = id;
                        const auto name = marker.at("name").get<std::string>();
                        std::snprintf(marker_name_, sizeof(marker_name_), "%s", name.c_str());
                        return;
                    }
    }
    template <class F> void mutate_markers(std::string label, F edit) {
        mutate(std::move(label), [&](Json& draft) {
            for (auto& state : draft["states"])
                if (state.at("id") == selected_) {
                    if (!state.contains("markers"))
                        state["markers"] = Json::array();
                    edit(draft, state["markers"]);
                }
        });
    }
    void mutate_marker(std::string label, const char* field, const Json& value) {
        mutate_markers(std::move(label), [&](Json&, Json& markers) {
            for (auto& marker : markers)
                if (marker.at("id") == marker_selected_)
                    marker[field] = value;
        });
    }
    void draw_markers() {
        const auto draft = document_->source().document;
        Json markers = Json::array();
        std::size_t total = 0;
        for (const auto& state : draft.at("states")) {
            total += state.contains("markers") ? state.at("markers").size() : 0;
            if (state.at("id") == selected_ && state.contains("markers"))
                markers = state.at("markers");
        }
        ImGui::SeparatorText("Timing markers");
        ImGui::TextDisabled("C++ observes markers during Play. Preview is muted.");
        const auto next = draft.value("next_marker_id", std::uint64_t(1));
        ImGui::BeginDisabled(markers.size() >= 64 || total >= 512 || next > UINT32_MAX);
        if (ui::button("Add marker", "Create a stable marker ID at half of the clip.")) {
            mutate_markers("Add animation marker", [&](Json& d, Json& list) {
                d["next_marker_id"] = next + 1;
                list.push_back({{"id", next}, {"name", "Marker"}, {"phase", .5}, {"value", 0.}});
            });
            select_marker(std::uint32_t(next));
        }
        FORGE_UI_PROBE("animation_states:add_marker");
        ImGui::EndDisabled();
        for (const auto& marker : markers) {
            const auto id = marker.at("id").get<std::uint32_t>();
            const auto label =
                marker.at("name").get<std::string>() + "##marker" + std::to_string(id);
            if (ImGui::Selectable(label.c_str(), marker_selected_ == id))
                select_marker(id);
        }
        for (const auto& state : document_->source().document.at("states"))
            if (state.at("id") == selected_ && state.contains("markers"))
                for (const auto& marker : state.at("markers")) {
                    if (marker.at("id") != marker_selected_)
                        continue;
                    const auto current = marker;
                    ImGui::Text("Marker ID: %u", marker_selected_);
                    ImGui::InputText("Marker name", marker_name_, sizeof(marker_name_));
                    FORGE_UI_PROBE("animation_states:marker_name");
                    if (ImGui::IsItemDeactivatedAfterEdit())
                        mutate_marker("Rename animation marker", "name", std::string(marker_name_));
                    float phase = current.at("phase");
                    if (ImGui::SliderFloat("Clip phase", &phase, 0, 1, "%.3f"))
                        mutate_marker("Move animation marker", "phase", phase);
                    FORGE_UI_PROBE("animation_states:marker_phase");
                    double value = current.at("value");
                    if (ImGui::InputDouble("Marker value", &value, 0, 0, "%.3f",
                                           ImGuiInputTextFlags_EnterReturnsTrue))
                        mutate_marker("Set animation marker value", "value", value);
                    FORGE_UI_PROBE("animation_states:marker_value");
                    if (ui::button("Remove marker",
                                   "Remove this occurrence without recycling its ID.")) {
                        mutate_markers("Remove animation marker", [&](Json&, Json& list) {
                            for (auto i = list.begin(); i != list.end(); ++i)
                                if (i->at("id") == marker_selected_) {
                                    list.erase(i);
                                    break;
                                }
                        });
                        select_marker(0);
                    }
                    FORGE_UI_PROBE("animation_states:remove_marker");
                    return;
                }
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
                              {"blend_root", nullptr},
                              {"blend_mask", nullptr},
                              {"blend_space", nullptr},
                              {"blend_mode", 0},
                              {"blend_weight", 0}};
            // Clear weight before clearing its clip; each staged property must remain valid.
            for (const auto* field : {"blend_weight", "blend_root", "blend_mask", "blend_space",
                                      "blend_mode", "blend_clip", "states", "state"}) {
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
