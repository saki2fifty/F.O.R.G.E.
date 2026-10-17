#include "editor_capture_input.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace forge::test;
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
} // namespace
int main() {
    try {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {300, 300};
        io.DeltaTime = 1.f / 60;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        EditorCaptureInput input;
        bool hover = false, clicked = false;
        auto frame = [&](unsigned stage, unsigned frames) {
            // Reproduce the platform/static-capture overwrite that caused native159.
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            input.after_platform_update(io, stage, frames);
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize({300, 300});
            ImGui::Begin("Pointer transport", nullptr, ImGuiWindowFlags_NoTitleBar);
            ImGui::SetCursorScreenPos({30, 30});
            clicked = ImGui::InvisibleButton("Target", {40, 40});
            hover = ImGui::IsItemHovered();
            ImGui::End();
            ImGui::Render();
        };
        frame(121, 2);
        frame(121, 3);
        input.queue(io, {50, 50}, 0);
        frame(121, 5);
        require(hover, "Platform update lost controlled pointer");
        input.queue(io, {50, 50}, 1);
        frame(121, 6);
        input.queue(io, {50, 50}, 2);
        frame(121, 7);
        require(clicked, "Actual widget did not receive click/release");
        frame(121, 26);
        require(!hover && !ImGui::IsMousePosValid(), "Completed setup leaked hover/tooltips");
        frame(122, 5);
        require(!hover, "Noninteractive capture inherited pointer");
        input.queue(io, {50, 50}, 0);
        frame(142, 5);
        require(hover, "Vertex capture pointer not applied");
        frame(142, 23);
        require(!hover, "Vertex setup did not end at finite bound");
        for (unsigned stage : {235u, 238u, 293u, 296u, 297u, 304u}) {
            input.click_once(io, stage, 4, {50, 50}, false);
            frame(stage, 4);
            require(!hover, "Capture clicked before its widget became ready");
            input.click_once(io, stage, 40, {50, 50}, true);
            frame(stage, 41);
            require(!hover && input.click_stage != stage,
                    "Capture clicked on its first ready observation");
            // A moved target and a readiness gap must each restart settling.
            input.click_once(io, stage, 41, {51, 50}, true);
            frame(stage, 42);
            input.click_once(io, stage, 42, {50, 50}, true);
            input.click_once(io, stage, 43, {50, 50}, false);
            input.click_once(io, stage, 44, {50, 50}, true);
            input.click_once(io, stage, 45, {50, 50}, true);
            require(input.click_stage != stage, "Readiness gap did not reset settling");
            input.click_once(io, stage, 46, {50, 50}, true);
            frame(stage, 46);
            require(hover && !io.MouseDown[0], "Capture did not hover before pressing");
            frame(stage, 47);
            require(io.MouseDown[0] && !clicked, "Capture omitted the press frame");
            // A menu/control may disappear on press. No further click_once call
            // is needed to release after the platform overwrites its input.
            frame(stage, 48);
            require(clicked, "Capture did not receive actual click/release");
            input.click_once(io, stage, 49, {50, 50}, true);
            frame(stage, 51);
            require(!hover, "Capture setup leaked pointer past its finite bound");
        }
        input = {};
        input.click_once(io, 238, 60, {50, 50}, true);
        input.click_once(io, 238, 61, {50, 50}, true);
        input.click_once(io, 238, 62, {50, 50}, true);
        frame(238, 62);
        frame(238, 63);
        require(io.MouseDown[0], "Stage departure fixture omitted initial press");
        frame(239, 0);
        require(!io.MouseDown[0], "Leaving a capture stage leaked a held button");
        input.click_once(io, 235, 65, {50, 50}, true);
        input.click_once(io, 235, 66, {50, 50}, true);
        input.click_once(io, 235, 67, {std::numeric_limits<float>::quiet_NaN(), 50}, true);
        require(input.click_stage != 235 && input.stable_frames == 0,
                "Nonfinite click target did not reset settling");
        input.queue(io, {std::numeric_limits<float>::quiet_NaN(), 50}, 0);
        frame(124, 5);
        require(!hover && !input.valid, "Nonfinite fixture pointer accepted");
        require(EditorCaptureInput::document_focus_allowed(121, false) &&
                    !EditorCaptureInput::document_focus_allowed(121, true) &&
                    !EditorCaptureInput::document_focus_allowed(124, true) &&
                    !EditorCaptureInput::document_focus_allowed(296, true) &&
                    !EditorCaptureInput::document_focus_allowed(297, true) &&
                    EditorCaptureInput::document_focus_allowed(297, false) &&
                    EditorCaptureInput::document_focus_allowed(305, true) &&
                    EditorCaptureInput::document_focus_allowed(122, true),
                "Finite setup popup focus policy differs");
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        int mode = 0;
        ImVec2 combo_at{};
        auto combo_frame = [&] {
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            input.after_platform_update(io, 121, 12);
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize({300, 300});
            ImGui::Begin("Tool capture", nullptr, ImGuiWindowFlags_NoTitleBar);
            const char* modes[] = {"Select", "Sculpt", "Paint"};
            ImGui::Combo("Tool", &mode, modes, 3);
            const auto lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
            combo_at = {(lo.x + hi.x) / 2, (lo.y + hi.y) / 2};
            ImGui::End();
            if (EditorCaptureInput::document_focus_allowed(
                    121, ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId |
                                                         ImGuiPopupFlags_AnyPopupLevel)))
                ImGui::SetWindowFocus("Tool capture");
            ImGui::Render();
        };
        combo_frame();
        combo_frame();
        input.queue(io, combo_at, 0);
        combo_frame();
        input.queue(io, combo_at, 1);
        combo_frame();
        input.queue(io, combo_at, 2);
        combo_frame();
        combo_frame();
        require(
            ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel),
            "Actual tool combo did not open");
        for (const auto key : {ImGuiKey_Home, ImGuiKey_DownArrow, ImGuiKey_Enter}) {
            io.AddKeyEvent(key, true);
            combo_frame();
            io.AddKeyEvent(key, false);
            combo_frame();
        }
        require(mode == 1, "Parent focus stole actual combo selection");
        ImGui::DestroyContext();
        std::cout << "Platform overwrite/real widget click/static suppression/finite capture "
                     "bounds passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
