#pragma once
#include "property_drawer.hpp"
#include <forge/authoring.hpp>
#include <forge/scene_render_settings.hpp>
#include <numbers>
namespace forge::ui {
inline void scene_distance_fog_controls(const SceneDistanceFog& settings, bool locked,
                                        const std::function<void(const Json&)>& commit) {
    IdScope scope("distance_fog");
    const bool open = ImGui::CollapsingHeader("Distance fog");
    FORGE_UI_PROBE("fog:section");
    if (!open)
        return;
    help("Fade geometry toward a color as its distance from the camera grows.");
    DisabledScope disabled(locked);
    bool enabled = settings.enabled;
    if (ImGui::Checkbox("Enable distance fog", &enabled))
        commit({{"distance_fog", {{"enabled", enabled}}}});
    FORGE_UI_PROBE("fog:enabled");
    auto color = settings.color;
    property_label_row("Fog color (linear HDR)", "Linear RGB; channels from 0 to 65504.");
    if (ImGui::InputFloat3("##color", color.data(), "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
        commit({{"distance_fog", {{"color", color}}}});
    FORGE_UI_PROBE("fog:color");
    auto field = [&](const char* label, const char* key, float value, const char* tip) {
        IdScope id(key);
        property_label_row(label, tip);
        if (ImGui::InputFloat("##value", &value, 0, 0, "%.3f",
                              ImGuiInputTextFlags_EnterReturnsTrue))
            commit({{"distance_fog", {{key, value}}}});
        FORGE_UI_PROBE(std::string("fog:") + key);
        help(tip);
    };
    field("Start (metres)", "start", settings.start,
          "No fog up to this radial camera distance. Start must be zero or greater and below end.");
    field("End (metres)", "end", settings.end,
          "Full fog color at this distance, from 0.001 to 1000000 metres. Move end first when "
          "increasing start past its current value. Enter commits one Undo step.");
    if (button("Reset distance fog", "Disable fog and restore gray, start 0 and end 100 metres."))
        commit({{"distance_fog",
                 {{"enabled", false}, {"color", {.5, .5, .5}}, {"start", 0}, {"end", 100}}}});
    FORGE_UI_PROBE("fog:reset");
    ImGui::TextWrapped("Affects geometry in Scene and game views before transparency, bloom and "
                       "display processing. Sky, camera background, editor overlays and game HUD "
                       "keep their own colors. Scene Save and Undo include these settings.");
}
inline void scene_auto_exposure_controls(const SceneAutoExposure& settings, bool locked,
                                         const std::function<void(const Json&)>& commit) {
    IdScope scope("auto_exposure");
    const bool open = ImGui::CollapsingHeader("Automatic exposure");
    FORGE_UI_PROBE("exposure:section");
    if (!open)
        return;
    help("Adapt the composed Game image to positive HDR light. Scene View keeps its personal "
         "manual preview. These are authored settings; save the Scene to retain them.");
    DisabledScope disabled(locked);
    bool enabled = settings.enabled;
    if (ImGui::Checkbox("Enable automatic exposure", &enabled))
        commit({{"auto_exposure", {{"enabled", enabled}}}});
    FORGE_UI_PROBE("exposure:enabled");
    auto field = [&](const char* label, const char* key, float value, const char* tip,
                     float scale = 1) {
        IdScope id(key);
        property_label_row(label, tip);
        value *= scale;
        if (ImGui::InputFloat("##value", &value, 0, 0, "%.3f",
                              ImGuiInputTextFlags_EnterReturnsTrue))
            commit({{"auto_exposure", {{key, double(value) / scale}}}});
        FORGE_UI_PROBE(std::string("exposure:") + key);
        help(tip);
    };
    field("Minimum gain (stops)", "min_ev", settings.min_ev,
          "Lower automatic gain limit, -20 to +20 stops. It must not exceed the maximum. "
          "The separate Exposure value adds manual compensation.");
    field("Maximum gain (stops)", "max_ev", settings.max_ev,
          "Upper automatic gain limit, -20 to +20 stops. +1 doubles linear light. Enter commits.");
    field("Low percentile (%)", "low_percentile", settings.low_percentile,
          "Exclude this percentage of the darkest positive samples. 0–100; must be below the "
          "high percentile. Empty black pixels do not contribute.",
          100);
    field("High percentile (%)", "high_percentile", settings.high_percentile,
          "Keep samples up to this percentile, excluding brighter outliers. 0–100; must exceed "
          "the low percentile.",
          100);
    field("Target gray (linear)", "target_gray", settings.target_gray,
          "Target linear light from 0.001 to 1 before tone mapping; default 0.18. This is not "
          "a promised display brightness after grading.");
    field(
        "Brightening rate (1/s)", "speed_up", settings.speed_up,
        "Rate for increasing gain in darker light, 0.01–20 per unscaled second. Larger is faster.");
    field("Darkening rate (1/s)", "speed_down", settings.speed_down,
          "Rate for decreasing gain in brighter light, 0.01–20 per unscaled second. Simulation "
          "pause/time scale does not change visual adaptation.");
    if (button("Reset automatic exposure", "Disable automatic gain and restore its default limits, "
                                           "percentiles, target and rates in one Undo step.")) {
        const SceneAutoExposure defaults;
        commit({{"auto_exposure",
                 {{"enabled", defaults.enabled},
                  {"min_ev", defaults.min_ev},
                  {"max_ev", defaults.max_ev},
                  {"low_percentile", defaults.low_percentile},
                  {"high_percentile", defaults.high_percentile},
                  {"target_gray", defaults.target_gray},
                  {"speed_up", defaults.speed_up},
                  {"speed_down", defaults.speed_down}}}});
    }
    FORGE_UI_PROBE("exposure:reset");
}
inline void scene_color_controls(const SceneColorAdjustments& settings, bool locked,
                                 const std::function<void(const Json&)>& commit) {
    IdScope scope("color_adjustments");
    heading("Color adjustments",
            "Scene-wide color grading. Scene Save and Undo include these values.");
    DisabledScope disabled(locked);
    bool enabled = settings.enabled;
    if (ImGui::Checkbox("Enable color adjustments", &enabled))
        commit({{"color_adjustments", {{"enabled", enabled}}}});
    FORGE_UI_PROBE("color:enabled");
    property_label_row(
        "Color filter (linear RGB)",
        "Multiply linear HDR light before tone mapping. White leaves colors unchanged.");
    auto filter = settings.filter;
    if (ImGui::InputFloat3("##filter", filter.data(), "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
        commit({{"color_adjustments", {{"filter", filter}}}});
    FORGE_UI_PROBE("color:filter");
    help("Red, green and blue multipliers from 0 to 4. White is 1,1,1; use values below 1 to tint. "
         "Enter commits.");
    auto field = [&](const char* label, const char* key, float value, const char* tip) {
        IdScope id(key);
        property_label_row(label, tip);
        if (ImGui::InputFloat("##value", &value, 0, 0, "%.3f",
                              ImGuiInputTextFlags_EnterReturnsTrue))
            commit({{"color_adjustments", {{key, value}}}});
        FORGE_UI_PROBE(std::string("color:") + key);
        help(tip);
    };
    field("Saturation", "saturation", settings.saturation,
          "0 makes the image grayscale, 1 preserves saturation, 2 strengthens it. Applied after "
          "tone mapping; Enter commits.");
    field("Contrast", "contrast", settings.contrast,
          "0 makes a flat 18% linear gray; 1 preserves contrast; 2 increases contrast around that "
          "gray. Enter commits.");
    if (button("Reset color adjustments", "Disable grading and restore white filter, saturation 1 "
                                          "and contrast 1 in one Undo step."))
        commit({{"color_adjustments",
                 {{"enabled", false}, {"filter", {1, 1, 1}}, {"saturation", 1}, {"contrast", 1}}}});
    FORGE_UI_PROBE("color:reset");
    ImGui::TextWrapped(
        "Affects Scene and game images, including sky and bloom. Editor controls, overlays and the "
        "game HUD keep their own colors. Display output is bounded to SDR.");
}
inline void scene_bloom_controls(const SceneBloom& settings, bool locked,
                                 const std::function<void(const Json&)>& commit) {
    IdScope scope("bloom");
    heading("Bloom", "Glow around bright surfaces. Runs before display exposure and tone mapping.");
    DisabledScope disabled(locked);
    bool enabled = settings.enabled;
    if (ImGui::Checkbox("Enable bloom", &enabled))
        commit({{"bloom", {{"enabled", enabled}}}});
    FORGE_UI_PROBE("bloom:enabled");
    auto field = [&](const char* label, const char* key, float value, const char* tip) {
        IdScope id(key);
        property_label_row(label, tip);
        if (ImGui::InputFloat("##value", &value, 0, 0, "%.3f",
                              ImGuiInputTextFlags_EnterReturnsTrue))
            commit({{"bloom", {{key, value}}}});
        FORGE_UI_PROBE(std::string("bloom:") + key);
        help(tip);
    };
    field("Brightness threshold", "threshold", settings.threshold,
          "Linear brightness from 0 to 65504. Only brighter light contributes to the glow.");
    field("Intensity", "intensity", settings.intensity,
          "Glow multiplier from 0 to 4. Zero skips the effect; Enter commits one undo step.");
    field("Spread", "radius", settings.radius,
          "Filter spacing from 0.5 to 16 pixels in the half-resolution glow image. Larger "
          "values spread glow farther.");
    ImGui::TextWrapped("Each camera keeps its glow inside its own view. Scene Save and Undo "
                       "include these settings. Bloom uses additional GPU memory and draw work.");
}
inline void scene_lighting(bool& open, Scene& scene, const AssetCatalog* catalog, bool locked) {
    if (!open)
        return;
    draft_window_size({640 * interface_scale, 720 * interface_scale});
    if (ImGui::Begin("Scene lighting", &open)) {
        if (editor_context)
            editor_context->task.focus(DocumentTask::Scene);
        heading(
            "Environment",
            "These values belong to the scene. Scene Save and Undo include each committed change.");
        ImGui::BeginDisabled(locked);
        try {
            const auto settings = scene_render_settings(scene.document());
            auto commit = [&](const Json& patch) {
                authoring_command(scene, "scene.rendering.set", patch);
                if (editor_context)
                    editor_context->problems.resolve("scene.lighting");
            };
            Json texture = settings.environment.texture.id ? Json(settings.environment.texture.id)
                                                           : Json(nullptr);
            property_label_row("Environment map", "Choose the scene's sky and image-based light.");
            if (catalog) {
                if (asset_ref_picker(*catalog, texture, "texture", "##environment"))
                    commit({{"environment", {{"texture", texture}}}});
            } else {
                ImGui::TextDisabled("Asset catalog is unavailable.");
                help("Open a project with an admitted asset catalog before assigning an "
                     "environment.");
            }
            ImGui::TextWrapped("Choose an imported color cubemap or equirectangular image. HDR "
                               "color is used when available.");
            float intensity = settings.environment.intensity;
            property_label_row("Intensity", "Multiply sky brightness and image-based lighting.");
            if (ImGui::InputFloat("##intensity", &intensity, 0, 0, "%.3f",
                                  ImGuiInputTextFlags_EnterReturnsTrue))
                commit({{"environment", {{"intensity", intensity}}}});
            help("Multiply sky brightness and image-based lighting. Zero disables both "
                 "contributions. Enter commits one undo step.");
            double degrees = std::remainder(settings.environment.rotation, 2 * std::numbers::pi) *
                             180 / std::numbers::pi;
            property_label_row("Rotation (degrees)", "Rotate the sky and lighting around world Y.");
            if (ImGui::InputDouble("##rotation", &degrees, 0, 0, "%.2f",
                                   ImGuiInputTextFlags_EnterReturnsTrue))
                commit({{"environment", {{"rotation", degrees * std::numbers::pi / 180}}}});
            help("Rotate sky and image-based lighting together around world Y. Positive angles "
                 "rotate +X toward -Z. Enter commits.");
            bool sky = settings.environment.sky;
            if (ImGui::Checkbox("Show sky", &sky))
                commit({{"environment", {{"sky", sky}}}});
            help("Show the environment as the distant background. Turning this off keeps its "
                 "lighting and reflections.");
            heading("Game display", "Authored exposure applies to game cameras. Scene View has a "
                                    "personal preview exposure under View.");
            float exposure = settings.exposure;
            property_label_row("Exposure (stops)",
                               "Adjust the brightness of authored game cameras.");
            if (ImGui::InputFloat("##exposure", &exposure, 0, 0, "%.2f",
                                  ImGuiInputTextFlags_EnterReturnsTrue))
                commit({{"exposure", exposure}});
            help("Game-camera exposure from -20 to +20 stops. +1 doubles linear light before tone "
                 "mapping. With automatic exposure, this adds brightness compensation. Enter "
                 "commits.");
            scene_auto_exposure_controls(settings.auto_exposure, locked, commit);
            scene_distance_fog_controls(settings.distance_fog, locked, commit);
            scene_bloom_controls(settings.bloom, locked, commit);
            scene_color_controls(settings.color_adjustments, locked, commit);
            heading("Shadows", "Scene-wide shadow quality. Individual lights and mesh renderers "
                               "control casting and receiving in the Inspector.");
            bool enabled = settings.shadows.enabled;
            if (ImGui::Checkbox("Enable shadows", &enabled))
                commit({{"shadows", {{"enabled", enabled}}}});
            help("Enable directional, spot and point-light shadow maps. Each light must also have "
                 "Cast shadows enabled. Scene Save and Undo include these settings.");
            auto integer = [&](const char* label, const char* key, unsigned value,
                               const char* tip) {
                IdScope field(key);
                property_label_row(label, tip);
                auto edited = value;
                if (ImGui::InputScalar("##value", ImGuiDataType_U32, &edited, nullptr, nullptr,
                                       "%u", ImGuiInputTextFlags_EnterReturnsTrue))
                    commit({{"shadows", {{key, edited}}}});
                help(tip);
            };
            integer("Map resolution", "resolution", settings.shadows.resolution,
                    "Pixels per shadow-map side. Higher values improve detail and use more GPU "
                    "memory. Device limits and a 256 MiB depth-payload budget are checked before "
                    "allocation.");
            integer("Directional cascades", "cascades", settings.shadows.cascades,
                    "1–8 depth ranges per directional light. More ranges preserve nearby detail "
                    "over longer distances. Spot lights use one map; point lights use six faces.");
            integer("Maximum shadow lights", "max_lights", settings.shadows.max_lights,
                    "1–8 shadow-casting lights per camera. Additional lights still illuminate the "
                    "scene and report a shadow-budget diagnostic in Problems.");
            double distance = settings.shadows.distance;
            property_label_row("Shadow distance (m)",
                               "Maximum camera depth for directional shadows.");
            if (ImGui::InputDouble("##distance", &distance, 0, 0, "%.3f",
                                   ImGuiInputTextFlags_EnterReturnsTrue))
                commit({{"shadows", {{"distance", distance}}}});
            help("Maximum camera depth for directional shadows, fading at the far end. Also bounds "
                 "point and spot shadows when the light has no finite range. Enter commits.");
        } catch (const std::exception& e) {
            field_error(e.what());
            report_error("scene.lighting", e.what());
        }
        ImGui::EndDisabled();
    }
    ImGui::End();
}
} // namespace forge::ui
