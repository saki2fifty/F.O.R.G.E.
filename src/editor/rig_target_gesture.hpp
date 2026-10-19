#pragma once
#include "../animation_playback_access.hpp"
#include "authoring.hpp"
namespace forge {
struct RigTargetView {
    std::string entity, component;
    AffineTransform holder;
    Float3 target{};
    Double3 world() const { return holder.point({target[0], target[1], target[2]}); }
    Double3 axis(unsigned index) const {
        Double3 direction{};
        direction.at(index) = 1;
        return holder.vector(direction);
    }
};
// A transient view of the existing effective Scene projection, never an owner.
inline std::optional<RigTargetView> rig_target_view(const Json& source, const std::string& selected,
                                                    const std::string& component) {
    if (!AnimationPlaybackAccess::rig_type(component) || !source.is_object() ||
        !source.contains("entities") || !source.at("entities").is_array())
        return {};
    for (const auto& row : source.at("entities")) {
        if (!row.is_object() || row.value("id", Json()) != selected)
            continue;
        if (row.value("prefab", Json(false)) != false ||
            row.value("spatial_resolved", Json(true)) != true || !row.contains("world_affine") ||
            !row.at("world_affine").is_array() || row.at("world_affine").size() != 12 ||
            !row.contains("components") || !row.at("components").is_object())
            return {};
        const auto& values = row.at("components");
        if (!values.contains(component) || !values.at(component).is_object() ||
            !values.contains("forge.model_source") ||
            !values.at("forge.model_source").is_object() ||
            !values.at("forge.model_source").value("model", Json()).is_string() ||
            !values.at("forge.model_source").value("node", Json()).is_null())
            return {};
        RigTargetView result{selected, component, {}};
        for (unsigned i = 0; i < 12; ++i) {
            if (!row.at("world_affine")[i].is_number())
                return {};
            const auto coordinate = row.at("world_affine")[i].get<double>();
            if (!std::isfinite(coordinate))
                return {};
            result.holder.m[i] = coordinate;
        }
        for (unsigned i = 0; i < 3; ++i) {
            const auto key = std::string("target_") + "xyz"[i];
            if (!values.at(component).contains(key) || !values.at(component).at(key).is_number())
                return {};
            const auto value = values.at(component).at(key).get<double>();
            if (!std::isfinite(value) || std::abs(value) > 1000000)
                return {};
            result.target[i] = float(value);
        }
        return result;
    }
    return {};
}
// Drag along an arbitrary model axis under the complete holder affine. Returns
// model-space units, preserving mirrored/nonuniform/sheared scene placement.
// Only this one basis vector is needed; other singular axes do not become identity.
inline std::optional<double> rig_axis_drag(const EditorCamera& camera, Double3 origin,
                                           Double3 basis, std::array<float, 2> press,
                                           std::array<float, 2> mouse, float width, float height) {
    if (width <= 0 || height <= 0)
        return {};
    const auto dot3 = [](Double3 a, Double3 b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    const double squared = dot3(basis, basis);
    if (!std::isfinite(squared) || squared < 1e-16)
        return {};
    Double3 normal{};
    const auto forward = camera.forward();
    const auto parallel = dot3({forward[0], forward[1], forward[2]}, basis) / squared;
    for (unsigned i = 0; i < 3; ++i) {
        normal[i] = forward[i] - basis[i] * parallel;
    }
    if (dot3(normal, normal) < 1e-12)
        return {};
    const auto intersect = [&](std::array<float, 2> pixel) -> std::optional<Double3> {
        const auto ray = view_ray(camera, pixel[0], pixel[1], width, height);
        if (!ray)
            return {};
        const auto& direction = ray->direction;
        const auto& eye = ray->origin;
        const auto denominator = dot3(normal, {direction[0], direction[1], direction[2]});
        if (!std::isfinite(denominator) || std::abs(denominator) < 1e-8)
            return {};
        const auto distance =
            dot3(normal, {origin[0] - eye[0], origin[1] - eye[1], origin[2] - eye[2]}) /
            denominator;
        if (!std::isfinite(distance) || distance < EditorCamera::near_plane ||
            distance > EditorCamera::far_plane)
            return {};
        return Double3{eye[0] + direction[0] * distance, eye[1] + direction[1] * distance,
                       eye[2] + direction[2] * distance};
    };
    const auto first = intersect(press), last = intersect(mouse);
    if (!first || !last)
        return {};
    const auto delta =
        dot3({(*last)[0] - (*first)[0], (*last)[1] - (*first)[1], (*last)[2] - (*first)[2]},
             basis) /
        squared;
    return std::isfinite(delta) ? std::optional<double>(delta) : std::nullopt;
}
class RigTargetGesture {
  public:
    bool active() const { return owner_ != nullptr; }
    bool valid_for(const Scene& scene) const {
        return !active() || (owner_ == &scene && revision_ == scene.revision());
    }
    const RigTargetView& target() const { return preview_; }
    const RigTargetView& original() const { return original_; }
    unsigned axis() const { return axis_; }
    bool begin(const Scene& scene, const std::string& entity, const std::string& component,
               unsigned axis) {
        cancel();
        if (axis > 2)
            return false;
        const auto target = rig_target_view(scene.effective_document(), entity, component);
        if (!target)
            return false;
        const auto basis = target->axis(axis);
        double squared = 0;
        for (const auto coordinate : basis)
            squared += coordinate * coordinate;
        if (!std::isfinite(squared) || squared < 1e-16)
            return false;
        original_ = preview_ = *target;
        axis_ = axis;
        owner_ = &scene;
        revision_ = scene.revision();
        return true;
    }
    bool update(double delta, bool snap, double step) {
        if (active() && !valid_for(*owner_)) {
            cancel();
            return false;
        }
        if (!active() || !std::isfinite(delta))
            return false;
        double value = double(original_.target[axis_]) + delta;
        if (snap) {
            if (!std::isfinite(step) || step <= 0)
                return false;
            value = std::round(value / step) * step;
        }
        if (!std::isfinite(value) || std::abs(value) > 1000000)
            return false;
        const auto previous = preview_.target[axis_];
        preview_.target[axis_] = float(value);
        try {
            pending_ = preview_authoring(*owner_, commands());
            return true;
        } catch (const std::exception&) {
            preview_.target[axis_] = previous;
            return false;
        }
    }
    Json preview(const Json& source) const { return active() && pending_ ? *pending_ : source; }
    bool commit(Scene& scene) {
        if (!active())
            return false;
        if (!valid_for(scene)) {
            cancel();
            throw std::runtime_error("Rig target cancelled: scene changed during the drag");
        }
        const auto edits = commands();
        const auto revision = revision_;
        const bool changed = preview_.target != original_.target;
        cancel();
        if (changed)
            apply_authoring(scene, edits, revision);
        return changed;
    }
    void cancel() {
        owner_ = nullptr;
        pending_.reset();
    }

  private:
    Json commands() const {
        return Json::array({{{"operation", "property.set"},
                             {"arguments",
                              {{"entity", original_.entity},
                               {"component", original_.component},
                               {"field", std::string("target_") + "xyz"[axis_]},
                               {"value", preview_.target[axis_]}}}}});
    }
    const Scene* owner_ = nullptr;
    RigTargetView original_, preview_;
    std::optional<Json> pending_;
    std::uint64_t revision_ = 0;
    unsigned axis_ = 0;
};
} // namespace forge
