#pragma once
#include <TextEditor.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <stdexcept>
#include <string>

namespace forge::ui {
inline bool source_widget_owns_keyboard(const char* window_id) {
    if (!ImGui::GetCurrentContext())
        return false;
    const auto* owner = ImGui::FindWindowByName(window_id);
    for (const auto* window = ImGui::GetCurrentContext()->NavWindow; window;
         window = window->ParentWindow)
        if (window == owner)
            return true;
    return false;
}
// One ordinary widget Undo record, shared by native structured edits and automation.
// The synchronous clipboard adapter never reads or writes the OS clipboard.
inline void replace_source_widget_text(TextEditor& editor, const std::string& text) {
    if (!ImGui::GetCurrentContext() || editor.IsReadOnly())
        throw std::runtime_error("Open an editable source in the owning editor context");
    if (editor.GetText() == text)
        return;
    auto contents = text;
    if (!contents.empty() && contents.back() == '\n')
        contents.pop_back();
    auto& platform = ImGui::GetPlatformIO();
    struct ClipboardText {
        const char* text;
    } source{contents.c_str()};
    struct RestoreClipboard {
        ImGuiPlatformIO& platform;
        decltype(platform.Platform_GetClipboardTextFn) get;
        void* data;
        ~RestoreClipboard() {
            platform.Platform_GetClipboardTextFn = get;
            platform.Platform_ClipboardUserData = data;
        }
    } restore{platform, platform.Platform_GetClipboardTextFn, platform.Platform_ClipboardUserData};
    platform.Platform_ClipboardUserData = &source;
    platform.Platform_GetClipboardTextFn = [](ImGuiContext* context) {
        return static_cast<const ClipboardText*>(context->PlatformIO.Platform_ClipboardUserData)
            ->text;
    };
    editor.SelectAll();
    if (contents.empty())
        editor.Delete();
    else
        editor.Paste();
}
} // namespace forge::ui
