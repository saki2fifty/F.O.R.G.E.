#pragma once
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
            blockout_entity(doc, pending_)["components"][component_] = values_;
        return doc;
    }
    bool commit(Scene& scene) {
        if (!active())
            return false;
        if (revision_ != scene.revision()) {
            cancel();
            throw std::runtime_error("Property edit cancelled because the scene changed");
        }
        Json commands = Json::array();
        for (const auto& [field, value] : values_.items())
            if (component_ == "forge.tint" ? (field == "r" || field == "g" || field == "b")
                                           : (field == "x" || field == "y" || field == "z"))
                commands.push_back({{"operation", "property.set"},
                                    {"arguments",
                                     {{"entity", pending_},
                                      {"component", component_},
                                      {"field", field},
                                      {"value", value}}}});
        const auto revision = revision_;
        cancel();
        apply_authoring(scene, commands, revision);
        return true;
    }
    void copy_transform(const Json& doc, const std::string& id) {
        auto view = render_document(doc);
        const auto& c = blockout_entity(view, id).at("components");
        clipboard_ = Json::object();
        for (const char* name : {"forge.position", "forge.rotation", "forge.scale"}) {
            const auto value =
                read_xyz(c, name, std::string(name) == "forge.scale" ? Float3{1, 1, 1} : Float3{});
            (*clipboard_)[name] = {{"x", value[0]}, {"y", value[1]}, {"z", value[2]}};
        }
    }
    void paste_transform(Scene& scene, const std::string& id) {
        if (!clipboard_)
            throw std::runtime_error("Copy a transform first");
        Json commands = Json::array();
        for (const auto& [name, value] : clipboard_->items())
            for (const auto& [axis, number] : value.items())
                commands.push_back(
                    {{"operation", "property.set"},
                     {"arguments",
                      {{"entity", id}, {"component", name}, {"field", axis}, {"value", number}}}});
        apply_authoring(scene, commands, scene.revision());
    }
    void vector_control(Scene& scene, const std::string& id, const std::string& name) {
        if (active() && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            abort();
        if (suppressed_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsAnyItemActive())
            suppressed_ = false;
        const bool scale = name == "forge.scale";
        const bool position = name == "forge.position";
        const auto& components = blockout_entity(snapshot_.effective(scene), id).at("components");
        auto value = read_xyz(components, name.c_str(), scale ? Float3{1, 1, 1} : Float3{});
        if (pending_ == id && component_ == name)
            value = {values_.at("x"), values_.at("y"), values_.at("z")};
        const bool changed = ImGui::DragFloat3(scale      ? "Scale"
                                               : position ? "Position"
                                                          : "Rotation",
                                               value.data(),
                                               scale      ? 0.01f
                                               : position ? 0.05f
                                                          : 0.5f,
                                               scale      ? 0.001f
                                               : position ? -1000000.0f
                                                          : -360000.0f,
                                               scale      ? 10000.0f
                                               : position ? 1000000.0f
                                                          : 360000.0f,
                                               "%.3f", ImGuiSliderFlags_AlwaysClamp);
        const bool released = ImGui::IsItemDeactivatedAfterEdit();
        ui::help(position ? "World X/Y/Z position. Drag or Ctrl-click to type. Release commits one "
                            "undo step; Escape cancels."
                 : scale  ? "Positive X/Y/Z scale. Drag or Ctrl-click to type. Values range from "
                            "0.001 to 10000. Release commits one undo step; Escape cancels."
                          : "Euler X/Y/Z angles in degrees. Applied X, then Y, then Z. Drag or "
                            "Ctrl-click to type; release commits one undo step. Escape cancels.");
        if (changed)
            stage(scene, id, name, value, {"x", "y", "z"});
        if (released && component_ == name)
            commit(scene);
    }
    void draw(Scene& scene, const std::string& id, std::string& status) {
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
                        "Local-axis scale, then Euler X/Y/Z rotation, then world position. Parent "
                        "transforms are not inherited.");
            vector_control(scene, id, "forge.position");
            vector_control(scene, id, "forge.rotation");
            vector_control(scene, id, "forge.scale");
            ImGui::BeginDisabled(active());
            if (ui::button("Copy transform", "Copy effective position, rotation, and scale into "
                                             "the editor's internal clipboard.")) {
                copy_transform(scene.document(), id);
                status = "Transform copied";
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!clipboard_);
            if (ui::button("Paste transform", "Replace position, rotation, and scale as one "
                                              "undoable edit. Color and shape are unchanged.")) {
                paste_transform(scene, id);
                status = "Transform pasted";
            }
            ImGui::EndDisabled();
            if (ui::button(
                    "Reset transform",
                    "Set position and rotation to zero and scale to one as one undoable edit.")) {
                authoring_command(scene, "transform.reset", {{"entity", id}});
            }
            ImGui::EndDisabled();
            if (ImGui::BeginPopupContextItem("##transform-actions")) {
                if (ImGui::MenuItem("Reset position"))
                    authoring_command(scene, "transform.position",
                                      {{"entity", id}, {"value", {{"x", 0}, {"y", 0}, {"z", 0}}}});
                ui::help("Reset position only; retain rotation and scale.");
                ImGui::EndPopup();
            }
            ui::heading("Primitive appearance", "Built-in meshes and opaque blockout tint. This is "
                                                "not a material or texture system.");
            int kind = int(primitive_kind(entity));
            ImGui::BeginDisabled(active());
            if (ImGui::Combo("Shape", &kind, primitive_names, 4)) {
                authoring_command(scene, "appearance.shape", {{"entity", id}, {"kind", kind}});
            }
            ui::help("Switch between Cube, Sphere, Cylinder, and Plane, retaining transform and "
                     "color. Plane is two-sided and lies in local XZ.");
            ImGui::EndDisabled();
            const auto color =
                entity.at("components")
                    .value("forge.tint", Json{{"r", 0.2f}, {"g", 0.6f}, {"b", 0.7f}});
            Float3 rgb{color.at("r"), color.at("g"), color.at("b")};
            if (ImGui::ColorEdit3("Color", rgb.data(), ImGuiColorEditFlags_NoInputs))
                stage(scene, id, "forge.tint", rgb, {"r", "g", "b"});
            ui::help("Choose an opaque RGB blockout color. Lighting modulates the displayed color. "
                     "Each picker drag is one undo step.");
            if (component_ == "forge.tint" && !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                !ImGui::IsAnyItemActive())
                commit(scene);
        } catch (const std::exception& e) {
            cancel();
            status = e.what();
        }
    }

  private:
    void stage(Scene& scene, const std::string& id, const std::string& component, Float3 value,
               std::array<const char*, 3> fields) {
        if (suppressed_)
            return;
        for (float number : value)
            if (!std::isfinite(number) ||
                (component == "forge.scale" && (number < 0.001f || number > 10000)) ||
                (component == "forge.rotation" && std::abs(number) > 360000) ||
                (component == "forge.position" && std::abs(number) > 1000000) ||
                (component == "forge.tint" && (number < 0 || number > 1)))
                throw std::runtime_error("Invalid property value; edit rejected");
        if (active() && (pending_ != id || component_ != component))
            commit(scene);
        if (!active()) {
            pending_ = id;
            component_ = component;
            revision_ = scene.revision();
            auto effective = render_document(scene.document());
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
