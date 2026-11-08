#pragma once
#include "editor_state.hpp"
#include "property_drawer.hpp"
#include "ui_probe.hpp"
#include "widgets.hpp"
#include <algorithm>
#include <cmath>
#include <forge/game_settings.hpp>
#include <misc/cpp/imgui_stdlib.h>
#include <set>

namespace forge {
// Edits only the existing Project Settings draft. The shared value owner admits it on Save.
struct AudioRoutingEditor {
    using Json = nlohmann::json;
    static void draw(Json& audio, const AssetCatalog* catalog = nullptr) {
        const bool expanded = ImGui::TreeNode("Custom audio buses");
        FORGE_UI_PROBE("audio-routing:tree");
        if (!expanded)
            return;
        ui::help("Project-local routing and shared effects. Save Settings, then restart Play. "
                 "Sources select "
                 "stable keys; changing a key does not rename references in Scenes or Prefabs.");
        if (!audio.contains("routing")) {
            if (ui::button("Add custom bus",
                           "Create a project audio route in this settings draft."))
                audio["routing"] = audio_routing_json({{{"bus_1", "", 1, "effects", {}}}});
            FORGE_UI_PROBE("audio-routing:add-first");
            ImGui::TreePop();
            return;
        }
        if (audio.contains("routing")) {
            auto& buses = audio["routing"]["buses"];
            for (std::size_t i = 0; i < buses.size(); ++i) {
                ui::IdScope row(std::to_string(i).c_str());
                auto& bus = buses[i];
                auto key = bus.at("key").get<std::string>();
                const auto title = bus.value("label", std::string{});
                const bool open =
                    ImGui::TreeNodeEx("bus", ImGuiTreeNodeFlags_DefaultOpen, "%s (%s)",
                                      title.empty() ? "Bus" : title.c_str(), key.c_str());
                if (!open)
                    continue;
                push_field_width("Initial volume");
                if (ImGui::InputText("Key", &key)) {
                    // An imported/default settings override follows this row's numeric default;
                    // source/edge identities remain explicit and are never silently rewritten.
                    if (audio.contains("bus_volumes") &&
                        audio["bus_volumes"].contains(bus.at("key").get<std::string>())) {
                        const auto old_key = bus.at("key").get<std::string>();
                        bus["volume"] = audio["bus_volumes"].at(old_key);
                        audio["bus_volumes"].erase(old_key);
                    }
                    bus["key"] = key;
                }
                FORGE_UI_PROBE("audio-routing:key:" + std::to_string(i));
                ui::help(
                    "Stable key: lowercase ASCII letter, then letters, digits or underscores; "
                    "at most64 bytes. Stock terminal names are reserved. References use this key.");
                auto label = bus.value("label", std::string{});
                if (ImGui::InputText("Label", &label))
                    bus["label"] = label;
                FORGE_UI_PROBE("audio-routing:label:" + std::to_string(i));
                const auto overrides = audio.value("bus_volumes", Json::object());
                float volume = overrides.value(key, bus.value("volume", 1.f));
                if (ImGui::SliderFloat("Initial volume", &volume, 0, 1, "%.3f",
                                       ImGuiSliderFlags_AlwaysClamp) &&
                    std::isfinite(volume)) {
                    bus["volume"] = volume;
                    if (audio.contains("bus_volumes"))
                        audio["bus_volumes"].erase(key);
                }
                FORGE_UI_PROBE("audio-routing:volume:" + std::to_string(i));
                effects(bus, i, catalog);
                target("Output", bus["output"], buses);
                FORGE_UI_PROBE("audio-routing:output:" + std::to_string(i));
                ui::help(
                    "Main post-fader path, gain1. Custom paths ultimately reach Master, Music, "
                    "Effects or Dialogue. Their category faders and Master apply afterward.");
                Json missing_sends = Json::array();
                auto& sends = bus.contains("sends") ? bus["sends"] : missing_sends;
                for (std::size_t j = 0; j < sends.size(); ++j) {
                    ui::IdScope send(std::to_string(j).c_str());
                    target("Send target", sends[j]["target"], buses);
                    float gain = sends[j].value("gain", 1.f);
                    if (ImGui::SliderFloat("Send gain", &gain, 0, 1, "%.3f",
                                           ImGuiSliderFlags_AlwaysClamp) &&
                        std::isfinite(gain))
                        sends[j]["gain"] = gain;
                    FORGE_UI_PROBE("audio-routing:send-gain:" + std::to_string(i) + ":" +
                                   std::to_string(j));
                    const bool remove_send =
                        ui::button("Remove send", "Remove this additional path from the draft.");
                    FORGE_UI_PROBE("audio-routing:remove-send:" + std::to_string(i) + ":" +
                                   std::to_string(j));
                    if (remove_send) {
                        sends.erase(j);
                        break;
                    }
                }
                ImGui::BeginDisabled(sends.size() >= audio_bus_send_limit);
                if (ui::button("Add send",
                               "Add an extra post-fader path. Sends sum, without normalization.")) {
                    std::set<std::string> used{bus.at("output").get<std::string>()};
                    for (const auto& send : sends)
                        used.insert(send.at("target").get<std::string>());
                    for (const char* terminal : {"master", "music", "effects", "dialogue"})
                        if (!used.contains(terminal)) {
                            sends.push_back({{"target", terminal}, {"gain", .25f}});
                            break;
                        }
                }
                if (!missing_sends.empty())
                    bus["sends"] = std::move(missing_sends);
                FORGE_UI_PROBE("audio-routing:add-send:" + std::to_string(i));
                ImGui::EndDisabled();
                if (ui::button("Remove bus", "Remove this draft definition. Referencing routes and "
                                             "sources must be updated explicitly.")) {
                    if (audio.contains("bus_volumes"))
                        audio["bus_volumes"].erase(key);
                    buses.erase(i);
                    ImGui::PopItemWidth();
                    ImGui::TreePop();
                    break;
                }
                FORGE_UI_PROBE("audio-routing:remove:" + std::to_string(i));
                ImGui::PopItemWidth();
                ImGui::TreePop();
            }
            ImGui::BeginDisabled(buses.size() >= audio_custom_bus_limit);
            if (ui::button("Add custom bus", "Add one of at most16 project-local buses.")) {
                std::set<std::string> used;
                for (const auto& bus : buses)
                    used.insert(bus.at("key").get<std::string>());
                unsigned number = 1;
                while (used.contains("bus_" + std::to_string(number)))
                    ++number;
                buses.push_back({{"key", "bus_" + std::to_string(number)},
                                 {"label", ""},
                                 {"volume", 1.f},
                                 {"output", "effects"},
                                 {"sends", Json::array()}});
            }
            FORGE_UI_PROBE("audio-routing:add");
            ImGui::EndDisabled();
            try {
                (void)audio_bus_volumes(audio);
                ImGui::TextDisabled("Routing draft valid | %zu / 16 buses", buses.size());
            } catch (const std::exception& error) {
                ui::field_error(error.what());
            }
        }
        ImGui::TreePop();
    }

  private:
    static void push_field_width(const char* longest_label) {
        ImGui::PushItemWidth(
            std::max(4 * ImGui::GetFontSize(), ImGui::GetContentRegionAvail().x -
                                                   ImGui::CalcTextSize(longest_label).x -
                                                   ImGui::GetStyle().ItemInnerSpacing.x));
    }
    static void effects(Json& bus, [[maybe_unused]] std::size_t index,
                        const AssetCatalog* catalog) {
        const bool open = ImGui::TreeNode("Shared effects");
        FORGE_UI_PROBE("audio-routing:fx:" + std::to_string(index));
        if (!open)
            return;
        push_field_width("High-pass cutoff (Hz)");
        try {
            auto ordinary = bus.value("effects", Json::object());
            const bool has_impulse = ordinary.contains("impulse_response");
            const auto retained_impulse = ordinary.value("impulse_response", Json());
            ordinary.erase("impulse_response");
            auto value = audio_bus_effects_from_json(ordinary);
            bool changed = false;
            const auto toggle = [&](const char* label, [[maybe_unused]] const char* key,
                                    bool& flag) {
                changed |= ImGui::Checkbox(label, &flag);
                FORGE_UI_PROBE("audio-routing:fx-" + std::string(key) + ":" +
                               std::to_string(index));
            };
            const auto parameter = [&](const char* label, [[maybe_unused]] const char* key,
                                       float& number, float minimum, float maximum) {
                changed |= ImGui::SliderFloat(label, &number, minimum, maximum, "%.3f",
                                              ImGuiSliderFlags_AlwaysClamp);
                FORGE_UI_PROBE("audio-routing:fx-" + std::string(key) + ":" +
                               std::to_string(index));
            };
            ui::help("Incoming signals mix before High-pass > Low-pass > Echo > Reverb. The bus "
                     "volume and sends follow effects. Game-paused and independent audio retain "
                     "separate histories; Source Stop does not erase shared tails.");
            toggle("High-pass filter", "high-pass", value.high_pass_enabled);
            parameter("High-pass cutoff (Hz)", "high-pass-hz", value.high_pass_hz, 20, 20000);
            toggle("Low-pass filter", "low-pass", value.low_pass_enabled);
            parameter("Low-pass cutoff (Hz)", "low-pass-hz", value.low_pass_hz, 20, 20000);
            toggle("Echo", "echo", value.echo_enabled);
            parameter("Echo interval (s)", "echo-seconds", value.echo_seconds, .01f, 1);
            parameter("Echo feedback", "echo-feedback", value.echo_feedback, 0, .75f);
            toggle("Reverb", "reverb", value.reverb_enabled);
            parameter("Reverb decay (s)", "reverb-decay", value.reverb_decay, .1f, 10);
            parameter("Reverb damping", "reverb-damping", value.reverb_damping, 0, .95f);
            parameter("Reverb wet mix", "reverb-wet", value.reverb_wet, 0, 1);
            ui::help("Inactive parameters are saved too. Feedback can amplify peaks; reduce "
                     "source or bus volume as needed. Save Settings applies this complete draft.");
            if (changed) {
                validate_audio_bus_effects(value);
                if (value == AudioBusEffects{} && !has_impulse)
                    bus.erase("effects");
                else {
                    bus["effects"] = audio_bus_effects_json(value);
                    if (has_impulse)
                        bus["effects"]["impulse_response"] = retained_impulse;
                }
            }
            impulse_response(bus, index, catalog);
            if (ui::button("Reset effects", "Restore bypassed defaults for this bus."))
                bus.erase("effects");
            FORGE_UI_PROBE("audio-routing:fx-reset:" + std::to_string(index));
        } catch (const std::exception& error) {
            ui::field_error(error.what());
        }
        ImGui::PopItemWidth();
        ImGui::TreePop();
    }
    static void impulse_response(Json& bus, [[maybe_unused]] std::size_t index,
                                 const AssetCatalog* catalog) {
        if (!bus.contains("effects") || !bus.at("effects").contains("impulse_response")) {
            ImGui::BeginDisabled(!catalog);
            if (ui::button("Add impulse response", "Choose an imported response for this bus."))
                bus["effects"]["impulse_response"] = {{"clip", nullptr}, {"wet", 1.f}};
            FORGE_UI_PROBE("audio-routing:impulse-add:" + std::to_string(index));
            ImGui::EndDisabled();
            return;
        }
        auto& impulse = bus["effects"]["impulse_response"];
        if (catalog)
            asset_ref_picker(*catalog, impulse["clip"], AudioClipAsset::type, "Impulse AudioClip",
                             false);
        else
            ImGui::TextDisabled("Project Content must be ready to select the response.");
        FORGE_UI_PROBE("audio-routing:impulse-clip:" + std::to_string(index));
        float wet = impulse.value("wet", 1.f);
        if (ImGui::SliderFloat("Impulse wet mix", &wet, 0, 1, "%.3f", ImGuiSliderFlags_AlwaysClamp))
            impulse["wet"] = wet;
        FORGE_UI_PROBE("audio-routing:impulse-wet:" + std::to_string(index));
        ui::help(
            "Use a resident 48000 Hz mono or stereo AudioClip, up to two seconds. The response "
            "follows the other shared effects. Zero wet mix bypasses it; Save Settings and "
            "restart Play to apply changes.");
        if (ui::button("Remove impulse response", "Remove this bus's response assignment.")) {
            bus["effects"].erase("impulse_response");
            if (audio_bus_effects_from_json(bus["effects"]) == AudioBusEffects{})
                bus.erase("effects");
        }
        FORGE_UI_PROBE("audio-routing:impulse-remove:" + std::to_string(index));
    }
    static void target(const char* label, Json& value, const Json& buses) {
        const auto current = value.get<std::string>();
        if (!ImGui::BeginCombo(label, current.c_str()))
            return;
        for (const char* terminal : {"master", "music", "effects", "dialogue"})
            if (ImGui::Selectable(terminal, current == terminal))
                value = terminal;
        std::size_t index = 0;
        for (const auto& bus : buses) {
            ui::IdScope choice(std::to_string(index++).c_str());
            const auto key = bus.at("key").get<std::string>();
            if (ImGui::Selectable(key.c_str(), current == key))
                value = key;
        }
        ImGui::EndCombo();
    }
};
} // namespace forge
