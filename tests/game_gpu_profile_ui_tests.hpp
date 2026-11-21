#pragma once
#include "editor/game_gpu_profile.hpp"
#include "imgui_input_driver.hpp"
#include "imgui_software_capture.hpp"
#include <array>
#include <tuple>
#include <utility>
#include <vector>

namespace forge::test {
// Production controls with the real query observations from the calling probe.
// Callbacks verify UI intent only; Windows process execution has its own gate.
inline void game_gpu_profile_controls(const nlohmann::json& report,
                                      const std::filesystem::path& output) {
    std::filesystem::create_directories(output);
    StandaloneGpuProfileReceiver receiver;
    receiver.append(standalone_gpu_profile_record(report));
    receiver.finish(true);
    StandaloneGpuProfileReceiver live_receiver(true);
    live_receiver.append(standalone_gpu_profile_live_record(report, 1));
    const bool old_observe = observe_ui;
    const float old_scale = ui::interface_scale;
    struct Restore {
        bool observe;
        float scale;
        ~Restore() {
            observe_ui = observe;
            ui::interface_scale = scale;
            ui_targets.clear();
        }
    } restore{old_observe, old_scale};
    const std::array<std::tuple<float, int, int, const char*>, 4> layouts{
        {{1.f, 840, 610, "wide-100"},
         {1.f, 480, 610, "narrow-100"},
         {1.5f, 1260, 915, "wide-150"},
         {2.f, 1680, 1220, "wide-200"}}};
    for (const auto& [scale, width, height, name] : layouts) {
        ImGuiSoftwareCapture capture(width, height);
        ui::style(scale);
        observe_ui = true;
        ImGui::GetIO().ConfigInputTrickleEventQueue = false;
        std::vector<std::pair<bool, bool>> requests;
        bool show_live = false, live_supported = true;
        bool supported = true;
        bool locked = false;
        const auto frame = [&] {
            ui_targets.clear();
            capture.new_frame();
            ImGui::SetNextWindowPos({8 * scale, 8 * scale}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({float(width) - 16 * scale, float(height) - 16 * scale},
                                     ImGuiCond_Always);
            ImGui::Begin("Export Game", nullptr, ImGuiWindowFlags_NoSavedSettings);
            ui::draw_game_gpu_profile_actions(
                supported, locked,
                [&](bool verify, bool live) { requests.emplace_back(verify, live); },
                live_supported);
            ui::draw_game_gpu_profile_report(show_live ? live_receiver : receiver);
            ImGui::End();
            capture.render();
        };
        frame();
        frame();
        capture.save(output / (std::string(name) + ".png"));
        ImGuiInputDriver input(frame);
        input.click("button:Profile game");
        input.click("button:Verify GPU timings");
        input.click("button:Watch GPU timings");
        input.click("button:Copy GPU report");
        const auto* copied = ImGui::GetClipboardText();
        if (!copied || nlohmann::json::parse(copied) != report)
            throw std::runtime_error("GPU report copy changed the accepted observation");
        if (requests !=
                std::vector<std::pair<bool, bool>>{{false, false}, {true, false}, {false, true}} ||
            receiver.report() != report)
            throw std::runtime_error("GPU controls changed native observations or routing intent");
        show_live = true;
        frame();
        frame();
        capture.save(output / (std::string(name) + "-live.png"));
        input.click("button:Copy live GPU observation");
        const auto live_copy = nlohmann::json::parse(ImGui::GetClipboardText());
        if (live_copy.at("provisional") != true || live_copy.at("report") != report ||
            live_copy.at("snapshot") != live_receiver.live_snapshot() ||
            !live_receiver.report().is_null())
            throw std::runtime_error(
                "Native live controls lost their provisional status or observation");
        live_supported = false;
        frame();
        if (ui_targets.at("button:Watch GPU timings").enabled ||
            !ui_targets.at("button:Profile game").enabled)
            throw std::runtime_error(
                "An older kit did not retain final-report actions while blocking live mode");
        live_supported = true;
        locked = true;
        frame();
        if (ui_targets.at("button:Profile game").enabled ||
            ui_targets.at("button:Verify GPU timings").enabled ||
            ui_targets.at("button:Watch GPU timings").enabled)
            throw std::runtime_error("Locked authoring left native GPU game actions enabled");
        locked = false;
        supported = false;
        frame();
        if (ui_targets.at("button:Profile game").enabled ||
            ui_targets.at("button:Verify GPU timings").enabled ||
            ui_targets.at("button:Watch GPU timings").enabled)
            throw std::runtime_error("An unsupported runtime kit left GPU actions enabled");
    }
}
} // namespace forge::test
