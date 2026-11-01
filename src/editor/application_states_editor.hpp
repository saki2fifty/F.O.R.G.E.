#pragma once
#include "../application_state_authoring.hpp"
#include "../texture_authoring.hpp"
#include "editor_state.hpp"
#include "source_document_panel.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <forge/authoring.hpp>
#include <string_view>

namespace forge {
class ApplicationStatesEditor : public SourceDocumentPanel<ApplicationStatesDocumentTraits> {
    friend struct AssetDocumentAutomation;

  public:
    ApplicationStatesEditor()
        : SourceDocumentPanel("Application states", application_states_import_registry,
                              prepare_application_states_publication, desktop_texture_target()) {}
    bool has_unapplied_edits() const { return draft_open_; }
    void content(SceneDocument& project, bool locked) {
        ImGui::BeginDisabled(locked || dirty());
        if (ui::button("New application states...",
                       "Create logical game modes and their conditions."))
            ImGui::OpenPopup("New application states");
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal("New application states", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Project file", path_, sizeof(path_));
            ImGui::BeginDisabled(locked);
            if (ui::button("Create", "Create Menu, Playing and Game over states.")) {
                try {
                    auto created = ApplicationStatesDocument::create(
                        project.writer_guard(), std::filesystem::u8path(path_));
                    const auto locator = created->locator();
                    created.reset();
                    load(project, locator);
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& e) {
                    error_ = e.what();
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::button("Cancel", "Close without creating a source."))
                ImGui::CloseCurrentPopup();
            if (!error_.empty())
                ui::field_error(error_);
            ImGui::EndPopup();
        }
    }
    void draw(SceneDocument&, Scene& scene, bool locked) {
        if (!document_ && !open_error_)
            return;
        if (save_ && !locked && !pending()) {
            save_ = false;
            if (flush_gesture())
                publish();
        }
        bool open = true;
        const bool expanded = ImGui::Begin("Application States###Application States", &open);
        if (!open)
            request_close();
        if (document_ && ui::editor_context &&
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            ui::editor_context->task.focus_document("application_states",
                                                    path_utf8(document_->locator()));
        if (expanded && document_) {
            ImGui::TextWrapped(
                "Domains hold logical game modes. Systems opt into state conditions; "
                "a state named paused does not pause the engine clock.");
            ImGui::TextUnformatted(path_utf8(document_->locator()).c_str());
            ImGui::BeginDisabled(locked || pending());
            if (ui::button("Save / Publish", "Save and cook this definition for Play and export."))
                request_save();
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_undo());
            if (ui::button("Undo", "Undo source edits or cancel the current text draft."))
                undo();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_redo());
            if (ui::button("Redo", "Redo source edits."))
                redo();
            ImGui::EndDisabled();
            draw_domains();
            ImGui::SeparatorText("Scene controller");
            ImGui::TextWrapped("Assign one published definition to one object in this Scene. "
                               "Active states are runtime values; Stop restores a fresh game.");
            ImGui::BeginDisabled(needs_publish_ || document_->dirty() || gesture_dirty());
            if (ui::button("Attach to selected object",
                           "Assign this definition as one Scene Undo step."))
                attach(scene);
            FORGE_UI_PROBE("application_states:attach");
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            draw_draft(locked || pending());
        }
        if (expanded && !error_.empty())
            ui::field_error(error_);
        ImGui::End();
        draw_close_dialog("Pending application-state changes", locked);
    }

  private:
    enum class Draft { DomainKey, DomainLabel, NewDomain, StateKey, StateLabel, NewState };
    char path_[256] = "Assets/Game.appstates.json";
    std::array<char, 97> text_{};
    std::string domain_, state_;
    Draft draft_ = Draft::NewDomain;
    bool draft_open_ = false, popup_requested_ = false;
    std::uint64_t draft_revision_ = 0;
    ImGuiID text_id_ = 0;
    static std::size_t index(const Json& rows, const std::string& key) {
        for (std::size_t i = 0; i < rows.size(); ++i)
            if (rows[i].at("key") == key)
                return i;
        throw std::runtime_error("The selected state/domain no longer exists");
    }
    bool gesture_dirty() const override { return draft_open_; }
    void cancel_gesture() override {
        if (ImGui::GetCurrentContext() && text_id_ && ImGui::GetActiveID() == text_id_)
            ImGui::ClearActiveID();
        draft_open_ = popup_requested_ = false;
    }
    void view_closed() override {
        cancel_gesture();
        domain_.clear();
        state_.clear();
    }
    void source_opened() override { reconcile(); }
    void changed() override {
        cancel_gesture();
        needs_publish_ = true;
        reconcile();
    }
    void reconcile() {
        if (!document_)
            return;
        const auto& domains = document_->source().document.at("domains");
        auto selected = std::find_if(domains.begin(), domains.end(),
                                     [&](const auto& row) { return row.at("key") == domain_; });
        if (selected == domains.end()) {
            domain_ = domains[0].at("key");
            selected = domains.begin();
        }
        const auto& states = selected->at("states");
        if (std::none_of(states.begin(), states.end(),
                         [&](const auto& row) { return row.at("key") == state_; }))
            state_ = states[0].at("key");
    }
    void begin_draft(Draft kind, const std::string& value) {
        draft_ = kind;
        draft_open_ = popup_requested_ = true;
        draft_revision_ = document_->revision();
        std::snprintf(text_.data(), text_.size(), "%s", value.c_str());
    }
    static void rename_condition(Json& condition, const std::string& domain,
                                 const std::string& old_state, const std::string& value) {
        if (condition.at("domain") == domain && condition.at("state") == old_state)
            condition["state"] = value;
    }
    bool flush_gesture() override {
        if (!draft_open_)
            return true;
        const auto kind = draft_;
        const auto domain = domain_, state = state_, value = std::string(text_.data());
        const bool accepted =
            edit_source(draft_revision_, "Edit application state", [=](Json& source) {
                auto& domains = source.at("domains");
                if (kind == Draft::NewDomain) {
                    domains.push_back(
                        {{"key", value},
                         {"label", value},
                         {"initial", "idle"},
                         {"states", Json::array({{{"key", "idle"}, {"label", "Idle"}}})}});
                    return;
                }
                auto& selected = domains.at(index(domains, domain));
                if (kind == Draft::DomainLabel) {
                    selected["label"] = value;
                    return;
                }
                if (kind == Draft::DomainKey) {
                    selected["key"] = value;
                    for (auto& row : domains) {
                        if (row.contains("enabled_when") &&
                            row["enabled_when"].at("domain") == domain)
                            row["enabled_when"]["domain"] = value;
                        if (row.contains("rules"))
                            for (auto& rule : row["rules"])
                                for (auto& condition : rule.at("all"))
                                    if (condition.at("domain") == domain)
                                        condition["domain"] = value;
                    }
                    return;
                }
                auto& states = selected.at("states");
                if (kind == Draft::NewState) {
                    states.push_back({{"key", value}, {"label", value}});
                    return;
                }
                auto& node = states.at(index(states, state));
                if (kind == Draft::StateLabel) {
                    node["label"] = value;
                    return;
                }
                node["key"] = value;
                if (selected.at("initial") == state)
                    selected["initial"] = value;
                for (auto& other : states)
                    if (other.value("parent", std::string{}) == state)
                        other["parent"] = value;
                if (selected.contains("rules"))
                    for (auto& rule : selected["rules"])
                        if (rule.at("state") == state)
                            rule["state"] = value;
                for (auto& row : domains) {
                    if (row.contains("enabled_when"))
                        rename_condition(row["enabled_when"], domain, state, value);
                    if (row.contains("rules"))
                        for (auto& rule : row["rules"])
                            for (auto& condition : rule.at("all"))
                                rename_condition(condition, domain, state, value);
                }
            });
        if (accepted) {
            if (kind == Draft::DomainKey || kind == Draft::NewDomain)
                domain_ = value;
            if (kind == Draft::StateKey || kind == Draft::NewState)
                state_ = value;
            reconcile();
        }
        return accepted;
    }
    template <class F> bool mutate(const char* label, F edit) {
        return flush_gesture() && edit_source(document_->revision(), label, edit);
    }
    template <class F> static void add_dependency(Json& source, std::size_t owner, F apply) {
        std::string reason = "Create another domain for this condition";
        for (std::size_t i = 0; i < source.at("domains").size(); ++i) {
            if (i == owner)
                continue;
            auto candidate = source;
            const auto& target = source["domains"][i];
            apply(candidate,
                  Json{{"domain", target["key"]}, {"state", target["states"][0]["key"]}});
            try {
                ApplicationStatesSource{candidate}.validate();
            } catch (const std::exception& e) {
                reason = e.what();
                continue;
            }
            source = std::move(candidate);
            return;
        }
        throw std::runtime_error("Cannot add a state condition: " + reason);
    }
    void draw_draft(bool locked) {
        if (popup_requested_) {
            ImGui::OpenPopup("Edit application-state value");
            popup_requested_ = false;
        }
        if (!ImGui::BeginPopupModal("Edit application-state value", nullptr,
                                    ImGuiWindowFlags_AlwaysAutoResize))
            return;
        if (!draft_open_) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        const bool label = draft_ == Draft::DomainLabel || draft_ == Draft::StateLabel;
        ImGui::TextWrapped(label ? "Display label (up to 96 UTF-8 bytes)."
                                 : "Semantic key: lowercase ASCII letter, then letters, digits or "
                                   "underscores (up to 48 bytes).");
        ImGui::BeginDisabled(locked);
        ImGui::InputText("Value", text_.data(), label ? text_.size() : 49);
        text_id_ = ImGui::GetItemID();
        FORGE_UI_PROBE("application_states:text");
        if (ui::button("Apply", "Validate and apply as one source Undo step.") && flush_gesture())
            ImGui::CloseCurrentPopup();
        FORGE_UI_PROBE("application_states:apply_text");
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ui::button("Cancel", "Discard this text draft.")) {
            cancel_gesture();
            error_.clear();
            ImGui::CloseCurrentPopup();
        }
        if (!error_.empty())
            ui::field_error(error_);
        ImGui::EndPopup();
    }
    void draw_domains() {
        const auto source = document_->source().document;
        const auto& domains = source.at("domains");
        const auto selected = index(domains, domain_);
        const auto& domain = domains[selected];
        ImGui::SeparatorText("Domains");
        if (ImGui::BeginCombo("Domain", domain_.c_str())) {
            for (const auto& row : domains) {
                const auto key = row.at("key").get<std::string>();
                if (ImGui::Selectable(key.c_str(), key == domain_)) {
                    domain_ = key;
                    reconcile();
                }
                FORGE_UI_PROBE("application_states:domain:" + key);
            }
            ImGui::EndCombo();
        }
        FORGE_UI_PROBE("application_states:domain");
        if (domain_ != domain.at("key").get<std::string>())
            return;
        if (ui::button("Add domain", "Add an independent domain with an Idle state."))
            begin_draft(Draft::NewDomain, "mode");
        ImGui::SameLine();
        if (ui::button("Rename domain", "Update all recognized domain conditions in one edit."))
            begin_draft(Draft::DomainKey, domain_);
        ImGui::SameLine();
        if (ui::button("Edit domain label", "Change its display label."))
            begin_draft(Draft::DomainLabel, domain.value("label", domain_));
        ImGui::SameLine();
        ImGui::BeginDisabled(domains.size() == 1);
        const bool removed =
            ui::button("Remove domain",
                       "Update referencing conditions before removing a domain.") &&
            mutate("Remove application state domain",
                   [&](Json& d) { d["domains"].erase(selected); });
        ImGui::EndDisabled();
        if (removed || domain_ != domain.at("key").get<std::string>())
            return;
        bool computed = domain.value("computed", false);
        const bool mode_changed = ImGui::Checkbox("Computed", &computed);
        FORGE_UI_PROBE("application_states:computed");
        if (mode_changed) {
            mutate("Choose state domain mode", [&](Json& d) {
                auto& row = d["domains"][selected];
                row["computed"] = computed;
                if (!computed)
                    row.erase("rules");
            });
            return;
        }
        ImGui::TextWrapped(
            "Direct domains accept requests. Computed domains choose their first matching rule or "
            "initial state. Turning computed mode off clears its rules (Undo is available).");
        const auto base = "/domains/" + std::to_string(selected);
        if (state_combo("Initial state", domain.at("states"), domain.at("initial"),
                        base + "/initial"))
            return;
        draw_gate(source, selected);
        draw_states(source, selected);
        if (computed)
            draw_rules(source, selected);
    }
    bool state_combo(const char* label, const Json& states, const std::string& current,
                     const std::string& pointer, bool none = false,
                     std::string_view excluded = {}) {
        bool changed = false;
        const bool expanded = ImGui::BeginCombo(label, current.empty() ? "None" : current.c_str());
        FORGE_UI_PROBE("application_states:combo:" + pointer);
        if (expanded) {
            if (none && ImGui::Selectable("None", current.empty()))
                changed = mutate("Choose application state",
                                 [&](Json& d) { d[Json::json_pointer(pointer)] = ""; });
            for (const auto& state : states) {
                const auto key = state.at("key").get<std::string>();
                if (key == excluded)
                    continue;
                if (ImGui::Selectable(key.c_str(), key == current))
                    changed = mutate("Choose application state",
                                     [&](Json& d) { d[Json::json_pointer(pointer)] = key; });
                FORGE_UI_PROBE("application_states:choice:" + pointer + ":" + key);
            }
            ImGui::EndCombo();
        }
        return changed;
    }
    bool condition(const Json& source, std::size_t owner, const Json& value,
                   const std::string& pointer) {
        bool changed = false;
        const auto& domains = source.at("domains");
        const auto current = value.at("domain").get<std::string>();
        if (ImGui::BeginCombo("Domain condition", current.c_str())) {
            for (std::size_t i = 0; i < domains.size(); ++i)
                if (i != owner) {
                    const auto key = domains[i].at("key").get<std::string>();
                    if (ImGui::Selectable(key.c_str(), key == current))
                        changed = mutate("Choose state condition domain", [&](Json& d) {
                            d[Json::json_pointer(pointer)] = {
                                {"domain", key}, {"state", domains[i]["states"][0]["key"]}};
                        });
                }
            ImGui::EndCombo();
        }
        if (changed)
            return true;
        return state_combo("Required state (including descendants)",
                           domains[index(domains, current)].at("states"), value.at("state"),
                           pointer + "/state");
    }
    void draw_gate(const Json& source, std::size_t selected) {
        const auto& domains = source.at("domains");
        const auto& domain = domains[selected];
        bool enabled = domain.contains("enabled_when");
        ImGui::BeginDisabled(domains.size() < 2);
        const bool toggled = ImGui::Checkbox("Subordinate to another domain", &enabled);
        FORGE_UI_PROBE("application_states:gate");
        ImGui::EndDisabled();
        if (toggled) {
            mutate("Choose subordinate state condition", [&](Json& d) {
                auto& row = d["domains"][selected];
                if (!enabled)
                    row.erase("enabled_when");
                else
                    add_dependency(d, selected, [&](Json& candidate, const Json& value) {
                        candidate["domains"][selected]["enabled_when"] = value;
                    });
            });
            return;
        }
        if (enabled) {
            ImGui::PushID("gate");
            condition(source, selected, domain.at("enabled_when"),
                      "/domains/" + std::to_string(selected) + "/enabled_when");
            ImGui::PopID();
            ImGui::TextWrapped("Inactive while its condition is false; reactivation starts at the "
                               "initial state. Dependency cycles are rejected.");
        }
    }
    void draw_states(const Json& source, std::size_t selected) {
        const auto& domain = source.at("domains")[selected];
        const auto& states = domain.at("states");
        ImGui::SeparatorText("States");
        if (ImGui::BeginCombo("State", state_.c_str())) {
            for (const auto& row : states) {
                const auto key = row.at("key").get<std::string>();
                if (ImGui::Selectable(key.c_str(), key == state_))
                    state_ = key;
                FORGE_UI_PROBE("application_states:state:" + key);
            }
            ImGui::EndCombo();
        }
        FORGE_UI_PROBE("application_states:state");
        const auto chosen = index(states, state_);
        if (ui::button("Add state", "Add a semantic state key."))
            begin_draft(Draft::NewState, "state");
        ImGui::SameLine();
        if (ui::button("Rename state",
                       "Update initial state, parents, rules and conditions atomically."))
            begin_draft(Draft::StateKey, state_);
        ImGui::SameLine();
        if (ui::button("Edit state label", "Change this state's display label."))
            begin_draft(Draft::StateLabel, states[chosen].value("label", state_));
        ImGui::SameLine();
        ImGui::BeginDisabled(states.size() == 1);
        const bool removed =
            ui::button("Remove state",
                       "Update initial state, parent and condition references before removal.") &&
            mutate("Remove application state",
                   [&](Json& d) { d["domains"][selected]["states"].erase(chosen); });
        ImGui::EndDisabled();
        if (removed)
            return;
        state_combo("Parent state", states, states[chosen].value("parent", std::string{}),
                    "/domains/" + std::to_string(selected) + "/states/" + std::to_string(chosen) +
                        "/parent",
                    true, state_);
    }
    void draw_rules(const Json& source, std::size_t selected) {
        const auto& domains = source.at("domains");
        const auto& domain = domains[selected];
        ImGui::SeparatorText("Computed rules");
        ImGui::TextWrapped("First matching rule wins. Every condition in a rule must match.");
        const auto rules = domain.value("rules", Json::array());
        for (std::size_t i = 0; i < rules.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const auto pointer =
                "/domains/" + std::to_string(selected) + "/rules/" + std::to_string(i);
            bool changed = state_combo("Output state", domain.at("states"), rules[i].at("state"),
                                       pointer + "/state");
            for (std::size_t j = 0; j < rules[i].at("all").size() && !changed; ++j) {
                ImGui::PushID(static_cast<int>(j));
                changed = condition(source, selected, rules[i]["all"][j],
                                    pointer + "/all/" + std::to_string(j));
                ImGui::BeginDisabled(rules[i]["all"].size() == 1);
                if (ui::button("Remove condition", "Remove one AND condition."))
                    changed = mutate("Remove state condition", [&](Json& d) {
                        d[Json::json_pointer(pointer)]["all"].erase(j);
                    });
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::BeginDisabled(rules[i]["all"].size() >= 8);
            if (ui::button("Add condition", "Duplicate a condition, then choose its domain/state."))
                changed = mutate("Add state condition", [&](Json& d) {
                    d[Json::json_pointer(pointer)]["all"].push_back(rules[i]["all"][0]);
                });
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::button("Remove rule", "Remove this computation rule."))
                changed = mutate("Remove computed state rule",
                                 [&](Json& d) { d["domains"][selected]["rules"].erase(i); });
            ImGui::SameLine();
            ImGui::BeginDisabled(i == 0);
            if (ui::button("Move up", "Evaluate this rule before the preceding rule."))
                changed = mutate("Reorder computed state rules", [&](Json& d) {
                    std::swap(d["domains"][selected]["rules"][i],
                              d["domains"][selected]["rules"][i - 1]);
                });
            ImGui::EndDisabled();
            ImGui::PopID();
            if (changed)
                return;
        }
        ImGui::BeginDisabled(domains.size() < 2 || rules.size() >= 64);
        if (ui::button("Add rule", "Add an output with one condition on another domain."))
            mutate("Add computed state rule", [&](Json& d) {
                add_dependency(d, selected, [&](Json& candidate, const Json& value) {
                    auto& row = candidate["domains"][selected];
                    if (!row.contains("rules"))
                        row["rules"] = Json::array();
                    row["rules"].push_back(
                        {{"state", domain["initial"]}, {"all", Json::array({value})}});
                });
            });
        ImGui::EndDisabled();
    }
    void attach(Scene& scene) {
        try {
            const auto selected =
                ui::editor_context ? ui::editor_context->selection.entity() : std::string{};
            const auto actor = scene.entity(selected);
            if (!actor || actor.has(flecs::Prefab))
                throw std::runtime_error("Choose an ordinary Scene object for the controller");
            bool duplicate = false;
            scene.world().each([&](flecs::entity other, const ApplicationStates& value) {
                if (other != actor && !other.has(flecs::Prefab) &&
                    other.has<SceneMember>(scene.membership()) && value.definition.id)
                    duplicate = true;
            });
            if (duplicate)
                throw std::runtime_error(
                    "This Scene already has a configured Application States controller");
            auto commands = Json::array();
            if (!actor.has<ApplicationStates>())
                commands.push_back(
                    {{"operation", "component.add"},
                     {"arguments",
                      {{"entity", selected}, {"component", "forge.application_states"}}}});
            commands.push_back({{"operation", "property.set"},
                                {"arguments",
                                 {{"entity", selected},
                                  {"component", "forge.application_states"},
                                  {"field", "definition"},
                                  {"value", document_->source().asset()}}}});
            apply_authoring(scene, commands, scene.revision());
            error_.clear();
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
};
} // namespace forge
