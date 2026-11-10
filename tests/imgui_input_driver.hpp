#pragma once
#include "editor/ui_probe.hpp"
#include <algorithm>
#include <functional>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
namespace forge::test {
// Drive actual ImGui input and scrolling through production control probe bounds.
class ImGuiInputDriver {
    std::function<void()> frame_;

  public:
    explicit ImGuiInputDriver(std::function<void()> frame) : frame_(std::move(frame)) {}
    void click(const char* key) { activate(key, false); }
    // Selectable hit rectangles deliberately include padding outside their row's
    // child clip. Activate the visible intersection without weakening the normal
    // full-control layout checks used by buttons and editable fields.
    void select(const char* key) { activate(key, true); }

  private:
    void activate(const char* key, bool selectable) {
        auto& io = ImGui::GetIO();

        test::UiTarget box;
        bool visible = false;
        for (unsigned attempt = 0; attempt < 40; ++attempt) {
            frame_();
            const auto found = test::ui_targets.find(key);
            if (found == test::ui_targets.end())
                throw std::runtime_error(std::string("Display control is absent: ") + key +
                                         " at scale " + std::to_string(io.DisplaySize.x));
            box = found->second;
            const auto source = box;
            if (selectable) {
                box.minimum.x = std::max(box.minimum.x, box.clip_minimum.x);
                box.minimum.y = std::max(box.minimum.y, box.clip_minimum.y);
                box.maximum.x = std::min(box.maximum.x, box.clip_maximum.x);
                box.maximum.y = std::min(box.maximum.y, box.clip_maximum.y);
            }
            if (box.maximum.x > box.minimum.x && box.maximum.y > box.minimum.y &&
                box.minimum.y >= box.clip_minimum.y && box.maximum.y <= box.clip_maximum.y) {
                io.AddMousePosEvent((box.minimum.x + box.maximum.x) / 2,
                                    (box.minimum.y + box.maximum.y) / 2);
                frame_();
                const auto current = test::ui_targets.find(key);
                // Shrinking content can clamp scroll during this hover frame.
                // Press only after the target has stopped moving under the pointer.
                if (current == test::ui_targets.end() ||
                    current->second.minimum.x != source.minimum.x ||
                    current->second.minimum.y != source.minimum.y ||
                    current->second.maximum.x != source.maximum.x ||
                    current->second.maximum.y != source.maximum.y)
                    continue;
                box.enabled = current->second.enabled;
                visible = true;
                break;
            }
            if (box.clip_maximum.x <= box.clip_minimum.x ||
                box.clip_maximum.y <= box.clip_minimum.y)
                continue; // A new popup may need its first layout before input.
            io.AddMousePosEvent(box.clip_minimum.x + 8, box.clip_maximum.y - 8);
            io.AddMouseWheelEvent(0, source.minimum.y < source.clip_minimum.y ? 1.f : -1.f);
            frame_();
        }
        if (!visible)
            throw std::runtime_error(std::string("Display control could not be reached: ") + key +
                                     " at scale " + std::to_string(io.DisplaySize.x));
        if (!box.enabled || box.minimum.x < box.clip_minimum.x ||
            box.maximum.x > box.clip_maximum.x || box.minimum.y < box.clip_minimum.y ||
            box.maximum.y > box.clip_maximum.y)
            throw std::runtime_error(
                std::string(key) + " clipped/disabled at scale " +
                std::to_string(io.DisplaySize.x) + ": " +
                nlohmann::json{
                    {"box", {box.minimum.x, box.minimum.y, box.maximum.x, box.maximum.y}},
                    {"clip",
                     {box.clip_minimum.x, box.clip_minimum.y, box.clip_maximum.x,
                      box.clip_maximum.y}},
                    {"enabled", box.enabled}}
                    .dump());
        io.AddMouseButtonEvent(0, true);
        frame_();
        io.AddMouseButtonEvent(0, false);
        frame_();
    }

  public:
    void type(const char* key, const char* value) {
        auto& io = ImGui::GetIO();

        click(key);
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_A, true);
        frame_();
        io.AddKeyEvent(ImGuiKey_A, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        io.AddInputCharactersUTF8(value);
        frame_();
        io.AddKeyEvent(ImGuiKey_Enter, true);
        frame_();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        frame_();
    }
};
} // namespace forge::test
