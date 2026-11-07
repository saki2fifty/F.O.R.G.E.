#pragma once
#include "editor_state.hpp"
#include "ui_probe.hpp"
#include "widgets.hpp"
#include <cmath>
#include <forge/game_settings.hpp>
#include <misc/cpp/imgui_stdlib.h>
#include <set>

namespace forge {
// Edits only the existing Project Settings draft. The shared value owner admits it on Save.
struct AudioRoutingEditor {
    using Json = nlohmann::json;
    static void draw(Json& audio) {
        const bool expanded = ImGui::TreeNode("Custom audio buses");
        FORGE_UI_PROBE("audio-routing:tree");
        if (!expanded)
            return;
        ui::help("Project-local scalar routing. Save Settings, then restart Play. Sources select "
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
                    ImGui::TreePop();
                    break;
                }
                FORGE_UI_PROBE("audio-routing:remove:" + std::to_string(i));
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
