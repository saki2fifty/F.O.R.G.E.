#pragma once
#include "../texture_authoring.hpp"
#include "editor_state.hpp"
#include "navigation_behavior_graph.hpp"
#include "source_document_panel.hpp"
#include <forge/authoring.hpp>
#include <forge/navigation_components.hpp>

namespace forge {
class NavigationBehaviorEditor : public SourceDocumentPanel<NavigationBehaviorDocumentTraits> {
    friend struct AssetDocumentAutomation;

  public:
    NavigationBehaviorEditor()
        : SourceDocumentPanel("Navigation behavior", navigation_behavior_import_registry,
                              prepare_navigation_behavior_publication, desktop_texture_target()) {}
    void content(SceneDocument& project, bool locked) {
        ImGui::BeginDisabled(locked || dirty());
        if (ui::button("New navigation behavior...",
                       "Create a navigation state graph or behavior tree."))
            ImGui::OpenPopup("New navigation behavior");
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal("New navigation behavior", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Project file", path_, sizeof(path_));
            const char* profiles[]{"State graph", "Behavior tree"};
            ImGui::Combo("Profile", &new_profile_, profiles, 2);
            FORGE_UI_PROBE("navigation_behavior:profile");
            ImGui::BeginDisabled(locked);
            if (ui::button("Create", new_profile_ == 1 ? "Create a tree with one Wait node."
                                                       : "Create a graph with one Wait state.")) {
                try {
                    auto created = NavigationBehaviorDocument::create(
                        project.writer_guard(), std::filesystem::u8path(path_),
                        new_profile_ == 1 ? NavigationBehaviorSource::create_tree
                                          : NavigationBehaviorSource::create);
                    const auto path = created->locator();
                    created.reset();
                    load(project, path);
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
    void draw(SceneDocument&, Scene& scene, bool locked) {
        if (!document_) {
            if (open_error_) {
                if (ImGui::Begin("Navigation Behavior###Navigation Behavior", &open_error_)) {
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
            if (flush_gesture())
                publish();
        }
        bool opened = true;
        const bool expanded = ImGui::Begin("Navigation Behavior###Navigation Behavior", &opened);
        if (!opened)
            request_close();
        if (document_ && ui::editor_context &&
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            ui::editor_context->task.focus_document("navigation_behavior",
                                                    path_utf8(document_->locator()));
        if (expanded && document_) {
            const bool tree = document_->source().document.at("version") == 2;
            ImGui::TextWrapped(
                tree
                    ? "A tree chooses Navigation Agent intent. Sequences retain their running "
                      "child; "
                      "Reactive sequences and Selectors reconsider earlier children. Conditions do "
                      "not move the object. Root completion restarts on the next fixed tick."
                    : "States choose Navigation Agent destinations. The first matching outgoing "
                      "transition wins; at most one transition runs per fixed tick.");
            ImGui::TextUnformatted(path_utf8(document_->locator()).c_str());
            ImGui::BeginDisabled(locked || pending());
            if (ui::button(
                    "Save / Publish",
                    "Save and cook this graph; failed publication retains the last good asset."))
                request_save();
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_undo());
            if (ui::button("Undo", "Undo source edits."))
                undo();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_redo());
            if (ui::button("Redo", "Redo source edits."))
                redo();
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            auto source = document_->source().document;
            graph_.draw(
                source, document_->revision(), selected_, edge_, locked || pending(), graph_edit(),
                [&](std::uint32_t id) { select(id); }, [&](std::uint32_t id) { select_edge(id); });
            ImGui::BeginDisabled(locked || pending());
            source = document_->source().document;
            if (tree)
                draw_tree_controls(source);
            else {
                ImGui::BeginDisabled(source.at("states").size() >= 32 ||
                                     source.at("next_id").get<std::uint64_t>() > UINT32_MAX);
                if (ui::button("Add state",
                               "Allocate a stable state ID; deleted IDs are never reused.")) {
                    std::uint32_t id = 0;
                    if (mutate("Add behavior state",
                               [&](Json& d) { id = add_navigation_behavior_state(d); }))
                        select(id);
                }
                ImGui::EndDisabled();
                source = document_->source().document;
                for (const auto& row : source.at("states"))
                    if (row.at("id") == selected_) {
                        draw_state(source, row);
                        break;
                    }
            }
            ImGui::SeparatorText("Scene object");
            ImGui::TextWrapped(
                "Add Navigation Agent to an object first. Assign Target for chase/distance tests "
                "and Anchor for local move-to points in its Inspector.");
            ImGui::BeginDisabled(needs_publish_ || document_->dirty());
            if (ui::button("Attach to selected Navigation Agent",
                           "Assign this graph as one Scene Undo step."))
                attach(scene);
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            if (pending())
                ImGui::TextUnformatted("Publishing navigation behavior...");
            else if (needs_publish_)
                ImGui::TextUnformatted("Source changes need Save / Publish.");
            if (!error_.empty())
                ui::field_error(error_);
        }
        ImGui::End();
        if (close_ && dirty())
            ImGui::OpenPopup("Pending navigation behavior changes");
        if (ImGui::BeginPopupModal("Pending navigation behavior changes", nullptr,
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
    NavigationBehaviorGraphView graph_;
    int new_profile_ = 0;
    std::uint32_t selected_ = 0, edge_ = 0;
    char path_[256] = "Assets/Patrol.behavior.json", name_[65]{};
    bool gesture_dirty() const override { return graph_.draft_dirty(); }
    void cancel_gesture() override { graph_.cancel(); }
    bool flush_gesture() override { return graph_.flush(graph_edit()); }
    void view_closed() override {
        graph_.reset();
        selected_ = edge_ = 0;
    }
    void source_opened() override { select(document_->source().document.at("entry")); }
    void changed() override {
        needs_publish_ = true;
        const auto selected_edge = edge_;
        select(selected_);
        if (selected_edge)
            select_edge(selected_edge);
    }
    StateGraphCanvas::Edit graph_edit() {
        return [this](std::uint64_t revision, std::string label,
                      const std::function<void(Json&)>& edit) {
            return edit_source(revision, std::move(label), edit);
        };
    }
    static constexpr std::pair<NavigationBehaviorTreeKind, const char*> tree_options_[]{
        {NavigationBehaviorTreeKind::Sequence, "Sequence"},
        {NavigationBehaviorTreeKind::ReactiveSequence, "Reactive sequence"},
        {NavigationBehaviorTreeKind::Selector, "Selector"},
        {NavigationBehaviorTreeKind::Wait, "Wait"},
        {NavigationBehaviorTreeKind::MoveToPoint, "Move to point"},
        {NavigationBehaviorTreeKind::FollowTarget, "Follow target"},
        {NavigationBehaviorTreeKind::TargetNear, "Target near"},
        {NavigationBehaviorTreeKind::TargetFar, "Target far"},
        {NavigationBehaviorTreeKind::TargetMissing, "Target missing"}};
    void draw_tree_controls(const Json& source) {
        ImGui::BeginDisabled(source.at("nodes").size() >= 32 ||
                             source.at("next_id").get<std::uint64_t>() > UINT32_MAX);
        if (ui::button("Add node...", "Allocate a stable node ID; deleted IDs are never reused."))
            ImGui::OpenPopup("Add behavior tree node");
        FORGE_UI_PROBE("navigation_behavior:tree_add");
        if (ImGui::BeginPopup("Add behavior tree node")) {
            for (const auto& [kind, label] : tree_options_) {
                if (ImGui::Selectable(label)) {
                    std::uint32_t id = 0;
                    if (mutate("Add behavior tree node",
                               [&](Json& d) { id = add_navigation_behavior_node(d, kind); })) {
                        graph_.reset();
                        select(id);
                    }
                }
                FORGE_UI_PROBE(std::string("navigation_behavior:tree_add:") +
                               navigation_behavior_tree_kind_name(kind));
            }
            ImGui::EndPopup();
        }
        ImGui::EndDisabled();
        ImGui::TextWrapped("Connect a composite's Out port to a child's In port. Each node has "
                           "one parent. Publish one connected tree; disconnected nodes remain "
                           "editable drafts.");
        const auto current = document_->source().document;
        for (const auto& node : current.at("nodes"))
            if (node.at("id") == selected_) {
                draw_tree_node(current, node);
                return;
            }
    }
    void draw_tree_node(const Json& source, const Json& node) {
        ImGui::SeparatorText("Selected node");
        ImGui::Text("Stable ID: %u", selected_);
        ImGui::InputText("Name", name_, sizeof(name_));
        FORGE_UI_PROBE("navigation_behavior:name");
        if (ImGui::IsItemDeactivatedAfterEdit())
            state_edit("Rename behavior tree node",
                       [&](Json& row) { row["name"] = std::string(name_); });
        const auto kind = node.at("kind").get<std::string>();
        const char* preview = "Unknown";
        for (const auto& [option, label] : tree_options_)
            if (kind == navigation_behavior_tree_kind_name(option))
                preview = label;
        if (ImGui::BeginCombo("Node type", preview)) {
            for (const auto& [option, label] : tree_options_)
                if (ImGui::Selectable(label, kind == navigation_behavior_tree_kind_name(option)))
                    mutate("Set behavior tree node type", [&](Json& d) {
                        set_navigation_behavior_node_kind(d, selected_, option);
                    });
            ImGui::EndCombo();
        }
        FORGE_UI_PROBE("navigation_behavior:tree_kind");
        if (kind == "wait" || kind == "target_near" || kind == "target_far") {
            const bool seconds = kind == "wait";
            const char* field = seconds ? "seconds" : "distance";
            double value = node.at(field);
            if (ImGui::InputDouble(seconds ? "Seconds" : "Distance (m)", &value, 0, 0, "%.3f",
                                   ImGuiInputTextFlags_EnterReturnsTrue))
                state_edit("Set behavior tree value", [&](Json& row) { row[field] = value; });
            FORGE_UI_PROBE(seconds ? "navigation_behavior:tree_seconds"
                                   : "navigation_behavior:tree_distance");
        }
        if (kind == "move_to_point") {
            auto point = node.at("point").get<std::array<float, 3>>();
            if (ImGui::InputFloat3("Point", point.data(), "%.3f",
                                   ImGuiInputTextFlags_EnterReturnsTrue))
                state_edit("Set behavior tree point", [&](Json& row) { row["point"] = point; });
            FORGE_UI_PROBE("navigation_behavior:tree_point");
            ImGui::TextWrapped("World coordinates without Anchor; local coordinates with Anchor. "
                               "XY navigation ignores point Z.");
        }
        bool parented = false;
        for (const auto& parent : source.at("nodes"))
            if (parent.contains("children"))
                for (const auto& child : parent.at("children"))
                    parented |= child == selected_;
        ImGui::BeginDisabled(parented);
        if (ui::button("Make root", "Start at this node. Disconnect its parent first."))
            mutate("Choose behavior tree root", [&](Json& d) { d["entry"] = selected_; });
        FORGE_UI_PROBE("navigation_behavior:tree_root");
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ui::button("Remove node", "Remove this node and its connections as one Undo step.")) {
            mutate("Remove behavior tree node",
                   [&](Json& d) { remove_navigation_behavior_node(d, selected_); });
            return;
        }
        FORGE_UI_PROBE("navigation_behavior:tree_remove");
        if (!node.contains("children") || node.at("children").empty())
            return;
        ImGui::SeparatorText("Children (evaluated in order)");
        const auto& children = node.at("children");
        for (std::size_t index = 0; index < children.size(); ++index) {
            const auto child = children[index].get<std::uint32_t>();
            ImGui::PushID(int(child));
            const auto label = std::to_string(index + 1) + ". Node " + std::to_string(child);
            if (ImGui::Selectable(label.c_str(), edge_ == child))
                edge_ = child;
            FORGE_UI_PROBE("navigation_behavior:tree_child:" + std::to_string(child));
            ImGui::BeginDisabled(index == 0);
            if (ui::button("Earlier", "Evaluate this child earlier."))
                state_edit("Reorder behavior tree children", [&](Json& row) {
                    std::swap(row["children"][index], row["children"][index - 1]);
                });
            FORGE_UI_PROBE("navigation_behavior:tree_earlier:" + std::to_string(child));
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(index + 1 == children.size());
            if (ui::button("Later", "Evaluate this child later."))
                state_edit("Reorder behavior tree children", [&](Json& row) {
                    std::swap(row["children"][index], row["children"][index + 1]);
                });
            FORGE_UI_PROBE("navigation_behavior:tree_later:" + std::to_string(child));
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::button("Disconnect", "Keep the child as a disconnected draft node."))
                state_edit("Disconnect behavior tree child", [&](Json& row) {
                    row["children"].erase(row["children"].begin() + index);
                });
            FORGE_UI_PROBE("navigation_behavior:tree_disconnect:" + std::to_string(child));
            ImGui::PopID();
        }
    }
    template <class F> bool mutate(std::string label, F edit) {
        return edit_source(document_->revision(), std::move(label), edit);
    }
    void select(std::uint32_t id) {
        selected_ = edge_ = 0;
        const auto& source = document_->source().document;
        for (const auto& s : source.at(source.at("version") == 2 ? "nodes" : "states"))
            if (s.at("id") == id) {
                selected_ = id;
                const auto name = s.at("name").get<std::string>();
                std::snprintf(name_, sizeof(name_), "%s", name.c_str());
                return;
            }
    }
    void select_edge(std::uint32_t id) {
        const auto& source = document_->source().document;
        if (source.at("version") == 2) {
            for (const auto& parent : source.at("nodes"))
                if (parent.contains("children"))
                    for (const auto& child : parent.at("children"))
                        if (child == id) {
                            select(parent.at("id"));
                            edge_ = id;
                            return;
                        }
            return;
        }
        for (const auto& s : document_->source().document.at("states"))
            for (const auto& e : s.at("transitions"))
                if (e.at("id") == id) {
                    select(s.at("id"));
                    edge_ = id;
                    return;
                }
    }
    template <class F> void state_edit(std::string label, F edit) {
        mutate(std::move(label), [&](Json& d) {
            for (auto& s : d.at(d.at("version") == 2 ? "nodes" : "states"))
                if (s.at("id") == selected_) {
                    edit(s);
                    break;
                }
        });
    }
    void draw_state(const Json& source, const Json& state) {
        ImGui::SeparatorText("Selected state");
        ImGui::Text("Stable ID: %u", selected_);
        ImGui::InputText("Name", name_, sizeof(name_));
        FORGE_UI_PROBE("navigation_behavior:name");
        if (ImGui::IsItemDeactivatedAfterEdit())
            state_edit("Rename behavior state", [&](Json& s) { s["name"] = std::string(name_); });
        const auto action = state.at("action");
        const auto kind = action.at("kind").get<std::string>();
        if (ImGui::BeginCombo("Action", kind.c_str())) {
            for (const auto* option : {"wait", "move_to_point", "follow_target"})
                if (ImGui::Selectable(option, kind == option))
                    state_edit("Set behavior action", [&](Json& s) {
                        // Preserve extensions when changing a recognized discriminator.
                        s["action"]["kind"] = option;
                        if (std::string_view(option) == "move_to_point" &&
                            !s["action"].contains("point"))
                            s["action"]["point"] = Json::array({0, 0, 0});
                    });
            ImGui::EndCombo();
        }
        FORGE_UI_PROBE("navigation_behavior:action");
        if (kind == "move_to_point") {
            auto point = action.at("point").get<std::array<float, 3>>();
            if (ImGui::InputFloat3("Point", point.data(), "%.3f",
                                   ImGuiInputTextFlags_EnterReturnsTrue))
                state_edit("Set behavior point", [&](Json& s) { s["action"]["point"] = point; });
            FORGE_UI_PROBE("navigation_behavior:point");
            ImGui::TextWrapped("World coordinates without Anchor; local coordinates with Anchor. "
                               "XY navigation ignores point Z.");
        }
        if (ui::button("Make entry state", "Start here when this graph is first enabled."))
            mutate("Choose behavior entry", [&](Json& d) { d["entry"] = selected_; });
        ImGui::SameLine();
        if (ui::button("Remove state",
                       "Remove the state and its incoming transitions as one Undo step."))
            mutate("Remove behavior state",
                   [&](Json& d) { remove_navigation_behavior_state(d, selected_); });
        if (!selected_)
            return;
        ImGui::SeparatorText("Outgoing transitions (first match wins)");
        ImGui::TextWrapped("Connect Out to In in the canvas to add a transition. Select a "
                           "transition to edit its condition or priority.");
        for (std::size_t i = 0; i < state.at("transitions").size(); ++i) {
            const auto& e = state.at("transitions")[i];
            const auto id = e.at("id").get<std::uint32_t>();
            const auto label = std::to_string(i + 1) + ". " +
                               e.at("condition").at("kind").get<std::string>() + " -> " +
                               std::to_string(e.at("to").get<std::uint32_t>()) + " (" +
                               std::to_string(id) + ")";
            if (ImGui::Selectable(label.c_str(), edge_ == id))
                edge_ = id;
            FORGE_UI_PROBE("navigation_behavior:edge:" + std::to_string(id));
        }
        for (std::size_t i = 0; i < state.at("transitions").size(); ++i)
            if (state.at("transitions")[i].at("id") == edge_) {
                draw_transition(source, state.at("transitions")[i], i,
                                state.at("transitions").size());
                break;
            }
    }
    template <class F> void edge_edit(std::string label, F edit) {
        state_edit(std::move(label), [&](Json& s) {
            for (auto& e : s["transitions"])
                if (e.at("id") == edge_) {
                    edit(e);
                    break;
                }
        });
    }
    void draw_transition(const Json& source, const Json& edge, std::size_t index,
                         std::size_t count) {
        const auto kind = edge.at("condition").at("kind").get<std::string>();
        if (ImGui::BeginCombo("Condition", kind.c_str())) {
            for (const auto* option :
                 {"elapsed", "arrived", "target_near", "target_far", "target_missing"})
                if (ImGui::Selectable(option, kind == option))
                    edge_edit("Set behavior condition", [&](Json& e) {
                        auto& c = e["condition"];
                        c["kind"] = option;
                        if (std::string_view(option) == "elapsed" && !c.contains("seconds"))
                            c["seconds"] = 1;
                        if ((std::string_view(option) == "target_near" ||
                             std::string_view(option) == "target_far") &&
                            !c.contains("distance"))
                            c["distance"] = 2;
                    });
            ImGui::EndCombo();
        }
        FORGE_UI_PROBE("navigation_behavior:condition");
        if (kind == "elapsed" || kind == "target_near" || kind == "target_far") {
            const char* key = kind == "elapsed" ? "seconds" : "distance";
            double value = edge.at("condition").at(key);
            if (ImGui::InputDouble(kind == "elapsed" ? "Seconds" : "Distance", &value, 0, 0, "%.3f",
                                   ImGuiInputTextFlags_EnterReturnsTrue))
                edge_edit("Set behavior condition value",
                          [&](Json& e) { e["condition"][key] = value; });
            FORGE_UI_PROBE("navigation_behavior:condition_value");
        }
        const auto target =
            std::find_if(source.at("states").begin(), source.at("states").end(),
                         [&](const auto& s) { return s.at("id") == edge.at("to"); });
        const auto name = target->at("name").get<std::string>();
        if (ImGui::BeginCombo("Next state", name.c_str())) {
            for (const auto& s : source.at("states"))
                if (ImGui::Selectable(s.at("name").get_ref<const std::string&>().c_str(),
                                      s.at("id") == edge.at("to")))
                    edge_edit("Set behavior destination", [&](Json& e) { e["to"] = s.at("id"); });
            ImGui::EndCombo();
        }
        ImGui::BeginDisabled(index == 0);
        if (ui::button("Higher priority", "Test this transition earlier."))
            state_edit("Reorder behavior transitions", [&](Json& s) {
                std::swap(s["transitions"][index], s["transitions"][index - 1]);
            });
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(index + 1 == count);
        if (ui::button("Lower priority", "Test this transition later."))
            state_edit("Reorder behavior transitions", [&](Json& s) {
                std::swap(s["transitions"][index], s["transitions"][index + 1]);
            });
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ui::button("Remove transition", "Remove this transition without reusing its ID."))
            state_edit("Remove behavior transition",
                       [&](Json& s) { s["transitions"].erase(s["transitions"].begin() + index); });
    }
    void attach(Scene& scene) {
        try {
            const auto id =
                ui::editor_context ? ui::editor_context->selection.entity() : std::string{};
            const auto actor = scene.entity(id);
            if (!actor || !actor.has<NavigationAgent>())
                throw std::runtime_error("Choose a scene object with Navigation Agent");
            auto commands = Json::array();
            if (!actor.has<NavigationBehavior>())
                commands.push_back(
                    {{"operation", "component.add"},
                     {"arguments", {{"entity", id}, {"component", "forge.navigation_behavior"}}}});
            commands.push_back({{"operation", "property.set"},
                                {"arguments",
                                 {{"entity", id},
                                  {"component", "forge.navigation_behavior"},
                                  {"field", "graph"},
                                  {"value", document_->source().asset()}}}});
            apply_authoring(scene, commands, scene.revision());
            error_.clear();
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
};
} // namespace forge
