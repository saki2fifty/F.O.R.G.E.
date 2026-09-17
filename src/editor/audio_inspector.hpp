#pragma once
#include "document.hpp"
#include "widgets.hpp"
#include <forge/audio_components.hpp>
#include <forge/authoring.hpp>
namespace forge {
inline bool audio_clip_picker(const std::filesystem::path& project, Json& value) {
    std::optional<AssetCatalog> loaded;
    try {
        loaded.emplace(AssetCatalog::open_project(project));
    } catch (const std::exception& e) {
        ImGui::TextWrapped("Audio assets unavailable: %s", e.what());
        ui::help("The project asset index could not be read. Correct forge.assets.json and retry; "
                 "existing references are preserved.");
        return false;
    }
    const auto& catalog = *loaded;
    std::string label = "None";
    if (!value.is_null()) {
        const auto result = catalog.resolve(value.get<AssetId>(), AudioClipAsset::type);
        label = result.record ? path_utf8(result.record->source) : "Missing AudioClip";
    }
    bool changed = false;
    if (ImGui::BeginCombo("Audio clip", label.c_str())) {
        if (ImGui::Selectable("None", value.is_null())) {
            value = nullptr;
            changed = true;
        }
        ui::help("Leave this source unassigned; Play reports a missing clip until one is chosen.");
        for (const auto& [id, record] : catalog.records()) {
            if (record.type != AudioClipAsset::type)
                continue;
            const auto name = path_utf8(record.source);
            if (ImGui::Selectable(name.c_str(), value == Json(id))) {
                value = id;
                changed = true;
            }
            ui::help("Select this registered WAV by its persistent asset identity.");
        }
        ImGui::EndCombo();
    }
    ui::help("AudioClip asset. Register a project-relative WAV below; references survive supported "
             "catalog relocation.");
    return changed;
}
inline bool audio_field(const std::filesystem::path& root, const Json& field, Json& value) {
    const std::string key = field.at("id");
    if (field.at("type") == "asset_ref")
        return audio_clip_picker(root, value);
    const std::map<std::string, const char*> labels = {{"play_on_start", "Play on Start"},
                                                       {"loop", "Loop"},
                                                       {"gain", "Gain"},
                                                       {"pitch", "Pitch"},
                                                       {"spatialized", "Spatialized"},
                                                       {"minimum_distance", "Minimum distance"},
                                                       {"maximum_distance", "Maximum distance"},
                                                       {"enabled", "Enabled"}};
    const char* label = labels.contains(key) ? labels.at(key) : key.c_str();
    bool changed = false;
    if (field.at("type") == "bool") {
        bool next = value;
        changed = ImGui::Checkbox(label, &next);
        if (changed)
            value = next;
    } else {
        double next = value;
        changed =
            ImGui::InputDouble(label, &next, 0, 0, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue);
        if (changed)
            value = next;
    }
    const std::string help = field.at("description").get<std::string>() +
                             ". Units: " + field.at("unit").get<std::string>() +
                             ". Numeric edits commit with Enter.";
    ui::help(help.c_str());
    return changed;
}
inline void audio_inspector(Scene& scene, SceneDocument& project, const std::string& selected,
                            std::string& message) {
    if (selected.empty())
        return;
    try {
        Json item;
        const auto effective = scene.effective_document();
        for (const auto& e : effective.at("entities"))
            if (e.at("id") == selected)
                item = e;
        if (item.is_null())
            return;
        if (ImGui::TreeNode("Audio")) {
            struct TreeScope {
                ~TreeScope() { ImGui::TreePop(); }
            } tree_scope;
            ui::help("Gameplay sound sources and one enabled listener. Audio runs in the separate "
                     "Play process.");
            const auto schema = scene.schema();
            for (const auto& type : schema.at("components")) {
                const std::string key = type.at("id");
                if (!key.starts_with("forge.audio_"))
                    continue;
                ImGui::PushID(key.c_str());
                struct IdScope {
                    ~IdScope() { ImGui::PopID(); }
                } id_scope;
                const bool source = key == "forge.audio_source";
                if (!item.at("components").contains(key)) {
                    if (ui::button(source ? "Add AudioSource" : "Add AudioListener",
                                   "Add this optional component in one undoable edit."))
                        authoring_command(scene, "component.add",
                                          {{"entity", selected}, {"component", key}});
                } else {
                    ui::heading(source ? "Audio Source" : "Audio Listener",
                                "Inherited properties follow prefab defaults; edits preserve "
                                "explicit override intent.");
                    for (const auto& field : type.at("fields")) {
                        const std::string name = field.at("id");
                        auto value = item.at("components").at(key).at(name);
                        if (audio_field(project.project(), field, value))
                            authoring_command(scene, "property.set",
                                              {{"entity", selected},
                                               {"component", key},
                                               {"field", name},
                                               {"value", value}});
                    }
                    if (ui::button("Remove / Revert component",
                                   "Remove owned configuration or overrides. Inherited "
                                   "configuration becomes visible again. Undo restores the edit."))
                        authoring_command(scene, "component.revert",
                                          {{"entity", selected}, {"component", key}});
                }
            }
            static char source_path[1024] = "Assets/sound.wav";
            ImGui::InputText("WAV in project", source_path, sizeof(source_path));
            ui::help("Relative path of a WAV already inside the current project, for example "
                     "Assets/sound.wav. No file is copied or transcoded.");
            if (ui::button(
                    "Register WAV",
                    "Create persistent AudioClip metadata in forge.assets.json. This project asset "
                    "operation is separate from scene Undo. Select the clip above afterwards.")) {
                project.check_ownership();
                (void)AssetCatalog::register_audio_clip(project.project(),
                                                        std::filesystem::u8path(source_path));
                message = "WAV registered. Choose it in Audio clip.";
            }
            ImGui::TextWrapped("Pause freezes gameplay sound. Step stays silent; Resume continues. "
                               "One enabled listener is required for spatial sound.");
            ui::help("Gain 0 is silent, 1 nominal, up to 4. Pitch 1 is normal, range 0.25 to 4. "
                     "Distances are meters; object scale does not change range.");
        } else
            ui::help("Add a sound source/listener, register WAV assets and choose clips.");
    } catch (const std::exception& e) {
        message = e.what();
    }
}
} // namespace forge
