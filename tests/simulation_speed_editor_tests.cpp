#include "simulation_speed_controls.hpp"
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
void exercise(float scale, ImVec2 size, bool menu) {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = size;
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::style(scale);
    test::observe_ui = true;
    double accepted = 1, submitted = -1;
    bool ready = true, pending = false;
    unsigned requests = 0;
    auto frame = [&] {
        test::ui_targets.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::Begin("Speed", nullptr, ImGuiWindowFlags_NoResize);
        if (menu) {
            if (ImGui::Button("More"))
                ImGui::OpenPopup("actions");
            FORGE_UI_PROBE("speed:more");
            if (ImGui::BeginPopup("actions")) {
                simulation_speed_controls(
                    accepted, ready, pending,
                    [&](double v) {
                        submitted = v;
                        pending = true;
                        ++requests;
                        return true;
                    },
                    true);
                ImGui::EndPopup();
            }
        } else
            simulation_speed_controls(accepted, ready, pending, [&](double v) {
                submitted = v;
                pending = true;
                ++requests;
                return true;
            });
        ImGui::End();
        ImGui::Render();
    };
    auto point = [&](const char* key) {
        frame();
        frame();
        check(test::ui_targets.contains(key), std::string("Missing speed widget: ") + key);
        const auto box = test::ui_targets.at(key);
        check(box.enabled && box.minimum.x >= box.clip_minimum.x &&
                  box.maximum.x <= box.clip_maximum.x && box.minimum.y >= box.clip_minimum.y &&
                  box.maximum.y <= box.clip_maximum.y,
              std::string("Clipped speed widget: ") + key + " scale=" + std::to_string(scale));
        return ImVec2{(box.minimum.x + box.maximum.x) * .5f, (box.minimum.y + box.maximum.y) * .5f};
    };
    auto click = [&](const char* key) {
        const auto p = point(key);
        io.AddMousePosEvent(p.x, p.y);
        frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        frame();
    };
    constexpr double rates[]{0, .25, .5, 1, 2, 4};
    for (unsigned i = 0; i != 6; ++i) {
        if (menu)
            click("speed:more");
        // Native menus open on hover; combos open on a click.
        if (menu) {
            const auto p = point("simulation-speed:chooser");
            io.AddMousePosEvent(p.x, p.y);
            frame();
            frame();
        } else
            click("simulation-speed:chooser");
        const auto key = "simulation-speed:" + std::to_string(i);
        click(key.c_str());
        check(requests == i + 1 && pending && submitted == rates[i],
              "Speed widget did not submit exact preset");
        frame();
        if (!menu)
            check(!test::ui_targets.at("simulation-speed:chooser").enabled,
                  "Pending speed widget enabled");
        accepted = submitted;
        pending = false;
    }
    ready = false;
    frame();
    if (menu) {
        click("speed:more");
        frame();
    }
    check(!test::ui_targets.at("simulation-speed:chooser").enabled,
          "Stopped speed control enabled");
    ImGui::DestroyContext();
}
} // namespace
int main() {
    try {
        for (float scale : {1.f, 1.5f, 2.f})
            for (auto size : {ImVec2{420, 640}, ImVec2{1200, 900}})
                for (bool menu : {false, true})
                    exercise(scale, size, menu);
        std::cout
            << "Actual scaled full/compact speed chooser and overflow presets/pending passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
