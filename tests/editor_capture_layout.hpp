#pragma once
#include <algorithm>
#include <imgui.h>
#include <imgui_internal.h>

namespace forge::test {
inline void fit_capture_window(const char* name, float scale) {
    if (auto* window = ImGui::FindWindowByName(name); window && !window->DockIsActive) {
        const auto* viewport = ImGui::GetMainViewport();
        const float inset = 4 * scale;
        // SetWindowSize updates SizeFull immediately; Size is the previous Begin's
        // geometry. Clamping Size here would undo a requested fixture resize.
        const ImVec2 size{std::min(window->SizeFull.x, viewport->WorkSize.x - 2 * inset),
                          std::min(window->SizeFull.y, viewport->WorkSize.y - 2 * inset)};
        const ImVec2 position{
            std::clamp(window->Pos.x, viewport->WorkPos.x + inset,
                       viewport->WorkPos.x + viewport->WorkSize.x - size.x - inset),
            std::clamp(window->Pos.y, viewport->WorkPos.y + inset,
                       viewport->WorkPos.y + viewport->WorkSize.y - size.y - inset)};
        ImGui::SetWindowPos(window, position, ImGuiCond_Always);
        ImGui::SetWindowSize(window, size, ImGuiCond_Always);
    }
}
} // namespace forge::test
