#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <forge/scene.hpp>
#include <limits>
namespace forge {
// Editor-only orbit/fly camera: +Y up, +Z forward, D3D depth [0,1].
class EditorCamera {
  public:
    using Vec = std::array<float, 3>;
    Vec target{0, 1, 0};
    float yaw = 0, pitch = 0, distance = 6;
    static constexpr float focal = 1.7320508f; // 60-degree vertical field of view.
    static constexpr float near_plane = 0.05f, far_plane = 1000000;
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
        pitch = std::clamp(pitch - dy * 0.006f, -1.5f, 1.5f);
    }
    void pan(float dx, float dy, float height) {
        if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(height) || height <= 0)
            return;
        const auto r = right(), u = up();
        const float scale = 2 * distance / (focal * height);
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
        const float step = 5 * std::min(seconds, 0.1f) / std::max(1.0f, length);
        for (unsigned i = 0; i < 3; ++i)
            target[i] += direction[i] * step;
    }
    void zoom(float wheel) {
        if (std::isfinite(wheel))
            distance = std::clamp(distance * std::exp(-std::clamp(wheel, -20.0f, 20.0f) * 0.15f),
                                  0.25f, 100000.0f);
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
            const auto& p = e.at("components").at("forge.position");
            const Vec center{p.at("x").get<float>(), p.at("y").get<float>(),
                             p.at("z").get<float>()};
            for (unsigned i = 0; i < 3; ++i) {
                lo[i] = std::min(lo[i], center[i] - 0.5f);
                hi[i] = std::max(hi[i], center[i] + 0.5f);
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
        const double angle = std::atan(std::min(1.0f, aspect) / focal);
        const double required = std::sqrt(radius_squared) / std::sin(angle) * 1.1;
        if (!std::isfinite(required) || required > 100000)
            return false;
        target = middle;
        distance = std::max(0.25f, float(required));
        return true;
    }
};
} // namespace forge
