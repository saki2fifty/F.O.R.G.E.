#pragma once
#include "authoring.hpp"
#include "navigation_region_view.hpp"
namespace forge {
// Detached local-extent edit; the Scene's property/history owner accepts the result.
class NavigationRegionGesture {
  public:
    bool active() const { return owner_ != nullptr; }
    bool valid_for(const Scene& scene) const {
        return !active() || (owner_ == &scene && revision_ == scene.revision());
    }
    bool selection_matches(const std::vector<std::string>& selection) const {
        return !active() || (selection.size() == 1 && selection.front() == original_.entity);
    }
    const NavigationRegionView& original() const { return original_; }
    unsigned dimension() const { return dimension_; }
    int side() const { return side_; }
    float extent() const { return extent_; }
    bool begin(const Scene& scene, const std::string& entity, unsigned dimension, int side) {
        cancel();
        if (dimension > 1 || (side != -1 && side != 1))
            return false;
        const auto source = scene.effective_document();
        const auto policies = extract_node_policies(source);
        for (const auto& row : source.at("entities"))
            if (row.at("id") == entity) {
                const auto view = navigation_region_view(row);
                const auto policy = policies.entities.find(row.at("id").get<EntityId>());
                if (!view || policy == policies.entities.end() || !policy->second.valid ||
                    !policy->second.visible || !policy->second.selectable)
                    return false;
                original_ = *view;
                dimension_ = dimension;
                side_ = side;
                extent_ = original_extent();
                revision_ = scene.revision();
                owner_ = &scene;
                return true;
            }
        return false;
    }
    bool update(double local_delta, bool snap, double step) {
        if (active() && !valid_for(*owner_)) {
            cancel();
            return false;
        }
        if (!active() || !std::isfinite(local_delta))
            return false;
        auto value = double(original_extent()) + 2 * side_ * local_delta;
        if (snap) {
            if (!std::isfinite(step) || step <= 0)
                return false;
            value = std::round(value / step) * step;
        }
        if (!std::isfinite(value) || value < double(.01f) || value > 1000)
            return false;
        auto candidate = original_.region;
        (dimension_ == 0 ? candidate.width : candidate.height) = float(value);
        const auto previous = extent_;
        try {
            (void)navigation_detail::region_quad(candidate, original_.transform);
            extent_ = float(value);
            pending_ = preview_authoring(*owner_, commands());
            return true;
        } catch (const std::exception&) {
            extent_ = previous;
            return false;
        }
    }
    Json preview(const Json& source) const { return active() && pending_ ? *pending_ : source; }
    bool commit(Scene& scene) {
        if (!active())
            return false;
        if (!valid_for(scene)) {
            cancel();
            throw std::runtime_error("Region resize cancelled: Scene changed during the drag");
        }
        const auto edits = commands();
        const auto revision = revision_;
        const bool changed = extent_ != original_extent();
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
    float original_extent() const {
        return dimension_ == 0 ? original_.region.width : original_.region.height;
    }
    Json commands() const {
        return Json::array({{{"operation", "property.set"},
                             {"arguments",
                              {{"entity", original_.entity},
                               {"component", "forge.navigation_region_2d"},
                               {"field", dimension_ == 0 ? "width" : "height"},
                               {"value", extent_}}}}});
    }
    const Scene* owner_ = nullptr;
    NavigationRegionView original_;
    std::optional<Json> pending_;
    std::uint64_t revision_ = 0;
    unsigned dimension_ = 0;
    int side_ = 1;
    float extent_ = 0;
};
} // namespace forge
