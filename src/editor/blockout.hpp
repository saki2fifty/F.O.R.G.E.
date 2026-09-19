#pragma once
#include "editor_state.hpp"
#include "icons.hpp"
#include "scene_cache.hpp"
#include "widgets.hpp"
#include <forge/authoring.hpp>
#include <forge/geometry.hpp>
#include <optional>
#include <set>
namespace forge {
template <class Document> inline auto& blockout_entity(Document& doc, const std::string& id) {
    for (auto& entity : doc["entities"])
        if (entity.at("id") == id)
            return entity;
    throw std::runtime_error("Entity no longer exists");
}
inline std::string create_primitive(Scene& scene, unsigned kind, Float3 position) {
    return authoring_command(
               scene, "entity.create",
               {{"kind", kind},
                {"position", {{"x", position[0]}, {"y", position[1]}, {"z", position[2]}}}})
        .at("selected")
        .get<std::string>();
}
class BlockoutProperties {
  public:
    bool at_view_target = false;
    bool active() const { return !pending_.empty(); }
    void cancel() {
        pending_.clear();
        component_.clear();
    }
    void abort() {
        cancel();
        suppressed_ = true;
    }
    void check(const Scene& scene) {
        if (active() && revision_ != scene.revision())
            cancel();
    }
    Json preview(const Json& source) const {
        auto doc = source;
        if (active())
            return preview_authoring(*owner_, commands());
        return doc;
    }
    bool commit(Scene& scene) {
        if (!active())
            return false;
        if (revision_ != scene.revision()) {
            cancel();
            throw std::runtime_error("Property edit cancelled because the scene changed");
        }
        const auto edits = commands();
        const auto revision = revision_;
        cancel();
        apply_authoring(scene, edits, revision);
        return true;
    }
    void copy_transform(const Scene& scene, const std::string& id) {
        auto view = scene.effective_document();
        const auto& c = blockout_entity(view, id).at("components");
        clipboard_ = Json::object();
        for (const char* channel : {"translation", "rotation", "scale"}) {
            const auto name = std::string("forge.local_") + channel;
            if (c.contains(name))
                (*clipboard_)[channel] = c.at(name);
            else if (std::string(channel) == "rotation")
                (*clipboard_)[channel] = {{"x", 0}, {"y", 0}, {"z", 0}, {"w", 1}};
            else
                (*clipboard_)[channel] = {{"x", 1}, {"y", 1}, {"z", 1}};
            for (auto it = (*clipboard_)[channel].begin(); it != (*clipboard_)[channel].end();) {
                if (it.key() != "x" && it.key() != "y" && it.key() != "z" && it.key() != "w")
                    it = (*clipboard_)[channel].erase(it);
                else
                    ++it;
            }
        }
    }
    void paste_transform(Scene& scene, const std::string& id) {
        if (!clipboard_)
            throw std::runtime_error("Copy a transform first");
        auto args = *clipboard_;
        args["entity"] = id;
        authoring_command(scene, "transform.local", args); // Explicitly copy all three channels.
    }
    void vector_control(Scene& scene, const std::string& id, const std::string& name) {
        if (active() && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            abort();
        if (suppressed_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsAnyItemActive())
            suppressed_ = false;
        const bool scale = name == "forge.scale";
        const bool position = name == "forge.position";
        const auto& components = blockout_entity(snapshot_.effective(scene), id).at("components");
        const auto shown = read_xyz(components, name.c_str(), scale ? Float3{1, 1, 1} : Float3{});
        Double3 value{shown[0], shown[1], shown[2]};
        if (position && components.contains(name)) {
            const auto& p = components.at(name);
            value = {p.at("x"), p.at("y"), p.at("z")};
        }
        if (pending_ == id && component_ == name)
            value = {values_.at("x"), values_.at("y"), values_.at("z")};
        const double low = scale ? double(.001f) : position ? -1e12 : -360000.0;
        const double high = scale ? 10000.0 : position ? 1e12 : 360000.0;
        const char* label = scale ? "Scale" : position ? "Position (m)" : "Rotation (deg)";
        bool changed = false, released = false;
        ImGui::BeginGroup();
        ImGui::TextUnformatted(label);
        ui::help("Local transform channels. Each row owns only its corresponding translation, "
                 "rotation or scale channel.");
        ImGui::PushID(name.c_str());
        const bool stacked = ImGui::GetContentRegionAvail().x < 240 * ui::interface_scale;
        if (!stacked)
            ImGui::PushMultiItemsWidths(3, ImGui::GetContentRegionAvail().x);
        const char* formats[] = {"X %.3f", "Y %.3f", "Z %.3f"};
        for (int axis = 0; axis < 3; ++axis) {
            ImGui::PushID(axis);
            if (axis && !stacked)
                ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
            if (stacked)
                ImGui::SetNextItemWidth(-1);
            changed |= ImGui::DragScalar("##value", ImGuiDataType_Double, &value[axis],
                                         scale      ? .01f
                                         : position ? .05f
                                                    : .5f,
                                         &low, &high, formats[axis], ImGuiSliderFlags_AlwaysClamp);
            released |= ImGui::IsItemDeactivatedAfterEdit();
            ui::help("Drag this labeled axis or Ctrl-click to type. Release commits one scene "
                     "Undo; Escape cancels.");
            if (!stacked)
                ImGui::PopItemWidth();
            ImGui::PopID();
        }
        ImGui::PopID();
        ImGui::EndGroup();
        ui::help(position ? "Local X/Y/Z translation in meters. Drag or Ctrl-click to type. "
                            "Release commits one "
                            "undo step; Escape cancels."
                 : scale  ? "Positive X/Y/Z scale. Drag or Ctrl-click to type. Values range from "
                            "0.001 to 10000. Release commits one undo step; Escape cancels."
                          : "Euler X/Y/Z angles in degrees. Applied X, then Y, then Z. Drag or "
                            "Ctrl-click to type; release commits one undo step. Escape cancels.");
        if (changed)
            stage(scene, id, name, value, {"x", "y", "z"});
        if (released && component_ == name)
            commit(scene);
        const auto authored = scene.document();
        for (const auto& row : authored.at("entities"))
            if (row.at("id") == id) {
                if (!row.contains("prefab_instance") && !row.contains("prefab_member") &&
                    !row.contains("base"))
                    break;
                const std::string channel = scale      ? "forge.local_scale"
                                            : position ? "forge.local_translation"
                                                       : "forge.local_rotation";
                const bool owned = row.at("components").contains(channel);
                ImGui::TextDisabled("%s", owned ? "Overridden" : "Inherited");
                ui::help("Independent transform-channel ownership. Equal values can still be "
                         "explicit overrides.");
                if (owned) {
                    ImGui::SameLine();
                    ui::IdScope scope(channel.c_str());
                    if (ui::button("Revert", "Remove only this channel override. Other transform "
                                             "channels remain unchanged; scene Undo restores it."))
                        authoring_command(scene, "component.revert",
                                          {{"entity", id}, {"component", channel}});
                }
            }
    }
    void draw(Scene& scene, const std::string& id, std::string& status) {
        if (error_entity_ != id) {
            error_.clear();
            error_entity_ = id;
        }
        try {
            check(scene);
            if (active() && pending_ != id)
                commit(scene);
            if (active() && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                abort();
                status = "Property edit cancelled";
            }
            // Keep just the selected entity stable while controls may commit a new revision.
            auto entity = blockout_entity(snapshot_.effective(scene), id);
            if (pending_ == id)
                entity["components"][component_] = values_;
            if (!entity.at("components").contains("forge.position"))
                return;
            ui::heading("Transform",
                        "Authored local translation, quaternion rotation shown as Euler degrees, "
                        "and scale. Spatial binding controls parent motion.");
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::GetFrameHeight());
            if (ui::icon_button("##transform-more", ui::Icon::More,
                                "Transform utilities\nCopy, paste or reset local transform."))
                ImGui::OpenPopup("Transform utilities");
            if (ImGui::BeginPopup("Transform utilities")) {
                ui::DisabledScope disabled(active());
                if (ImGui::MenuItem("Copy transform"))
                    copy_transform(scene, id);
                ui::help("Copy effective local TRS into the internal clipboard.");
                if (ImGui::MenuItem("Paste transform", nullptr, false, clipboard_.has_value()))
                    paste_transform(scene, id);
                ui::help("Replace all three local channels as one scene Undo step.");
                if (ImGui::MenuItem("Reset transform"))
                    authoring_command(scene, "transform.reset", {{"entity", id}});
                ui::help("Reset local position and rotation to zero, scale to one. Overrides all "
                         "three channels.");
                if (ImGui::MenuItem("Reset position"))
                    authoring_command(scene, "transform.position",
                                      {{"entity", id}, {"value", {{"x", 0}, {"y", 0}, {"z", 0}}}});
                ui::help(
                    "Reset translation only; preserve independent rotation and scale ownership.");
                ImGui::EndPopup();
            }
            auto bind = [&](const Json& args) {
                try {
                    authoring_command(scene, "transform.binding", args);
                    error_.clear();
                } catch (const std::exception& ex) {
                    status = error_ = ex.what();
                    ui::report_error("transform/" + id, error_);
                }
            };
            const auto binding = entity.value("spatial", Json{{"mode", "follow_structure"}});
            const auto mode = binding.at("mode").get<std::string>();
            std::string explicit_label = "Explicit: unresolved";
            const auto spatial_document = scene.document();
            if (mode == "explicit")
                for (const auto& target : spatial_document.at("entities"))
                    if (binding.at("target").at("entity") == target.at("id"))
                        explicit_label = "Explicit: " + target.at("name").get<std::string>();
            const char* space_label = mode == "world"      ? "World"
                                      : mode == "explicit" ? explicit_label.c_str()
                                                           : "Follow parent";
            {
                ui::DisabledScope disabled(entity.contains("prefab_member"));
                ui::property_label_row("Spatial binding");
                if (ImGui::BeginCombo("##binding", space_label)) {
                    for (auto choice : {"follow_structure", "world"}) {
                        if (ImGui::Selectable(std::string(choice) == "world" ? "World"
                                                                             : "Follow parent",
                                              mode == choice))
                            bind({{"entity", id}, {"spatial", {{"mode", choice}}}});
                        ui::help("Preserve world placement while changing spatial binding. Only "
                                 "required "
                                 "local channels become owned. Unrepresentable local shear is "
                                 "rejected.");
                    }
                    if (ImGui::BeginMenu("Explicit attachment")) {
                        const auto targets = scene.effective_document();
                        for (const auto& target : targets.at("entities")) {
                            const auto target_id = target.at("id").get<std::string>();
                            if (target_id == id ||
                                !target.at("components").contains("forge.local_translation"))
                                continue;
                            ImGui::PushID(target_id.c_str());
                            if (ImGui::MenuItem(
                                    target.at("name").get_ref<const std::string&>().c_str()))
                                bind({{"entity", id},
                                      {"spatial",
                                       {{"mode", "explicit"},
                                        {"target", scene.reference(target_id)}}}});
                            ui::help("Follow this object's transform without changing structural "
                                     "ownership. Preserve world placement; cycles are rejected.");
                            ImGui::PopID();
                        }
                        ImGui::EndMenu();
                    }
                    ui::help("Attach spatially to another transformed object in this scene.");
                    ImGui::EndCombo();
                }
                ui::help(
                    "Follow parent inherits the structural parent's transform. World keeps the "
                    "object independent. Explicit follows another object. Migrated old scenes "
                    "start in World space.");
                if (!entity.value("spatial_resolved", true)) {
                    ImGui::TextWrapped(
                        "Spatial parent is missing or unresolved; this object is not rendered.");
                    ui::help("The reference is retained. Restore its target, or explicitly detach "
                             "using the current local values.");
                    if (ui::button("Detach (keep local)",
                                   "Switch to World using current local values. This may change "
                                   "placement; it is one undoable edit."))
                        bind({{"entity", id},
                              {"mode", "keep_local"},
                              {"spatial", {{"mode", "world"}}}});
                }
            }
            vector_control(scene, id, "forge.position");
            vector_control(scene, id, "forge.rotation");
            vector_control(scene, id, "forge.scale");
            ui::heading("Blockout Geometry", "Built-in meshes and opaque blockout tint. This is "
                                             "not a material or texture system.");
            int kind = int(primitive_kind(entity));
            {
                ui::DisabledScope disabled(active());
                ui::property_label_row("Shape");
                if (ImGui::Combo("##Shape", &kind, primitive_names, primitive_count)) {
                    authoring_command(scene, "appearance.shape", {{"entity", id}, {"kind", kind}});
                }
                ui::help("Choose built-in blockout geometry or None. Preserves transform and tint. "
                         "No material or mesh asset is created.");
                appearance_intent(scene, id, "forge.primitive", {"kind"});
            }
            const auto color =
                entity.at("components")
                    .value("forge.tint", Json{{"r", 0.2f}, {"g", 0.6f}, {"b", 0.7f}});
            Float3 rgb{color.at("r"), color.at("g"), color.at("b")};
            ui::property_label_row("Tint");
            if (ImGui::ColorEdit3("##Tint", rgb.data(), ImGuiColorEditFlags_NoInputs))
                stage(scene, id, "forge.tint", {rgb[0], rgb[1], rgb[2]}, {"r", "g", "b"});
            ui::help("Choose an opaque RGB blockout color. Lighting modulates the displayed color. "
                     "Each picker drag is one undo step.");
            if (component_ == "forge.tint" && !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                !ImGui::IsAnyItemActive())
                commit(scene);
            appearance_intent(scene, id, "forge.tint", {"r", "g", "b"});
        } catch (const std::exception& e) {
            cancel();
            status = error_ = e.what();
            ui::report_error("transform/" + id, error_);
        }
        if (!error_.empty())
            ui::field_error(error_);
    }

  private:
    std::string error_, error_entity_;
    void appearance_intent(Scene& scene, const std::string& id, const std::string& component,
                           std::initializer_list<const char*> fields) {
        const auto doc = scene.document();
        for (const auto& row : doc.at("entities"))
            if (row.at("id") == id) {
                if (!row.contains("prefab_instance") && !row.contains("prefab_member") &&
                    !row.contains("base"))
                    return;
                ui::IdScope scope(component.c_str());
                ui::DisabledScope disabled(active());
                const bool whole = row.at("components").contains(component);
                const auto masks = row.value("property_overrides", Json::object());
                if (whole) {
                    ImGui::TextDisabled("Overridden component");
                    ui::help("Explicit whole-component ownership, even when equal to the source.");
                    if (ui::button("Revert component", "Follow the source appearance again; scene "
                                                       "Undo restores this override."))
                        authoring_command(scene, "component.revert",
                                          {{"entity", id}, {"component", component}});
                } else
                    for (const auto* field : fields) {
                        ui::IdScope field_scope(field);
                        const bool overridden =
                            masks.contains(component) && masks.at(component).contains(field);
                        ImGui::TextDisabled("%s: %s", field,
                                            overridden ? "Overridden" : "Inherited");
                        ui::help("Explicit per-property intent, independent of value equality.");
                        if (overridden) {
                            ImGui::SameLine();
                            if (ui::button("Revert", "Remove only this property's override. Other "
                                                     "appearance fields stay unchanged."))
                                authoring_command(
                                    scene, "property.revert",
                                    {{"entity", id}, {"component", component}, {"field", field}});
                        }
                    }
            }
    }
    Json commands() const {
        Json value;
        const bool color = component_ == "forge.tint";
        for (auto field : color ? std::array<const char*, 3>{"r", "g", "b"}
                                : std::array<const char*, 3>{"x", "y", "z"})
            value[field] = values_.at(field);
        const Json args = {{"entity", pending_}, {"value", value}};
        const Json command = {{"operation", color ? std::string("appearance.color")
                                                  : "transform." + component_.substr(6)},
                              {"arguments", args}};
        return Json::array({command});
    }
    const Scene* owner_ = nullptr;
    void stage(Scene& scene, const std::string& id, const std::string& component, Double3 value,
               std::array<const char*, 3> fields) {
        if (suppressed_)
            return;
        error_.clear();
        for (double number : value)
            if (!std::isfinite(number) ||
                (component == "forge.scale" && (number < 0.001f || number > 10000)) ||
                (component == "forge.rotation" && std::abs(number) > 360000) ||
                (component == "forge.position" && std::abs(number) > 1e12) ||
                (component == "forge.tint" && (number < 0 || number > 1)))
                throw std::runtime_error("Invalid property value; edit rejected");
        if (active() && (pending_ != id || component_ != component))
            commit(scene);
        if (!active()) {
            owner_ = &scene;
            pending_ = id;
            component_ = component;
            revision_ = scene.revision();
            auto effective = scene.effective_document();
            values_ = blockout_entity(effective, id)["components"].value(component, Json::object());
        }
        for (unsigned i = 0; i < 3; ++i)
            values_[fields[i]] = value[i];
    }
    AuthoringSnapshot snapshot_;
    bool suppressed_ = false;
    std::optional<Json> clipboard_;
    std::string pending_, component_;
    Json values_;
    std::uint64_t revision_ = 0;
};
} // namespace forge
