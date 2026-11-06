#pragma once
#include "../animation_clip_selection.hpp"
#include "../animation_graph_authoring.hpp"
#include "../texture_authoring.hpp"
#include "animation_graph_canvas.hpp"
#include "editor_state.hpp"
#include "source_document_panel.hpp"
#include <forge/authoring.hpp>

namespace forge {
class AnimationGraphEditor : public SourceDocumentPanel<AnimationGraphDocumentTraits> {
    friend struct AssetDocumentAutomation;

  public:
    AnimationGraphEditor()
        : SourceDocumentPanel("Animation graph", animation_graph_import_registry,
                              prepare_animation_graph_publication, desktop_texture_target()) {}
    AnimationGraphCanvas canvas;
    bool has_unapplied_edits() const { return bool(field_); }
    const std::string& diagnostic() const { return error_; }
    void content(SceneDocument& project, bool locked) {
        ImGui::BeginDisabled(locked || dirty());
        if (ui::button("New animation graph...", "Compose clips, blends and additive layers."))
            ImGui::OpenPopup("New animation graph");
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal("New animation graph", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Project file", path_, sizeof(path_));
            FORGE_UI_PROBE("animation_pose_graph:new_path");
            ImGui::BeginDisabled(locked);
            if (ui::button("Create",
                           "Create a graph source; choose its skeleton and clip nodes.")) {
                try {
                    auto created = AnimationGraphDocument::create(project.writer_guard(),
                                                                  std::filesystem::u8path(path_));
                    const auto path = created->locator();
                    created.reset();
                    load(project, path);
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& error) {
                    error_ = error.what();
                }
            }
            FORGE_UI_PROBE("animation_pose_graph:create");
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::button("Cancel", "Close without creating a graph."))
                ImGui::CloseCurrentPopup();
            if (!error_.empty())
                ui::field_error(error_);
            ImGui::EndPopup();
        }
    }
    void draw(SceneDocument&, Scene& scene, bool locked) {
        if (!is_open())
            return;
        if (locked || ImGui::GetIO().AppFocusLost || ImGui::IsKeyPressed(ImGuiKey_Escape))
            cancel_gesture();
        if (save_ && document_ && !locked && !pending()) {
            save_ = false;
            if (flush_gesture())
                publish();
        }
        bool open = true;
        const bool expanded = ImGui::Begin("Animation Graph", &open);
        if (!open)
            request_close();
        if (document_ && ui::editor_context &&
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            ui::editor_context->task.focus_document("animation_graph",
                                                    path_utf8(document_->locator()));
        if (expanded && document_) {
            ImGui::TextWrapped("%s", path_utf8(document_->locator()).c_str());
            ImGui::TextWrapped("Compose clips on one normalized cycle. Save / Publish, then use "
                               "the graph on a matching Animator.");
            ImGui::BeginDisabled(locked || pending());
            if (ui::button("Save / Publish", "Save the graph and publish its complete output."))
                request_save();
            FORGE_UI_PROBE("animation_pose_graph:save");
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_undo());
            if (ui::button("Undo", "Cancel a pending field or undo one graph edit."))
                undo();
            FORGE_UI_PROBE("animation_pose_graph:undo");
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_redo());
            if (ui::button("Redo", "Redo one graph edit."))
                redo();
            FORGE_UI_PROBE("animation_pose_graph:redo");
            ImGui::EndDisabled();
            const auto draft = document_->source().document;
            std::set<AssetId> skeletons;
            for (const auto& [id, record] : catalog_->records())
                if (record.type == SkeletonAsset::type && record.subasset &&
                    !record.subasset->removed)
                    skeletons.insert(id);
            reference(draft, "/skeleton", "Skeleton", SkeletonAsset::type, &skeletons);
            scalar(draft, "/cycle_seconds", "Cycle seconds",
                   "All clip leaves use the same normalized cycle.");
            canvas.draw(draft, document_->revision(), selected_,
                        locked || pending() || bool(field_), edit(), [&](auto id) {
                            if (flush_gesture())
                                selected_ = id;
                        });
            ImGui::SeparatorText("Graph inputs");
            ImGui::TextWrapped("Four stable slots. Animator override checkboxes replace these "
                               "defaults; names are labels.");
            inputs(draft);
            ImGui::SeparatorText("Nodes");
            unsigned clips = 0;
            for (const auto& node : draft.at("nodes"))
                clips += node.at("kind") == "clip";
            for (const auto* kind : {"clip", "blend", "additive"}) {
                if (kind != std::string_view("clip"))
                    ImGui::SameLine();
                ImGui::BeginDisabled(
                    draft.at("nodes").size() >= animation_graph_node_limit ||
                    (kind == std::string_view("clip") && clips >= animation_graph_clip_limit));
                const auto label = std::string("Add ") + kind;
                if (ui::button(label.c_str(), "Add a node as one source Undo step."))
                    add_node(kind);
                FORGE_UI_PROBE("animation_pose_graph:add:" + std::string(kind));
                ImGui::EndDisabled();
            }
            node_picker(draft, "/output", "Output node", {});
            if (ImGui::BeginCombo("Selected node", node_name(draft, selected_).c_str())) {
                for (const auto& node : draft.at("nodes")) {
                    const auto id = node.at("id").get<AssetId>();
                    ImGui::PushID(id.str().c_str());
                    if (ImGui::Selectable(node.at("name").get<std::string>().c_str(),
                                          id == selected_))
                        if (flush_gesture())
                            selected_ = id;
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            FORGE_UI_PROBE("animation_pose_graph:selected");
            for (std::size_t i = 0; i < draft.at("nodes").size(); ++i)
                if (draft.at("nodes")[i].at("id").get<AssetId>() == selected_) {
                    node_fields(draft, i);
                    break;
                }
            ImGui::BeginDisabled(dirty());
            if (ui::button("Use on selected Animator", "Assign this published graph and reset "
                                                       "graph overrides in one Scene Undo step."))
                attach(scene);
            FORGE_UI_PROBE("animation_pose_graph:attach");
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            if (pending())
                ImGui::TextWrapped("Publishing animation graph...");
            else if (dirty())
                ImGui::TextWrapped("Graph changes need Save / Publish. Connect every node to the "
                                   "output before publishing.");
            if (!error_.empty())
                ui::field_error(error_);
            ImGui::TextWrapped("Up to 32 nodes, 8 clip nodes, 4 inputs and 256 joints. Blend mixes "
                               "two poses; Additive adds the second pose relative to the skeleton "
                               "rest. Optional bone masks affect the second input. Graph, States, "
                               "Blend Space and manual playback are separate Animator modes.");
        } else if (expanded && !error_.empty())
            ui::field_error(error_);
        ImGui::End();
        draw_close_dialog("Pending animation graph changes", locked);
    }

  private:
    struct Field {
        std::string path, label;
        Json value;
        std::uint64_t revision;
        ImGuiID input = 0;
    };
    std::optional<Field> field_;
    AssetId selected_;
    char path_[512] = "Assets/Motion.animgraph.json";
    StateGraphCanvas::Edit edit() {
        return [this](auto revision, auto label, const auto& mutation) {
            return edit_source(revision, std::move(label), mutation);
        };
    }
    bool gesture_dirty() const override { return bool(field_) || canvas.draft_dirty(); }
    void clear_field() {
        if (auto* context = ImGui::GetCurrentContext(); context && field_ && field_->input) {
            if (ImGui::GetActiveID() == field_->input)
                ImGui::ClearActiveID();
            if (context->InputTextDeactivatedState.ID == field_->input)
                context->InputTextDeactivatedState.ClearFreeMemory();
        }
        field_.reset();
    }
    void cancel_gesture() override {
        clear_field();
        canvas.cancel();
    }
    bool flush_gesture() override {
        if (field_) {
            const auto field = *field_;
            if (!edit_source(field.revision, field.label,
                             [&](Json& d) { d[Json::json_pointer(field.path)] = field.value; }))
                return false;
        }
        return canvas.flush(edit());
    }
    void source_opened() override {
        cancel_gesture();
        canvas.reset();
        selected_ = {};
        if (document_ && !document_->source().document.at("nodes").empty())
            selected_ = document_->source().document.at("nodes")[0].at("id").get<AssetId>();
    }
    void view_closed() override {
        canvas.reset();
        selected_ = {};
    }
    void changed() override {
        SourceDocumentPanel::changed();
        cancel_gesture();
    }
    void mutate(std::string label, const std::function<void(Json&)>& mutation) {
        if (flush_gesture())
            (void)edit_source(document_->revision(), std::move(label), mutation);
    }
    void set_field(const std::string& path, Json value, const std::string& label, ImGuiID input) {
        if (field_ && field_->path != path && !flush_gesture())
            return;
        if (!field_)
            field_ = Field{path, "Change " + label, std::move(value), document_->revision(), input};
        else {
            field_->value = std::move(value);
            field_->input = input;
        }
    }
    void scalar(const Json& draft, const std::string& path, const char* label, const char* help) {
        ui::property_label_row(label, help);
        double value =
            (field_ && field_->path == path ? field_->value : draft.at(Json::json_pointer(path)))
                .get<double>();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputDouble(("##" + path).c_str(), &value, 0, 0, "%.4f"))
            set_field(path, value, label, ImGui::GetActiveID());
        FORGE_UI_PROBE("animation_pose_graph:" + path);
        if (ImGui::IsItemDeactivatedAfterEdit() && field_ && field_->path == path)
            (void)flush_gesture();
    }
    void name(const Json& draft, const std::string& path, const char* label) {
        const auto value =
            (field_ && field_->path == path ? field_->value : draft.at(Json::json_pointer(path)))
                .get<std::string>();
        char buffer[65]{};
        std::copy_n(value.data(), std::min(value.size(), sizeof(buffer) - 1), buffer);
        ui::property_label_row(label,
                               "1 to 64 bytes. Names are labels; stable identities do not change.");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputText(("##" + path).c_str(), buffer, sizeof(buffer)))
            set_field(path, std::string(buffer), label, ImGui::GetActiveID());
        FORGE_UI_PROBE("animation_pose_graph:" + path);
        if (ImGui::IsItemDeactivatedAfterEdit() && field_ && field_->path == path)
            (void)flush_gesture();
    }
    void reference(const Json& draft, const std::string& path, const char* label, const char* type,
                   const std::set<AssetId>* allowed) {
        ui::property_label_row(label,
                               "Choose a published asset compatible with the graph skeleton.");
        auto value = draft.at(Json::json_pointer(path));
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (asset_ref_picker(*catalog_, value, type, ("##" + path).c_str(), false, {}, allowed))
            mutate(std::string("Choose ") + label,
                   [&](auto& d) { d[Json::json_pointer(path)] = value; });
        FORGE_UI_PROBE("animation_pose_graph:" + path);
    }
    static std::string node_name(const Json& source, AssetId id) {
        for (const auto& node : source.at("nodes"))
            if (node.at("id").get<AssetId>() == id)
                return node.at("name").get<std::string>();
        return "None";
    }
    void node_picker(const Json& draft, const std::string& path, const char* label,
                     AssetId excluded) {
        const auto& value = draft.at(Json::json_pointer(path));
        const auto chosen = value.is_null() ? AssetId{} : value.get<AssetId>();
        if (ImGui::BeginCombo(label, node_name(draft, chosen).c_str())) {
            if (ImGui::Selectable("None", !chosen))
                mutate(std::string("Clear ") + label,
                       [&](auto& d) { d[Json::json_pointer(path)] = nullptr; });
            for (const auto& node : draft.at("nodes")) {
                const auto id = node.at("id").get<AssetId>();
                if (id == excluded)
                    continue;
                ImGui::PushID(id.str().c_str());
                if (ImGui::Selectable(node.at("name").get<std::string>().c_str(), id == chosen))
                    mutate(std::string("Choose ") + label,
                           [&](auto& d) { d[Json::json_pointer(path)] = id; });
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        FORGE_UI_PROBE("animation_pose_graph:" + path);
    }
    void inputs(const Json& draft) {
        std::set<unsigned> slots;
        for (std::size_t i = 0; i < draft.at("inputs").size(); ++i) {
            const auto slot = draft.at("inputs")[i].at("slot").get<unsigned>();
            slots.insert(slot);
            ImGui::PushID(int(slot));
            const auto base = "/inputs/" + std::to_string(i);
            ImGui::Text("Slot %u", slot);
            name(draft, base + "/name", "Input name");
            scalar(draft, base + "/initial", "Default value",
                   "Finite value within ±1000000; bound weights clamp to 0–1.");
            bool used = false;
            for (const auto& node : draft.at("nodes"))
                used |= node.contains("weight") && node.at("weight").is_object() &&
                        node.at("weight").at("input") == slot;
            ImGui::BeginDisabled(used);
            const bool remove =
                ui::button("Remove input", "Remove an input after detaching its weight bindings.");
            FORGE_UI_PROBE("animation_pose_graph:remove_input:" + std::to_string(slot));
            ImGui::EndDisabled();
            ImGui::PopID();
            if (remove) {
                mutate("Remove graph input", [&](auto& d) { d["inputs"].erase(i); });
                return;
            }
        }
        ImGui::BeginDisabled(slots.size() >= animation_graph_input_limit);
        if (ui::button("Add input",
                       "Add the first available stable slot without renumbering existing slots."))
            mutate("Add graph input", [&](auto& d) {
                unsigned slot = 1;
                while (slots.contains(slot))
                    ++slot;
                std::string label = "Input " + std::to_string(slot);
                std::set<std::string> names;
                for (const auto& row : d.at("inputs"))
                    names.insert(row.at("name").template get<std::string>());
                while (names.contains(label))
                    label += "_";
                d["inputs"].push_back({{"slot", slot}, {"name", label}, {"initial", 0}});
            });
        FORGE_UI_PROBE("animation_pose_graph:add_input");
        ImGui::EndDisabled();
    }
    void add_node(const char* kind) {
        const auto id = AssetId::generate();
        mutate("Add animation node", [&](auto& d) {
            Json node{{"id", id}, {"name", kind}, {"kind", kind}};
            if (kind == std::string_view("clip"))
                node["clip"] = nullptr;
            else {
                node["first"] = nullptr;
                node["second"] = nullptr;
                node["mask"] = nullptr;
                node["weight"] = .5;
            }
            d["nodes"].push_back(std::move(node));
            if (d.at("output").is_null())
                d["output"] = id;
        });
        const auto& nodes = document_->source().document.at("nodes");
        if (!nodes.empty() && nodes.back().at("id").get<AssetId>() == id)
            selected_ = id;
    }
    void node_fields(const Json& draft, std::size_t index) {
        const auto base = "/nodes/" + std::to_string(index);
        const auto& node = draft.at("nodes")[index];
        const auto id = node.at("id").get<AssetId>();
        name(draft, base + "/name", "Node name");
        ImGui::TextDisabled("%s", node.at("kind").get<std::string>().c_str());
        const auto skeleton =
            draft.at("skeleton").is_null() ? AssetId{} : draft.at("skeleton").get<AssetId>();
        if (node.at("kind") == "clip") {
            const auto allowed =
                animation_detail::compatible_animation_clips(*catalog_, {skeleton});
            reference(draft, base + "/clip", "Clip", AnimationClipAsset::type, &allowed);
        } else {
            node_picker(draft, base + "/first", "Input 1", id);
            node_picker(draft, base + "/second", "Input 2", id);
            std::set<AssetId> masks;
            for (const auto& [asset, record] : catalog_->records())
                if (record.type == AnimationMaskAsset::type)
                    for (const auto& edge : record.dependency_edges)
                        if (edge.role == "mask.skeleton" && edge.target == skeleton)
                            masks.insert(asset);
            reference(draft, base + "/mask", "Bone mask", AnimationMaskAsset::type, &masks);
            const auto& weight = node.at("weight");
            const unsigned slot = weight.is_object() ? weight.at("input").get<unsigned>() : 0;
            const auto label =
                slot ? "Input slot " + std::to_string(slot) : std::string("Constant");
            if (ImGui::BeginCombo("Weight source", label.c_str())) {
                if (ImGui::Selectable("Constant", slot == 0))
                    mutate("Use constant graph weight",
                           [&](auto& d) { d["nodes"][index]["weight"] = .5; });
                for (const auto& input : draft.at("inputs")) {
                    const auto input_slot = input.at("slot").get<unsigned>();
                    const auto text = "Slot " + std::to_string(input_slot) + " / " +
                                      input.at("name").get<std::string>();
                    if (ImGui::Selectable(text.c_str(), slot == input_slot))
                        mutate("Bind graph weight", [&](auto& d) {
                            d["nodes"][index]["weight"] = {
                                {"input", input_slot}, {"scale", 1}, {"bias", 0}};
                        });
                }
                ImGui::EndCombo();
            }
            FORGE_UI_PROBE("animation_pose_graph:weight_source");
            if (slot) {
                scalar(draft, base + "/weight/scale", "Input scale",
                       "weight = clamp(input × scale + bias, 0, 1).");
                scalar(draft, base + "/weight/bias", "Input bias", "Finite value within ±1000000.");
            } else
                scalar(draft, base + "/weight", "Weight", "Weight of input 2, from 0 to 1.");
        }
        if (ui::button("Remove selected node",
                       "Clear its connections and output, preserving other node identities."))
            mutate("Remove animation node", [&](auto& d) {
                for (auto& row : d["nodes"])
                    if (row.at("kind") != "clip")
                        for (const auto* port : {"first", "second"})
                            if (!row.at(port).is_null() &&
                                row.at(port).template get<AssetId>() == id)
                                row[port] = nullptr;
                if (!d.at("output").is_null() && d.at("output").template get<AssetId>() == id)
                    d["output"] = nullptr;
                asset_detail::remove_animation_graph_position(d, id);
                d["nodes"].erase(index);
            });
        FORGE_UI_PROBE("animation_pose_graph:remove_node");
    }
    void attach(Scene& scene) {
        try {
            if (!document_ || !ui::editor_context)
                throw std::runtime_error("Select a model root with an Animator first");
            const auto id = ui::editor_context->selection.entity();
            const auto actor = scene.entity(id);
            if (!actor || !actor.has<Animator>())
                throw std::runtime_error("Select a model root with an Animator first");
            const auto graph = document_->source().compiled();
            if (actor.get<Animator>().skeleton.id != graph.skeleton)
                throw std::runtime_error(
                    "The selected Animator must use this graph's exact skeleton");
            Json values{{"graph", graph.asset},   {"clip", nullptr},
                        {"states", nullptr},      {"state", 0},
                        {"blend_space", nullptr}, {"blend_clip", nullptr},
                        {"blend_weight", 0},      {"blend_mode", 0},
                        {"blend_root", nullptr},  {"blend_mask", nullptr},
                        {"blend_position", 0},    {"blend_position_y", 0}};
            for (unsigned i = 1; i <= animation_graph_input_limit; ++i)
                values["graph_override_" + std::to_string(i)] = false;
            authoring_command(
                scene, "property.set_many",
                {{"entity", id}, {"component", "forge.animator"}, {"values", values}});
            error_.clear();
        } catch (const std::exception& error) {
            error_ = error.what();
        }
    }
};
} // namespace forge
