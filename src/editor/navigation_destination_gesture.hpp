#pragma once
#include "authoring.hpp"
#include "navigation_destination_view.hpp"
namespace forge {
// World-coordinate property editing only; agent/entity transforms are untouched.
class NavigationDestinationGesture {
  public:
    bool active() const { return owner_ != nullptr; }
    bool valid_for(const Scene& scene) const {
        return !active() || (owner_ == &scene && revision_ == scene.revision());
    }
    bool selection_matches(const std::vector<std::string>& ids) const {
        return !active() || (ids.size() == 1 && ids.front() == original_.entity);
    }
    bool matches(const NavigationDestinationView& current) const {
        return !active() || current == original_;
    }
    const NavigationDestinationView& original() const { return original_; }
    int axis() const { return axis_; } // -1: whole navigation plane; 0/1: plane axis.
    std::array<unsigned, 2> axes() const {
        return {0, original_.plane == NavigationPlane::PlanarXY ? 1u : 2u};
    }
    bool begin(const Scene& scene, const NavigationDestinationView& view, int axis) {
        cancel();
        if (axis < -1 || axis > 1 || !view.selectable || view.project.empty() ||
            scene.asset_id() != view.scene)
            return false;
        const auto current =
            navigation_destination_view(scene.effective_document(), view.entity,
                                        view.agent.navmesh.id, view.plane, view.project);
        if (!current || *current != view)
            return false;
        original_ = view;
        value_ = {view.agent.destination_x, view.agent.destination_y, view.agent.destination_z};
        axis_ = axis;
        revision_ = scene.revision();
        owner_ = &scene;
        return true;
    }
    bool update(Double3 world_delta, bool snap, double step) {
        if (active() && !valid_for(*owner_)) {
            cancel();
            return false;
        }
        if (!active() ||
            !std::all_of(world_delta.begin(), world_delta.end(),
                         [](double x) { return std::isfinite(x); }) ||
            (snap && (!std::isfinite(step) || step <= 0)))
            return false;
        const auto previous = value_;
        const Double3 start{original_.agent.destination_x, original_.agent.destination_y,
                            original_.agent.destination_z};
        const auto plane_axes = axes();
        for (unsigned i = 0; i < plane_axes.size(); ++i) {
            if (axis_ >= 0 && unsigned(axis_) != i)
                continue;
            const auto dimension = plane_axes[i];
            auto next = start[dimension] + world_delta[dimension];
            if (snap)
                next = std::round(next / step) * step;
            if (!std::isfinite(next)) {
                value_ = previous;
                return false;
            }
            value_[dimension] = next;
        }
        try {
            auto preview = preview_authoring(*owner_, commands());
            pending_ = std::move(preview);
            return true;
        } catch (const std::exception&) {
            value_ = previous;
            return false;
        }
    }
    NavigationDestinationView target() const {
        auto result = original_;
        result.agent.destination_x = value_[0];
        result.agent.destination_y = value_[1];
        result.agent.destination_z = value_[2];
        result.point = value_;
        if (result.plane == NavigationPlane::PlanarXY)
            result.point[2] = result.origin[2];
        return result;
    }
    Json preview(const Json& source) const { return active() && pending_ ? *pending_ : source; }
    bool commit(Scene& scene) {
        if (!active())
            return false;
        if (!valid_for(scene)) {
            cancel();
            throw std::runtime_error("Destination edit cancelled: Scene changed during the drag");
        }
        const auto edits = commands();
        const auto revision = revision_;
        const bool changed =
            value_ != Double3{original_.agent.destination_x, original_.agent.destination_y,
                              original_.agent.destination_z};
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
        Json values = Json::object();
        const char* names[] = {"destination_x", "destination_y", "destination_z"};
        const auto dimensions = axes();
        for (unsigned i = 0; i < dimensions.size(); ++i)
            if (axis_ < 0 || unsigned(axis_) == i)
                values[names[dimensions[i]]] = value_[dimensions[i]];
        return Json::array({{{"operation", "property.set_many"},
                             {"arguments",
                              {{"entity", original_.entity},
                               {"component", "forge.navigation_agent"},
                               {"values", std::move(values)}}}}});
    }
    const Scene* owner_ = nullptr;
    NavigationDestinationView original_;
    Double3 value_{};
    std::optional<Json> pending_;
    std::uint64_t revision_ = 0;
    int axis_ = -1;
};
} // namespace forge
