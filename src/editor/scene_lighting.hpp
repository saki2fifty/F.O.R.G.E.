#pragma once
#include "property_drawer.hpp"
#include <forge/authoring.hpp>
#include <forge/scene_render_settings.hpp>
#include <numbers>
namespace forge::ui {
inline void scene_lighting(bool& open, Scene& scene, const AssetCatalog* catalog, bool locked) {
    if (!open)
        return;
    ImGui::SetNextWindowSize({440 * interface_scale, 340 * interface_scale},
                             ImGuiCond_FirstUseEver);
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
            if (catalog) {
                if (asset_ref_picker(*catalog, texture, "texture", "Environment map"))
                    commit({{"environment", {{"texture", texture}}}});
            } else {
                ImGui::TextDisabled("Asset catalog is unavailable.");
                help("Open a project with an admitted asset catalog before assigning an "
                     "environment.");
            }
            ImGui::TextWrapped("Choose an imported color cubemap or equirectangular image. HDR "
                               "color is used when available.");
            float intensity = settings.environment.intensity;
            if (ImGui::InputFloat("Intensity", &intensity, 0, 0, "%.3f",
                                  ImGuiInputTextFlags_EnterReturnsTrue))
                commit({{"environment", {{"intensity", intensity}}}});
            help("Multiply sky brightness and image-based lighting. Zero disables both "
                 "contributions. Enter commits one undo step.");
            double degrees = std::remainder(settings.environment.rotation, 2 * std::numbers::pi) *
                             180 / std::numbers::pi;
            if (ImGui::InputDouble("Rotation (degrees)", &degrees, 0, 0, "%.2f",
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
            if (ImGui::InputFloat("Exposure (stops)", &exposure, 0, 0, "%.2f",
                                  ImGuiInputTextFlags_EnterReturnsTrue))
                commit({{"exposure", exposure}});
            help("Game-camera exposure from -20 to +20 stops. +1 doubles linear light before tone "
                 "mapping. Enter commits.");
            heading("Shadows", "Scene-wide shadow quality. Individual lights and mesh renderers "
                               "control casting and receiving in the Inspector.");
            bool enabled = settings.shadows.enabled;
            if (ImGui::Checkbox("Enable shadows", &enabled))
                commit({{"shadows", {{"enabled", enabled}}}});
            help("Enable directional, spot and point-light shadow maps. Each light must also have "
                 "Cast shadows enabled. Scene Save and Undo include these settings.");
            auto integer = [&](const char* label, const char* key, unsigned value,
                               const char* tip) {
                auto edited = value;
                if (ImGui::InputScalar(label, ImGuiDataType_U32, &edited, nullptr, nullptr, "%u",
                                       ImGuiInputTextFlags_EnterReturnsTrue))
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
            if (ImGui::InputDouble("Shadow distance (m)", &distance, 0, 0, "%.3f",
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
