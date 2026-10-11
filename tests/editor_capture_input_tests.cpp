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
        input.queue(io, {std::numeric_limits<float>::quiet_NaN(), 50}, 0);
        frame(124, 5);
        require(!hover && !input.valid, "Nonfinite fixture pointer accepted");
        ImGui::DestroyContext();
        std::cout << "Platform overwrite/real widget click/static suppression/finite capture "
                     "bounds passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
