#pragma once
#include "camera.hpp"
#include <forge/authoring.hpp>
#include <forge/geometry.hpp>
#include <optional>
namespace forge {
using Vec3 = EditorCamera::Vec;
inline Vec3 subtract(Vec3 a, Vec3 b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
inline float dot(Vec3 a, Vec3 b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline std::optional<Vec3> block_position(const Json& e) {
    if (e.value("prefab", false) || !e.at("components").contains("forge.position"))
        return {};
    const auto& p = e.at("components").at("forge.position");
    return Vec3{p.at("x"), p.at("y"), p.at("z")};
}
inline std::optional<Vec3> entity_position(const Json& doc, const std::string& id) {
    for (const auto& e : doc.at("entities"))
        if (e.at("id") == id)
            return block_position(e);
    return {};
}
inline void set_position(Json& doc, const std::string& id, Vec3 position) {
    for (auto& e : doc.at("entities"))
        if (e.at("id") == id) {
            auto& fields = e["components"]["forge.position"];
            fields["x"] = position[0];
            fields["y"] = position[1];
            fields["z"] = position[2];
            return;
        }
    throw std::runtime_error("Entity no longer exists");
}
// Coordinates are relative to the viewport image, in logical pixels.
inline std::optional<std::array<float, 2>> project_point(const EditorCamera& camera, Vec3 p,
                                                         float width, float height) {
    const auto delta = subtract(p, camera.eye());
    const auto depth = dot(delta, camera.forward());
    if (width <= 0 || height <= 0 || depth < EditorCamera::near_plane ||
        depth > EditorCamera::far_plane)
        return {};
    const float scale = EditorCamera::focal * height / (2 * depth);
    return std::array<float, 2>{width / 2 + dot(delta, camera.right()) * scale,
                                height / 2 - dot(delta, camera.up()) * scale};
}
// Clip an infinite axis through WORLD zero to the camera frustum. Its endpoints
// must not follow the finite reference grid patch or the camera's orbit target.
inline std::optional<std::array<std::array<float, 2>, 2>>
project_world_axis(const EditorCamera& camera, unsigned axis, float width, float height) {
    if (axis > 2 || width <= 0 || height <= 0)
        return {};
    const auto eye = camera.eye(), right = camera.right(), up = camera.up(),
               forward = camera.forward();
    const double x = -dot(eye, right), y = -dot(eye, up), z = -dot(eye, forward);
    const double dx = right[axis], dy = up[axis], dz = forward[axis];
    const double hx = width / (height * double(EditorCamera::focal));
    const double hy = 1.0 / EditorCamera::focal;
    double low = -std::numeric_limits<double>::infinity();
    double high = std::numeric_limits<double>::infinity();
    auto clip = [&](double value, double slope) {
        if (std::abs(slope) < 1e-12)
            return value >= 0;
        const double t = -value / slope;
        if (slope > 0)
            low = std::max(low, t);
        else
            high = std::min(high, t);
        return low <= high;
    };
    if (!clip(z - EditorCamera::near_plane, dz) || !clip(EditorCamera::far_plane - z, -dz) ||
        !clip(hx * z + x, hx * dz + dx) || !clip(hx * z - x, hx * dz - dx) ||
        !clip(hy * z + y, hy * dz + dy) || !clip(hy * z - y, hy * dz - dy) || !std::isfinite(low) ||
        !std::isfinite(high))
        return {};
    std::array<std::array<float, 2>, 2> result;
    unsigned index = 0;
    for (double t : {low, high}) {
        const double depth = z + t * dz;
        if (depth <= 0)
            return {};
        const double scale = EditorCamera::focal * height / (2 * depth);
        result[index++] = {float(width / 2 + (x + t * dx) * scale),
                           float(height / 2 - (y + t * dy) * scale)};
    }
    return result;
}
inline Vec3 view_ray(const EditorCamera& camera, float x, float y, float width, float height) {
    const auto f = camera.forward(), r = camera.right(), u = camera.up();
    const float sx = (2 * x - width) / (EditorCamera::focal * height);
    const float sy = (height - 2 * y) / (EditorCamera::focal * height);
    return {f[0] + sx * r[0] + sy * u[0], f[1] + sx * r[1] + sy * u[1],
            f[2] + sx * r[2] + sy * u[2]};
}
inline std::string pick_block(const Json& doc, const EditorCamera& camera, float x, float y,
                              float width, float height) {
    if (width <= 0 || height <= 0 || x < 0 || y < 0 || x >= width || y >= height)
        return {};
    const auto ray = view_ray(camera, x, y, width, height), eye = camera.eye();
    float nearest = EditorCamera::far_plane;
    std::string selected;
    for (const auto& e : doc.at("entities")) {
        const auto id = e.at("id").get<std::string>();
        const auto center = block_position(e);
        if (!center)
            continue;
        if (const auto distance = object_hit(e, eye, ray, EditorCamera::near_plane, nearest)) {
            nearest = *distance;
            selected = id;
        }
    }
    return selected;
}
// Intersect a plane containing the axis and facing the camera as much as possible.
// This retains perspective-correct motion for oblique axes, unlike screen-pixel scaling.
inline std::optional<float> axis_drag(const EditorCamera& camera, Vec3 origin, int axis,
                                      std::array<float, 2> press, std::array<float, 2> mouse,
                                      float width, float height) {
    auto normal = camera.forward();
    normal[axis] = 0;
    const float plane_distance = dot(normal, subtract(origin, camera.eye()));
    auto intersect = [&](std::array<float, 2> point) -> std::optional<float> {
        const auto ray = view_ray(camera, point[0], point[1], width, height);
        const float denominator = dot(normal, ray);
        if (std::abs(denominator) < 0.000001f)
            return {};
        const float t = plane_distance / denominator;
        if (!std::isfinite(t) || t < EditorCamera::near_plane)
            return {};
        return camera.eye()[axis] + ray[axis] * t;
    };
    const auto first = intersect(press), last = intersect(mouse);
    if (!first || !last)
        return {};
    return *last - *first;
}
// Preview stays outside Scene until release: one undo command, and no partially saved drag.
class MoveGesture {
  public:
    bool active() const { return !id_.empty(); }
    bool valid_for(const Scene& scene) const { return !active() || revision_ == scene.revision(); }
    const Vec3& position() const { return preview_; }
    const Vec3& origin() const { return start_; }
    int axis() const { return axis_; }
    bool begin(const Scene& scene, const std::string& id, int axis) {
        const auto p = entity_position(render_document(scene.document()), id);
        if (!p || axis < -1 || axis > 2)
            return false;
        id_ = id;
        start_ = preview_ = *p;
        revision_ = scene.revision();
        axis_ = axis;
        return true;
    }
    void update(Vec3 delta, bool snap, float step) {
        if (!active())
            return;
        for (unsigned i = 0; i < 3; ++i) {
            preview_[i] = start_[i];
            if (axis_ < 0 || int(i) == axis_) {
                float value = start_[i] + delta[i];
                if (snap && std::isfinite(step) && step > 0)
                    value = std::round(value / step) * step;
                if (std::isfinite(value) && std::abs(value) <= 1000000)
                    preview_[i] = value;
            }
        }
    }
    Json preview(const Json& source) const {
        auto copy = source;
        if (active())
            set_position(copy, id_, preview_);
        return copy;
    }
    bool commit(Scene& scene) {
        if (!active())
            return false;
        const auto id = id_;
        cancel();
        if (scene.revision() != revision_)
            throw std::runtime_error("Move cancelled because the scene changed during the drag");
        if (preview_ == start_)
            return false;
        authoring_command(
            scene, "transform.position",
            {{"entity", id},
             {"value", {{"x", preview_[0]}, {"y", preview_[1]}, {"z", preview_[2]}}}});
        return true;
    }
    void cancel() { id_.clear(); }

  private:
    std::string id_;
    Vec3 start_{}, preview_{};
    std::uint64_t revision_ = 0;
    int axis_ = -1;
};
} // namespace forge
