#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <forge/geometry.hpp>
#include <forge/render_components.hpp>
#include <forge/scene.hpp>
#include <limits>
#include <optional>
namespace forge {
// Editor-only orbit/fly camera: +Y up, +Z forward, D3D depth [0,1].
class EditorCamera {
  public:
    using Vec = std::array<float, 3>;
    Vec target{0, 1, 0};
    float yaw = 0, pitch = 0, distance = 6;
    float fly_speed = 5;
    CameraProjection projection = CameraProjection::Perspective;
    float orthographic_height = 10;
    static constexpr float pole = 1.5707963267948966f;
    static constexpr float focal = 1.7320508f; // 60-degree vertical field of view.
    static constexpr float near_plane = 0.05f, far_plane = 1000000;
    static constexpr float minimum_height = .0001f, maximum_height = 1000000;
    struct Ray {
        Vec origin, direction;
    };
    bool orthographic() const { return projection == CameraProjection::Orthographic; }
    const char* projection_name() const { return orthographic() ? "Orthographic" : "Perspective"; }
    bool set_projection(CameraProjection value) {
        if ((value != CameraProjection::Perspective && value != CameraProjection::Orthographic) ||
            (projection != CameraProjection::Perspective && !orthographic()) ||
            !std::isfinite(distance) || distance < .25f || distance > 100000 ||
            !std::isfinite(orthographic_height) || orthographic_height < minimum_height ||
            orthographic_height > maximum_height)
            return false;
        if (value != projection) {
            if (value == CameraProjection::Orthographic)
                orthographic_height =
                    std::clamp(2 * distance / focal, minimum_height, maximum_height);
            else
                distance = std::clamp(orthographic_height * focal / 2, .25f, 100000.f);
            projection = value;
        }
        return true;
    }
    double pixels_per_unit(double depth, double height) const {
        if (!std::isfinite(depth) || depth < near_plane || depth > far_plane ||
            !std::isfinite(height) || height <= 0)
            return 0;
        if ((projection != CameraProjection::Perspective && !orthographic()) ||
            (orthographic() &&
             (!std::isfinite(orthographic_height) || orthographic_height < minimum_height ||
              orthographic_height > maximum_height)))
            return 0;
        const double scale =
            orthographic() ? height / orthographic_height : focal * height / (2 * depth);
        return std::isfinite(scale) && scale > 0 ? scale : 0;
    }
    float world_units_per_pixel(float depth, float height) const {
        const auto scale = pixels_per_unit(depth, height);
        const double units = scale > 0 ? 1 / scale : 0;
        return std::isfinite(units) && units <= std::numeric_limits<float>::max() ? float(units)
                                                                                  : 0;
    }
    std::optional<Ray> ray(float x, float y, float width, float height) const {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || width <= 0 ||
            !std::isfinite(height) || height <= 0 ||
            (projection != CameraProjection::Perspective && !orthographic()) ||
            !std::isfinite(distance) || distance < .25f || distance > 100000)
            return {};
        const auto f = forward(), r = right(), u = up();
        Ray result{eye(), f};
        if (orthographic()) {
            const auto scale = world_units_per_pixel(distance, height);
            if (!(scale > 0))
                return {};
            const float sx = (x - width / 2) * scale, sy = (height / 2 - y) * scale;
            for (unsigned i = 0; i < 3; ++i)
                result.origin[i] += sx * r[i] + sy * u[i];
        } else {
            const float sx = (2 * x - width) / (focal * height);
            const float sy = (height - 2 * y) / (focal * height);
            for (unsigned i = 0; i < 3; ++i)
                result.direction[i] += sx * r[i] + sy * u[i];
        }
        for (unsigned i = 0; i < 3; ++i)
            if (!std::isfinite(result.origin[i]) || !std::isfinite(result.direction[i]))
                return {};
        return result;
    }
    Vec forward() const {
        return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
    }
    Vec right() const { return {std::cos(yaw), 0, -std::sin(yaw)}; }
    Vec up() const {
        return {-std::sin(yaw) * std::sin(pitch), std::cos(pitch),
                -std::cos(yaw) * std::sin(pitch)};
    }
    Vec eye() const {
        const auto f = forward();
        return {target[0] - f[0] * distance, target[1] - f[1] * distance,
                target[2] - f[2] * distance};
    }
    void orbit(float dx, float dy) {
        if (!std::isfinite(dx) || !std::isfinite(dy))
            return;
        yaw = std::remainder(yaw + dx * 0.006f, 6.2831853f);
        pitch = std::clamp(pitch - dy * 0.006f, -pole, pole);
    }
    void align(unsigned axis, int sign) {
        if (axis > 2 || (sign != -1 && sign != 1))
            return;
        // Look from the chosen signed world axis toward the current orbit target.
        pitch = axis == 1 ? -sign * pole : 0;
        yaw = axis == 0 ? -sign * pole : (axis == 2 && sign > 0 ? 3.141592653589793f : 0);
    }
    const char* view_name() const {
        const auto f = forward();
        for (unsigned i = 0; i < 3; ++i)
            if (std::abs(f[i]) > 0.999999f) {
                const char* names[3][2] = {{"+X", "-X"}, {"Top (+Y)", "Bottom (-Y)"}, {"+Z", "-Z"}};
                return names[i][f[i] > 0 ? 1 : 0];
            }
        return "User";
    }
    void pan(float dx, float dy, float height) {
        if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(height) || height <= 0)
            return;
        const auto r = right(), u = up();
        const float scale = world_units_per_pixel(distance, height);
        for (unsigned i = 0; i < 3; ++i)
            target[i] += (-dx * r[i] + dy * u[i]) * scale;
    }
    void look(float dx, float dy) {
        const auto position = eye();
        orbit(dx, dy);
        const auto f = forward();
        for (unsigned i = 0; i < 3; ++i)
            target[i] = position[i] + f[i] * distance;
    }
    void fly(float sideways, float ahead, float seconds, float altitude = 0) {
        if (!std::isfinite(sideways) || !std::isfinite(ahead) || !std::isfinite(seconds) ||
            !std::isfinite(altitude) || seconds <= 0)
            return;
        const auto r = right(), f = forward();
        Vec direction;
        for (unsigned i = 0; i < 3; ++i)
            direction[i] = r[i] * sideways + f[i] * ahead + (i == 1 ? altitude : 0);
        const float length = std::hypot(direction[0], direction[1], direction[2]);
        if (length == 0)
            return;
        // Normalize the world-space direction, including vertical flight while tilted.
        const float step =
            std::clamp(fly_speed, 0.1f, 1000.0f) * std::min(seconds, 0.1f) / std::max(1.0f, length);
        for (unsigned i = 0; i < 3; ++i)
            target[i] += direction[i] * step;
    }
    void zoom(float wheel) {
        if (!std::isfinite(wheel))
            return;
        const auto factor = std::exp(-std::clamp(wheel, -20.0f, 20.0f) * 0.15f);
        if (orthographic())
            orthographic_height =
                std::clamp(orthographic_height * factor, minimum_height, maximum_height);
        else if (projection == CameraProjection::Perspective)
            distance = std::clamp(distance * factor, 0.25f, 100000.0f);
    }
    bool frame_sphere(Vec center, double radius, float aspect) {
        if (!std::isfinite(radius) || radius < 0 || !std::isfinite(aspect) || aspect <= 0 ||
            !std::all_of(center.begin(), center.end(), [](float v) { return std::isfinite(v); }))
            return false;
        const double angle = std::atan(std::min(1.0f, aspect) / focal);
        const double required =
            orthographic() ? radius + near_plane * 2 : radius / std::sin(angle) * 1.1;
        const double height = radius * 2.2 / std::min(1.0f, aspect);
        if (!std::isfinite(required) || required > 100000)
            return false;
        if ((projection != CameraProjection::Perspective && !orthographic()) ||
            (orthographic() && (!std::isfinite(height) || height > maximum_height)))
            return false;
        target = center;
        distance = std::max(0.25f, float(required));
        if (orthographic())
            orthographic_height = std::max(minimum_height, float(height));
        return true;
    }
    bool frame(const Json& doc, const std::string& selected, float aspect) {
        if (!std::isfinite(aspect) || aspect <= 0)
            return false;
        Vec lo, hi;
        lo.fill(std::numeric_limits<float>::max());
        hi.fill(std::numeric_limits<float>::lowest());
        bool found = false;
        for (const auto& e : doc.at("entities")) {
            if ((!selected.empty() && e.at("id") != selected) || e.value("prefab", false) ||
                !e.at("components").contains("forge.position"))
                continue;
            const auto bounds = object_bounds(e);
            for (unsigned i = 0; i < 3; ++i) {
                lo[i] = std::min(lo[i], bounds.first[i]);
                hi[i] = std::max(hi[i], bounds.second[i]);
            }
            found = true;
        }
        if (!found)
            return false;
        Vec middle;
        double radius_squared = 0;
        for (unsigned i = 0; i < 3; ++i) {
            middle[i] = lo[i] * 0.5f + hi[i] * 0.5f;
            const double half = (double(hi[i]) - double(lo[i])) * 0.5;
            radius_squared += half * half;
        }
        return frame_sphere(middle, std::sqrt(radius_squared), aspect);
    }
};
} // namespace forge
