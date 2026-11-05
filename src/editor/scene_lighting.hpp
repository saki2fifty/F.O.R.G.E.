#pragma once
#include "property_drawer.hpp"
#include <algorithm>
#include <forge/authoring.hpp>
#include <forge/scene_render_settings.hpp>
#include <numbers>
namespace forge::ui {
inline void scene_transparency_controls(bool enabled, bool locked,
                                        const std::function<void(const Json&)>& commit) {
    IdScope scope("weighted_transparency");
    const bool open = ImGui::CollapsingHeader("Transparency");
    FORGE_UI_PROBE("transparency:section");
    if (!open)
        return;
    DisabledScope disabled(locked);
    if (ImGui::Checkbox("Weighted transparency", &enabled))
        commit({{"weighted_transparency", enabled}});
    FORGE_UI_PROBE("transparency:weighted");
    ImGui::TextWrapped(
        "Approximate order-independent blending for transparent Mesh surfaces. "
        "Materials must test depth without writing it. Views containing transmission "
        "or 2D layers retain sorted alpha. Older surface Shaders need rebuilding. "
        "Scene Save and Undo include this setting.");
}
inline void scene_reflection_controls(const SceneScreenSpaceReflections& settings, bool locked,
                                      const std::function<void(const Json&)>& commit) {
    IdScope scope("screen_space_reflections");
    const bool open = ImGui::CollapsingHeader("Screen-space reflections");
    FORGE_UI_PROBE("reflection:section");
    if (!open)
        return;
    DisabledScope disabled(locked);
    bool enabled = settings.enabled;
    if (ImGui::Checkbox("Enable screen-space reflections", &enabled))
        commit({{"screen_space_reflections", {{"enabled", enabled}}}});
    FORGE_UI_PROBE("reflection:enabled");
    bool filtering = settings.roughness_filtering;
    if (ImGui::Checkbox("Roughness filtering", &filtering))
        commit({{"screen_space_reflections", {{"roughness_filtering", filtering}}}});
    FORGE_UI_PROBE("reflection:roughness_filtering");
    help("Average current visible radiance over a roughness-dependent footprint. Depth bounds "
         "limit mixing across surfaces. Adds view-owned textures and rendering work.");
    const auto field = [&](const char* label, const char* key, double value, const char* tip,
                           const char* format = "%.3f") {
        IdScope id(key);
        property_label_row(label, tip);
        if (ImGui::InputDouble("##value", &value, 0, 0, format,
                               ImGuiInputTextFlags_EnterReturnsTrue))
            commit({{"screen_space_reflections", {{key, value}}}});
        FORGE_UI_PROBE(std::string("reflection:") + key);
    };
    field("Maximum distance (m)", "max_distance", settings.max_distance,
          "Visible-depth ray reach from 0 to 100 metres. Zero skips reflections.");
    field("Thickness (m)", "thickness", std::max(double(settings.thickness), .0001),
          "Depth tolerance from 0.0001 to 1 metre. Smaller values reject more intersections.",
          "%.4f");
    field("Strength", "strength", settings.strength,
          "Blend from 0 to 1 between environment and admitted visible-surface reflection. Zero "
          "skips the pass.");
    field("Maximum roughness", "max_roughness", settings.max_roughness,
          "Surface roughness cutoff from 0 to 1. Reflections fade near this limit; zero skips the "
          "pass.");
    if (button("Reset screen-space reflections", "Disable reflections and restore 25 m, thickness "
                                                 "0.1 m, strength 1, maximum roughness 0.5 and "
                                                 "roughness filtering off."))
        commit({{"screen_space_reflections",
                 {{"enabled", false},
                  {"max_distance", 25},
                  {"thickness", .1},
                  {"strength", 1},
                  {"max_roughness", .5},
                  {"roughness_filtering", false}}}});
    FORGE_UI_PROBE("reflection:reset");
    ImGui::TextWrapped(
        "Uses current visible Mesh depth and real base-material normals. Misses retain environment "
        "lighting. "
        "Hidden and off-screen surfaces are unavailable. Roughness filtering softens the visible "
        "reflection source; reflections still fade near the roughness cutoff. "
        "independent coat/sheen lobes and anisotropic surfaces retain environment lighting. "
        "Game cameras must clear color and depth. Older surface Shaders need a recook. "
        "Scene Save and Undo include these settings.");
}
inline void scene_occlusion_controls(const SceneAmbientOcclusion& settings, bool locked,
                                     const std::function<void(const Json&)>& commit) {
    IdScope scope("ambient_occlusion");
    const bool open = ImGui::CollapsingHeader("Ambient occlusion");
    FORGE_UI_PROBE("occlusion:section");
    if (!open)
        return;
    DisabledScope disabled(locked);
    bool enabled = settings.enabled;
    if (ImGui::Checkbox("Enable ambient occlusion", &enabled))
        commit({{"ambient_occlusion", {{"enabled", enabled}}}});
    FORGE_UI_PROBE("occlusion:enabled");
    const auto field = [&](const char* label, const char* key, float value, const char* tip) {
        IdScope id(key);
        property_label_row(label, tip);
        if (ImGui::InputFloat("##value", &value, 0, 0, "%.3f",
                              ImGuiInputTextFlags_EnterReturnsTrue))
            commit({{"ambient_occlusion", {{key, value}}}});
        FORGE_UI_PROBE(std::string("occlusion:") + key);
    };
    field("Radius (m)", "radius", settings.radius,
          "Nearby visible surfaces within 0 to 10 metres may occlude ambient lighting. Zero skips "
          "the pass.");
    field("Strength", "strength", settings.strength,
          "Ambient-light reduction from 0 to 1. Zero skips the pass.");
    field("Depth bias (m)", "bias", settings.bias,
          "Depth tolerance from 0 to 1 metre. Increase to reduce self-occlusion.");
    field("Normal influence", "normal_influence", settings.normal_influence,
          "Blend material shading normals into the sampling direction, from 0 to 1. Zero keeps "
          "depth-derived normals. Normal maps add shading detail without changing geometry.");
    if (button("Reset ambient occlusion", "Disable occlusion and restore 0.5 m radius, strength 1, "
                                          "0.01 m bias and normal influence 0."))
        commit({{"ambient_occlusion",
                 {{"enabled", false},
                  {"radius", .5},
                  {"strength", 1},
                  {"bias", .01},
                  {"normal_influence", 0}}}});
    FORGE_UI_PROBE("occlusion:reset");
    ImGui::TextWrapped(
        "Reduces environment lighting near visible opaque and masked Mesh surfaces. "
        "Direct light, emission and fog color remain unchanged. Normal influence can refine "
        "depth-based sampling with material normals; it does not add geometry or hidden surfaces. "
        "Runs before temporal anti-aliasing, motion blur and depth of field. "
        "Game cameras must clear color and depth. Older surface Shader assets need a recook. "
        "Scene Save and Undo include these settings.");
}
inline void scene_motion_controls(const SceneMotionBlur& settings, bool locked,
                                  const std::function<void(const Json&)>& commit) {
    IdScope scope("motion_blur");
    const bool open = ImGui::CollapsingHeader("Motion blur");
    FORGE_UI_PROBE("motion:section");
    if (!open)
        return;
    DisabledScope disabled(locked);
    bool enabled = settings.enabled;
    if (ImGui::Checkbox("Enable motion blur", &enabled))
        commit({{"motion_blur", {{"enabled", enabled}}}});
    FORGE_UI_PROBE("motion:enabled");
    const auto field = [&](const char* label, const char* key, float value, const char* tip) {
        IdScope id(key);
        property_label_row(label, tip);
        if (ImGui::InputFloat("##value", &value, 0, 0, "%.3f",
                              ImGuiInputTextFlags_EnterReturnsTrue))
            commit({{"motion_blur", {{key, value}}}});
        FORGE_UI_PROBE(std::string("motion:") + key);
    };
    field("Shutter fraction", "shutter", settings.shutter,
          "Fraction from 0 to 1 of movement since the previous completed image. Zero stays sharp.");
    field("Maximum trail (px)", "max_pixels", settings.max_pixels,
          "Cap from 0 to 32 view pixels. Zero stays sharp.");
    if (button("Reset motion blur", "Disable blur and restore shutter 0.5, trail 32 pixels."))
        commit({{"motion_blur", {{"enabled", false}, {"shutter", .5}, {"max_pixels", 32}}}});
    FORGE_UI_PROBE("motion:reset");
    ImGui::TextWrapped(
        "Uses completed camera and Mesh movement, including morphs and skinning. "
        "Works with temporal anti-aliasing on or off; runs before depth of field. "
        "Depth and movement boundaries retain their silhouettes. Other layers stay sharp. "
        "Game cameras must clear both color and depth. The first image after a reset stays sharp. "
        "Scene Save and Undo include these settings.");
}
inline void scene_focus_controls(const SceneDepthOfField& settings, bool locked,
                                 const std::function<void(const Json&)>& commit) {
    IdScope scope("depth_of_field");
    const bool open = ImGui::CollapsingHeader("Depth of field");
    FORGE_UI_PROBE("focus:section");
    if (!open)
        return;
    DisabledScope disabled(locked);
    bool enabled = settings.enabled;
    if (ImGui::Checkbox("Enable depth of field", &enabled))
        commit({{"depth_of_field", {{"enabled", enabled}}}});
    FORGE_UI_PROBE("focus:enabled");
    const auto field = [&](const char* label, const char* key, float value, const char* tip) {
        IdScope id(key);
        property_label_row(label, tip);
        if (ImGui::InputFloat("##value", &value, 0, 0, "%.3f",
                              ImGuiInputTextFlags_EnterReturnsTrue))
            commit({{"depth_of_field", {{key, value}}}});
        FORGE_UI_PROBE(std::string("focus:") + key);
    };
    field("Focus distance (m)", "focus_distance", settings.focus_distance,
          "Positive distance along each camera's view, from 0.001 to 1000000 metres.");
    field("Focus falloff (m)", "focus_range", settings.focus_range,
          "Distance from the focus plane that reaches maximum blur; 0.001 to 1000000 metres.");
    field("Maximum blur (px)", "blur_radius", settings.blur_radius,
          "Radius from 0 to 12 output pixels. Zero skips the pass. Enter commits one Undo step.");
    if (button("Reset depth of field",
               "Disable focus blur and restore 10 m, 5 m falloff and 8 px."))
        commit({{"depth_of_field",
                 {{"enabled", false},
                  {"focus_distance", 10},
                  {"focus_range", 5},
                  {"blur_radius", 8}}}});
    FORGE_UI_PROBE("focus:reset");
    ImGui::TextWrapped(
        "Focus settings belong to the Scene and apply separately to each view. "
        "Opaque and masked Mesh geometry blurs before current-frame layers and bloom. "
        "Background, transparency, Sprites, Tile Maps, particles and editor overlays stay sharp. "
        "Depth edges retain their silhouette; this spatial profile does not spread foreground "
        "blur across a different surface. Scene Save and Undo include these settings.");
}
inline void scene_fast_aa_controls(const SceneFastApproximateAntiAliasing& settings, bool locked,
                                   const std::function<void(const Json&)>& commit) {
    IdScope scope("fast_approximate_anti_aliasing");
    const bool open = ImGui::CollapsingHeader("Fast approximate anti-aliasing");
    FORGE_UI_PROBE("fast-aa:section");
    if (!open)
        return;
    DisabledScope disabled(locked);
    bool enabled = settings.enabled;
    if (ImGui::Checkbox("Enable fast approximate anti-aliasing", &enabled)) {
        Json patch = {{"fast_approximate_anti_aliasing", {{"enabled", enabled}}}};
        if (enabled)
            patch["morphological_anti_aliasing"] = {{"enabled", false}};
        commit(patch);
    }
    FORGE_UI_PROBE("fast-aa:enabled");
    float strength = settings.subpixel_strength;
    property_label_row("Subpixel smoothing", "From 0 to 1. Edge smoothing remains active at 0.");
    if (ImGui::InputFloat("##subpixel", &strength, 0, 0, "%.3f",
                          ImGuiInputTextFlags_EnterReturnsTrue))
        commit({{"fast_approximate_anti_aliasing", {{"subpixel_strength", strength}}}});
    FORGE_UI_PROBE("fast-aa:subpixel");
    int steps = int(settings.search_steps);
    property_label_row("Edge search steps", "From 1 to 16 pixels in each direction along an edge.");
    if (ImGui::InputInt("##steps", &steps, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue))
        commit({{"fast_approximate_anti_aliasing", {{"search_steps", steps}}}});
    FORGE_UI_PROBE("fast-aa:steps");
    if (button("Reset fast approximate anti-aliasing",
               "Disable filtering and restore smoothing 0.5 and 12 search steps."))
        commit({{"fast_approximate_anti_aliasing",
                 {{"enabled", false}, {"subpixel_strength", .5}, {"search_steps", 12}}}});
    FORGE_UI_PROBE("fast-aa:reset");
    ImGui::TextWrapped("Smooths current scene color after tone mapping, before UI and editor "
                       "overlays. No temporal history. Supports up to 8,388,608 output pixels. "
                       "Higher smoothing can soften fine details. Scene Save and Undo include "
                       "these settings.");
}
inline void scene_occlusion_culling_controls(const SceneOcclusionCulling& settings, bool locked,
                                             const std::function<void(const Json&)>& commit) {
    IdScope scope("occlusion_culling");
    const bool open = ImGui::CollapsingHeader("Occlusion culling");
    FORGE_UI_PROBE("occlusion-culling:section");
    if (!open)
        return;
    DisabledScope disabled(locked);
    bool enabled = settings.enabled;
    if (ImGui::Checkbox("Enable occlusion culling", &enabled))
        commit({{"occlusion_culling", {{"enabled", enabled}}}});
    FORGE_UI_PROBE("occlusion-culling:enabled");
    double bias = settings.depth_bias;
    property_label_row(
        "Depth safety bias",
        "Normalized depth from 0 to 0.01. Larger values keep more geometry visible.");
    if (ImGui::InputDouble("##bias", &bias, 0, 0, "%.6f", ImGuiInputTextFlags_EnterReturnsTrue))
        commit({{"occlusion_culling", {{"depth_bias", bias}}}});
    FORGE_UI_PROBE("occlusion-culling:bias");
    if (button("Reset occlusion culling", "Disable culling and restore depth safety bias 0.0001."))
        commit({{"occlusion_culling", {{"enabled", false}, {"depth_bias", .0001}}}});
    FORGE_UI_PROBE("occlusion-culling:reset");
    ImGui::TextWrapped(
        "Uses up to 32 simple opaque depth occluders and 4096 compatible batches per view. "
        "Masked, transparent, skinned, morphed, wind-deformed and custom surfaces keep "
        "ordinary submission. Uncertain bounds or unsupported targets retain geometry. "
        "The depth hierarchy is rebuilt for each camera; no previous-frame visibility. "
        "Scene Save and Undo include these settings.");
}
inline void scene_morphological_aa_controls(const SceneMorphologicalAntiAliasing& settings,
                                            bool locked,
                                            const std::function<void(const Json&)>& commit) {
    IdScope scope("morphological_anti_aliasing");
    const bool open = ImGui::CollapsingHeader("Morphological anti-aliasing");
    FORGE_UI_PROBE("morphological-aa:section");
    if (!open)
        return;
    DisabledScope disabled(locked);
    bool enabled = settings.enabled;
    if (ImGui::Checkbox("Enable morphological anti-aliasing", &enabled)) {
        Json patch = {{"morphological_anti_aliasing", {{"enabled", enabled}}}};
        if (enabled)
            patch["fast_approximate_anti_aliasing"] = {{"enabled", false}};
        commit(patch);
    }
    FORGE_UI_PROBE("morphological-aa:enabled");
    double threshold = std::max(double(settings.edge_threshold), .01);
    property_label_row(
        "Edge threshold",
        "Display-color difference from 0.01 to 0.5. Lower values detect more edges.");
    if (ImGui::InputDouble("##threshold", &threshold, 0, 0, "%.3f",
                           ImGuiInputTextFlags_EnterReturnsTrue))
        commit({{"morphological_anti_aliasing", {{"edge_threshold", threshold}}}});
    FORGE_UI_PROBE("morphological-aa:threshold");
    int steps = int(settings.search_steps);
    property_label_row(
        "Contour search steps",
        "From 1 to 32 pixels per direction. Longer searches can resolve longer contours.");
    if (ImGui::InputInt("##steps", &steps, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue))
        commit({{"morphological_anti_aliasing", {{"search_steps", steps}}}});
    FORGE_UI_PROBE("morphological-aa:steps");
    float rounding = settings.corner_rounding;
    property_label_row(
        "Corner rounding",
        "From 0 to 1. Zero preserves detected sharp corners; larger values allow smoothing.");
    if (ImGui::InputFloat("##rounding", &rounding, 0, 0, "%.3f",
                          ImGuiInputTextFlags_EnterReturnsTrue))
        commit({{"morphological_anti_aliasing", {{"corner_rounding", rounding}}}});
    FORGE_UI_PROBE("morphological-aa:rounding");
    if (button("Reset morphological anti-aliasing",
               "Disable and restore threshold 0.1, 16 search steps and zero corner rounding."))
        commit({{"morphological_anti_aliasing",
                 {{"enabled", false},
                  {"edge_threshold", .1},
                  {"search_steps", 16},
                  {"corner_rounding", 0}}}});
    FORGE_UI_PROBE("morphological-aa:reset");
    ImGui::TextWrapped(
        "Reconstructs edges in the current display image before UI and guides. Enabling selects "
        "this spatial method; temporal anti-aliasing remains independent. Up to 8,388,608 pixels; "
        "optional cached images use up to 128 MiB of logical pixel storage.");
}

inline void scene_temporal_controls(const SceneTemporalAntiAliasing& settings, bool locked,
                                    const std::function<void(const Json&)>& commit) {
    IdScope scope("temporal_anti_aliasing");
    const bool open = ImGui::CollapsingHeader("Temporal anti-aliasing");
    FORGE_UI_PROBE("temporal:section");
    if (!open)
        return;
    DisabledScope disabled(locked);
    bool enabled = settings.enabled;
    if (ImGui::Checkbox("Enable temporal anti-aliasing", &enabled))
        commit({{"temporal_anti_aliasing", {{"enabled", enabled}}}});
    FORGE_UI_PROBE("temporal:enabled");
    float stability = settings.stability;
    property_label_row("History stability",
                       "History weight from 0 to 0.95. Enter commits one Undo step.");
    if (ImGui::InputFloat("##stability", &stability, 0, 0, "%.3f",
                          ImGuiInputTextFlags_EnterReturnsTrue))
        commit({{"temporal_anti_aliasing", {{"stability", stability}}}});
    FORGE_UI_PROBE("temporal:stability");
    help("Higher stability smooths changing edges over more frames. Motion and depth reject "
         "unrelated history; a camera cut or resize starts a fresh image.");
    if (button("Reset temporal anti-aliasing", "Disable accumulation and restore stability 0.8."))
        commit({{"temporal_anti_aliasing", {{"enabled", false}, {"stability", .8}}}});
    FORGE_UI_PROBE("temporal:reset");
    ImGui::TextWrapped(
        "Accumulates opaque and masked Mesh geometry, including morphs and skinning. "
        "Game cameras must clear both color and depth. Transparency, particles, "
        "Sprites, Tile Maps, sky and overlays remain current-frame layers. "
        "Custom surface Shaders need a current cook. Scene Save and Undo include these settings.");
}
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
            scene_reflection_controls(settings.screen_space_reflections, locked, commit);
            scene_transparency_controls(settings.weighted_transparency, locked, commit);
            scene_occlusion_culling_controls(settings.occlusion_culling, locked, commit);
            scene_occlusion_controls(settings.ambient_occlusion, locked, commit);
            scene_motion_controls(settings.motion_blur, locked, commit);
            scene_focus_controls(settings.depth_of_field, locked, commit);
            scene_temporal_controls(settings.temporal_anti_aliasing, locked, commit);
            scene_fast_aa_controls(settings.fast_approximate_anti_aliasing, locked, commit);
            scene_morphological_aa_controls(settings.morphological_anti_aliasing, locked, commit);
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
