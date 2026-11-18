#pragma once
#include "widgets.hpp"
#include <forge/input_message.hpp>
#include <nlohmann/json.hpp>

namespace forge::ui {
inline void draw_input_messages(const nlohmann::json& messages) {
    ImGui::Text("Input messages: %u sent this tick", messages.at("emitted").get<unsigned>());
    ImGui::Text(
        "Sent total: %llu | Last sent tick: %llu",
        static_cast<unsigned long long>(messages.at("emitted_total").get<std::uint64_t>()),
        static_cast<unsigned long long>(messages.at("last_sent_tick").get<std::uint64_t>()));
    help("Typed component copies dispatched before fixed gameplay systems. Sent counts do not "
         "prove that a receiver existed. Expand a source to inspect its last dispatch.");
    for (const auto& source : messages.at("sources")) {
        const auto ref = source.at("entity").get<EntityRef>();
        const auto id = ref.entity.str();
        IdScope scope(id.c_str());
        const auto* trigger = input_message_trigger_name(source.value("trigger", 0u));
        const auto name = source.value("name", source.at("payload").get<std::string>());
        const bool open = ImGui::TreeNode("##source", "%s [%s]: %s", name.c_str(),
                                          trigger ? trigger : "Invalid trigger",
                                          source.at("state").get_ref<const std::string&>().c_str());
        FORGE_UI_PROBE("input-message-monitor:" + id);
        if (!open)
            continue;
        ImGui::TextWrapped("Source: %s", id.c_str());
        ImGui::TextWrapped(
            "Action: %s",
            source.value("action_name", source.at("action").get<std::string>()).c_str());
        ImGui::TextWrapped("Payload: %s",
                           source.at("payload").get_ref<const std::string&>().c_str());
        if (source.contains("diagnostic"))
            ImGui::TextWrapped("%s", source.at("diagnostic").get_ref<const std::string&>().c_str());
        if (!source.contains("emitted_total") || !source.contains("last_sent")) {
            ImGui::TextWrapped("Source inspection is unavailable in this runtime. Use the runtime "
                               "included with this editor package.");
            FORGE_UI_PROBE("input-message-inspection-unavailable:" + id);
            ImGui::TreePop();
            continue;
        }
        ImGui::Text(
            "Source sent total: %llu",
            static_cast<unsigned long long>(source.at("emitted_total").get<std::uint64_t>()));
        if (source.contains("last_sent") && !source.at("last_sent").is_null()) {
            const auto& sent = source.at("last_sent");
            ImGui::Text(
                "Last sent tick: %llu | Encoded bytes: %llu",
                static_cast<unsigned long long>(sent.at("tick").get<std::uint64_t>()),
                static_cast<unsigned long long>(sent.at("encoded_bytes").get<std::size_t>()));
            if (sent.at("truncated").get<bool>())
                ImGui::TextWrapped("Preview truncated; this JSON text is incomplete.");
            help("Captured before observers changed the source. This read-only preview cannot "
                 "replay "
                 "a message. A different, inactive or retired route clears its source history.");
            const auto& preview = sent.at("preview").get_ref<const std::string&>();
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(preview.data(), preview.data() + preview.size());
            ImGui::PopTextWrapPos();
            FORGE_UI_PROBE("input-message-preview:" + id);
        } else
            ImGui::TextUnformatted("No dispatch captured for this source route.");
        ImGui::TreePop();
    }
    if (messages.at("overflow").get<unsigned>())
        ImGui::TextWrapped("%u sources exceed the 256-source tick limit.",
                           messages.at("overflow").get<unsigned>());
}
} // namespace forge::ui
