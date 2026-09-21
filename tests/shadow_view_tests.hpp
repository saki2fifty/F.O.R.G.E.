#pragma once
#include "render_projection.hpp"
#include "shadow_view.hpp"
void check_shadow_views(forge::DiligentPresentation& presentation) {
    using namespace forge;
    using namespace Diligent;
    SceneShadows settings;
    settings.resolution = 64;
    settings.cascades = 3;
    settings.distance = 25;
    ShadowMapManager manager;
    ShadowMapManager::InitInfo allocation;
    allocation.Format = TEX_FORMAT_D32_FLOAT;
    allocation.Resolution = settings.resolution;
    allocation.NumCascades = settings.cascades;
    allocation.ShadowMode = SHADOW_MODE_PCF;
    manager.Initialize(presentation.device(), presentation.cache(), allocation);
    Camera authored;
    authored.infinite_far = true;
    AffineTransform pose;
    pose.m[3] = 1e12;
    pose.m[7] = 1e12;
    auto camera = camera_view(authored, pose, 512, 256);
    LightView light{};
    light.kind = std::uint32_t(LightKind::Directional);
    light.direction = {0, -1, 0};
    const RenderBounds caster{{1e12 - 1, 1e12 + 100, 4}, {1e12 + 1, 1e12 + 102, 6}};
    const auto cascades =
        directional_shadow_views(manager, camera, light, settings, std::span(&caster, 1));
    require(cascades.size() == 3 && std::abs(cascades.back().end - 25) < 1e-4,
            "Native shadow cascades did not bound an infinite camera");
    require(cascades[1].begin < cascades[0].end && cascades[2].begin < cascades[1].end,
            "Directional cascade blend regions have no fitted overlap");
    bool covered = false;
    for (const auto& cascade : cascades) {
        require(cascade.camera.orientation_reversed,
                "Native left-handed shadow basis lost its winding reversal");
        require(cascade.end > cascade.begin && cascade.camera.position == camera.position,
                "Shadow cascade lost its camera-relative origin or split order");
        for (auto value : cascade.camera.projection)
            require(std::isfinite(value), "Large-origin shadow projection became nonfinite");
        const auto projected = project_render_point(cascade.camera, {1e12, 1e12 + 101, 5});
        if (projected && (*projected)[0] >= 0 && (*projected)[0] <= 64 && (*projected)[1] >= 0 &&
            (*projected)[1] <= 64)
            covered = true;
    }
    require(covered, "Off-camera caster was clipped from directional shadow depth");
    const auto reference_view = cascades.front().camera;
    const Double3 fixed_point{1e12, 1e12, 1};
    const auto reference_pixel = project_render_point(reference_view, fixed_point);
    require(reference_pixel.has_value(), "Shadow stability fixture is outside its first cascade");
    const double texel = 2. / reference_view.projection[0] / settings.resolution;
    for (int step = -12; step <= 12; ++step) {
        auto moved_pose = pose;
        for (unsigned axis = 0; axis < 3; ++axis)
            moved_pose.m[axis * 4 + 3] += reference_view.right[axis] * texel * step / 10;
        auto moved_camera = camera_view(authored, moved_pose, 512, 256);
        const auto moved =
            directional_shadow_views(manager, moved_camera, light, settings, std::span(&caster, 1));
        const auto pixel = project_render_point(moved.front().camera, fixed_point);
        require(pixel.has_value(), "Translated shadow stability fixture disappeared");
        const double delta = (*pixel)[0] - (*reference_pixel)[0];
        require(std::abs(delta - std::round(delta)) < .005,
                "Directional texel grid moved fractionally with the camera");
    }
    authored.infinite_far = false;
    authored.projection = std::uint32_t(CameraProjection::Orthographic);
    authored.orthographic_height = 10;
    authored.near_plane = 0;
    camera = camera_view(authored, pose, 512, 256);
    const auto ortho = directional_shadow_views(manager, camera, light, settings, {});
    require(ortho.size() == 3, "Orthographic camera could not prepare native cascades");
    light.kind = std::uint32_t(LightKind::Spot);
    light.position = {1e12, 1e12, 0};
    light.direction = {0, 0, 1};
    light.cosine_outer = .7f;
    light.range = 20;
    const auto spot = punctual_shadow_views(light, settings);
    require(spot.size() == 1 && project_render_point(spot[0].camera, {1e12, 1e12, 5}),
            "Spot shadow camera failed perspective depth projection");
    require(!spot[0].camera.orientation_reversed,
            "FORGE punctual basis incorrectly adopted native cascade parity");
    const auto& projection = spot[0].camera.projection;
    const double clip_z = 5 * projection[10] + projection[11];
    const double w = 5 * projection[14] + projection[15];
    require(clip_z > 1 && clip_z / w > 0 && clip_z / w < 1,
            "Perspective shadow fixture does not exercise the required Z/W correction");
    light.kind = std::uint32_t(LightKind::Point);
    const auto faces = punctual_shadow_views(light, settings);
    require(faces.size() == 6, "Point shadow did not prepare six faces");
    const std::array<Double3, 6> rays{
        {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    for (unsigned i = 0; i < 6; ++i) {
        Double3 point;
        for (unsigned c = 0; c < 3; ++c)
            point[c] = light.position[c] + rays[i][c] * 5;
        const auto pixel = project_render_point(faces[i].camera, point);
        require(pixel && std::abs((*pixel)[0] - 32) < .001 && std::abs((*pixel)[1] - 32) < .001,
                "Point shadow face convention disagrees with +/−XYZ projection");
    }
}
