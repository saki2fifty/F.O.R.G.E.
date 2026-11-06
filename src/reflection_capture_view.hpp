#pragma once
#include "reflection_capture_settings.hpp"
#include <forge/render_scene.hpp>
#include <numbers>
namespace forge {
// Canonical capture geometry and filtering shared by reviewed snapshots and
// runtime GPU producers. Neither helper owns rendering, pixels or simulation.
inline std::array<PreparedCamera, 6>
reflection_capture_cameras(const ReflectionCaptureSettings& settings, EntityId entity) {
    validate_reflection_capture_settings(settings);
    constexpr std::array<Double3, 6> directions{
        {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    constexpr std::array<Double3, 6> ups{
        {{0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}}};
    std::array<PreparedCamera, 6> result;
    for (unsigned i = 0; i < result.size(); ++i) {
        auto& view = result[i];
        view.entity = entity;
        auto& c = view.settings;
        c.vertical_fov = std::numbers::pi / 2;
        c.aspect = 1;
        c.near_plane = settings.near_plane;
        c.far_plane = settings.far_plane;
        c.layers = settings.layers;
        c.background_r = c.background_g = c.background_b = 0;
        c.background_a = 1;
        const auto& f = directions[i];
        const auto& u = ups[i];
        const Double3 r{u[1] * f[2] - u[2] * f[1], u[2] * f[0] - u[0] * f[2],
                        u[0] * f[1] - u[1] * f[0]};
        AffineTransform world;
        for (unsigned row = 0; row < 3; ++row) {
            world.m[row * 4] = r[row];
            world.m[row * 4 + 1] = u[row];
            world.m[row * 4 + 2] = f[row];
            world.m[row * 4 + 3] = settings.position[row];
        }
        view.view = camera_view(c, world, settings.size, settings.size);
    }
    return result;
}
inline RenderScene reflection_capture_scene(RenderScene scene) {
    // A lighting-map snapshot uses the source environment even when its authored
    // display sky is hidden. It contains no local/visible-depth recursive feedback.
    scene.cameras.clear();
    scene.reflection_probes.clear();
    scene.settings.environment.sky = true;
    scene.settings.screen_space_reflections.enabled = false;
    scene.settings.ambient_occlusion.enabled = false;
    scene.settings.temporal_anti_aliasing.enabled = false;
    scene.settings.motion_blur.enabled = false;
    scene.settings.depth_of_field.enabled = false;
    scene.settings.bloom.enabled = false;
    scene.settings.auto_exposure.enabled = false;
    scene.settings.color_adjustments.enabled = false;
    return scene;
}
} // namespace forge
