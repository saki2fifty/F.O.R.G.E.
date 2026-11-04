#pragma once
#include "editor/ui_probe.hpp"
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
    void click(const char* key) {
        auto& io = ImGui::GetIO();

        test::UiTarget box;
        bool visible = false;
        for (unsigned attempt = 0; attempt < 40; ++attempt) {
            frame_();
            box = test::ui_targets.at(key);
            if (box.minimum.y >= box.clip_minimum.y && box.maximum.y <= box.clip_maximum.y) {
                visible = true;
                break;
            }
            io.AddMousePosEvent(box.clip_minimum.x + 8, box.clip_maximum.y - 8);
            io.AddMouseWheelEvent(0, box.minimum.y < box.clip_minimum.y ? 1.f : -1.f);
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
        io.AddMousePosEvent((box.minimum.x + box.maximum.x) / 2,
                            (box.minimum.y + box.maximum.y) / 2);
        frame_();
        io.AddMouseButtonEvent(0, true);
        frame_();
        io.AddMouseButtonEvent(0, false);
        frame_();
    }
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
