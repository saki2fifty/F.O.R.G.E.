#pragma once
#include "property_drawer.hpp"
#include <forge/authoring.hpp>
#include <forge/scene_render_settings.hpp>
#include <numbers>
namespace forge::ui {
inline void scene_bloom_controls(const SceneBloom& settings, bool locked,
                                 const std::function<void(const Json&)>& commit) {
    IdScope scope("bloom");
    heading("Bloom", "Glow around bright surfaces. Runs before display exposure and tone mapping.");
    struct Disabled {
        explicit Disabled(bool value) { ImGui::BeginDisabled(value); }
        ~Disabled() { ImGui::EndDisabled(); }
    } disabled(locked);
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
                 "mapping. Enter commits.");
            scene_bloom_controls(settings.bloom, locked, commit);
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
