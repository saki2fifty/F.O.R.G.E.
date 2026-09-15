#pragma once
#include "widgets.hpp"
#include <forge/geometry.hpp>
#include <optional>
#include <set>
namespace forge {
inline Json& blockout_entity(Json& doc, const std::string& id) {
    for (auto& entity : doc["entities"])
        if (entity.at("id") == id)
            return entity;
    throw std::runtime_error("Entity no longer exists");
}
inline std::string create_primitive(Scene& scene, unsigned kind, Float3 position) {
    if (kind > 3)
        throw std::runtime_error("Unknown primitive");
    auto doc = scene.document();
    std::set<std::string> occupied;
    for (const auto& e : doc["entities"])
        occupied.insert(e.at("id").get<std::string>());
    unsigned serial = 1;
    while (occupied.contains("entity-" + std::to_string(serial)))
        ++serial;
    const auto id = "entity-" + std::to_string(serial);
    doc["entities"].push_back(
        {{"id", id},
         {"name", std::string(primitive_names[kind]) + " " + std::to_string(serial)},
         {"components",
          {{"forge.position", {{"x", position[0]}, {"y", position[1]}, {"z", position[2]}}},
           {"forge.rotation", {{"x", 0}, {"y", 0}, {"z", 0}}},
           {"forge.scale", {{"x", kind == 3 ? 4 : 1}, {"y", 1}, {"z", kind == 3 ? 4 : 1}}},
           {"forge.primitive", {{"kind", kind}}},
           {"forge.tint", {{"r", 0.2f}, {"g", 0.6f}, {"b", 0.7f}}}}}});
    scene.edit(doc);
    return id;
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
        auto doc = preview(scene.document());
        cancel();
        scene.edit(doc);
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
        auto doc = scene.document();
        auto& c = blockout_entity(doc, id)["components"];
        for (const auto& [name, value] : clipboard_->items())
            for (const auto& [axis, number] : value.items())
                c[name][axis] = number;
        scene.edit(doc);
    }
    void vector_control(Scene& scene, const std::string& id, const std::string& name) {
        if (active() && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            abort();
        if (suppressed_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsAnyItemActive())
            suppressed_ = false;
        const bool scale = name == "forge.scale";
        auto doc = render_document(preview(scene.document()));
        auto value = read_xyz(blockout_entity(doc, id).at("components"), name.c_str(),
                              scale ? Float3{1, 1, 1} : Float3{});
        const bool changed =
            ImGui::DragFloat3(scale ? "Scale" : "Rotation", value.data(), scale ? 0.01f : 0.5f,
                              scale ? 0.001f : -360000.0f, scale ? 10000.0f : 360000.0f, "%.3f",
                              ImGuiSliderFlags_AlwaysClamp);
        const bool released = ImGui::IsItemDeactivatedAfterEdit();
        ui::help(scale ? "Positive X/Y/Z scale. Drag or Ctrl-click to type. Values range from "
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
            auto doc = render_document(preview(scene.document()));
            const auto& entity = blockout_entity(doc, id);
            if (!entity.at("components").contains("forge.position"))
                return;
            ui::heading("Rotation and scale",
                        "Local-axis scale, then Euler X/Y/Z rotation, then world position. Parent "
                        "transforms are not inherited.");
            vector_control(scene, id, "forge.rotation");
            vector_control(scene, id, "forge.scale");
            ui::heading("Primitive appearance", "Built-in meshes and opaque blockout tint. This is "
                                                "not a material or texture system.");
            int kind = int(primitive_kind(entity));
            ImGui::BeginDisabled(active());
            if (ImGui::Combo("Shape", &kind, primitive_names, 4)) {
                auto next = scene.document();
                blockout_entity(next, id)["components"]["forge.primitive"]["kind"] = kind;
                scene.edit(next);
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
                auto next = scene.document();
                auto& c = blockout_entity(next, id)["components"];
                for (const char* name : {"forge.position", "forge.rotation", "forge.scale"})
                    for (const char* axis : {"x", "y", "z"})
                        c[name][axis] = std::string(name) == "forge.scale" ? 1 : 0;
                scene.edit(next);
            }
            ImGui::EndDisabled();
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
    bool suppressed_ = false;
    std::optional<Json> clipboard_;
    std::string pending_, component_;
    Json values_;
    std::uint64_t revision_ = 0;
};
} // namespace forge
