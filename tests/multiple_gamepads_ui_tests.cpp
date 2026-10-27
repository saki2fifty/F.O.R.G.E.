#include "editor/game_input.hpp"
#include <backends/imgui_impl_sdlrenderer3.h>
#include <filesystem>
#include <iostream>
#include <memory>
using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void widgets(const std::filesystem::path& output, float scale) {
    const std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
        SDL_CreateSurface(int(600 * scale), int(450 * scale), SDL_PIXELFORMAT_RGBA32),
        SDL_DestroySurface);
    check(bool(surface), SDL_GetError());
    const std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
        SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
    check(bool(renderer), SDL_GetError());
    const std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context(
        ImGui::CreateContext(), ImGui::DestroyContext);
    check(ImGui_ImplSDLRenderer3_Init(renderer.get()),
          "Native ImGui software renderer unavailable");
    struct Backend {
        ~Backend() { ImGui_ImplSDLRenderer3_Shutdown(); }
    } backend;
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {600 * scale, 450 * scale};
    io.DeltaTime = 1.f / 60;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigInputTrickleEventQueue = false;
    test::observe_ui = true;
    PlaySession play;
    GameInput input;
    input.pump(play, true);
    const auto initial = input.gamepads();
    check(initial.devices.size() == 4, "UI fixture did not discover four native virtual devices");
    {
        ui::style(scale);
        auto frame = [&] {
            test::ui_targets.clear();
            ImGui_ImplSDLRenderer3_NewFrame();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({10, 10});
            ImGui::SetNextWindowSize({460 * scale, 390 * scale});
            ImGui::Begin("Game", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
            input.controls(play);
            ImGui::End();
            ImGui::Render();
            SDL_SetRenderDrawColor(renderer.get(), 12, 14, 18, 255);
            SDL_RenderClear(renderer.get());
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer.get());
            SDL_RenderPresent(renderer.get());
        };
        auto click = [&](const std::string& name) {
            const auto target = test::ui_targets.at(name);
            const auto x = (target.minimum.x + target.maximum.x) / 2;
            const auto y = (target.minimum.y + target.maximum.y) / 2;
            check(target.enabled && x >= target.clip_minimum.x && x <= target.clip_maximum.x &&
                      y >= target.clip_minimum.y && y <= target.clip_maximum.y,
                  "Gamepad UI control clipped or disabled");
            io.AddMousePosEvent(x, y);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
        };
        auto key = [&](ImGuiKey value) {
            io.AddKeyEvent(value, true);
            frame();
            io.AddKeyEvent(value, false);
            frame();
        };
        frame();
        frame();
        click("button:Gamepads");
        frame();
        frame();
        frame();
        const auto slot_control = "gamepad:slot:" + initial.devices[0].device;
        check(test::ui_targets.contains(slot_control), "Assignment popup did not open");
        const auto* popup = ImGui::GetCurrentContext()->OpenPopupStack.back().Window;
        const auto last = test::ui_targets.at("gamepad:slot:" + initial.devices[3].device);
        check(popup && popup->Size.y <= last.maximum.y - popup->Pos.y + 40 * scale,
              "Assignment popup has excessive blank height");
        if (scale == 1) {
            std::filesystem::create_directories(output);
            check(SDL_SavePNG(surface.get(), (output / "gamepad-assignments.png").string().c_str()),
                  SDL_GetError());
        }
        click(slot_control);
        key(ImGuiKey_End);
        key(ImGuiKey_Enter);
        check(input.gamepads().devices[0].slot == 4 && input.gamepads().devices[3].slot == 0,
              "Keyboard assignment did not transfer occupied Player 4 slot");
        key(ImGuiKey_Escape);
        // Reset via the same owner for the next scale; no raw handle edits.
        auto devices = input.gamepads();
        input.assign_gamepad(play, {devices.revision, devices.devices[0].device, 1});
        devices = input.gamepads();
        input.assign_gamepad(play, {devices.revision, devices.devices[3].device, 4});
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need UI artifact folder");
        check(SDL_Init(SDL_INIT_GAMEPAD), SDL_GetError());
        struct SdlCleanup {
            ~SdlCleanup() { SDL_Quit(); }
        } cleanup;
        SDL_VirtualJoystickDesc device;
        SDL_INIT_INTERFACE(&device);
        device.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        device.naxes = 6;
        device.nbuttons = 15;
        device.axis_mask = 63;
        device.button_mask = 32767;
        device.name = "FORGE virtual controller";
        for (unsigned player = 0; player < 4; ++player)
            check(SDL_AttachVirtualJoystick(&device) != 0, SDL_GetError());
        for (float scale : {1.f, 1.5f, 2.f})
            widgets(std::filesystem::absolute(argv[1]), scale);
        std::cout << "Actual gamepad popup, keyboard assignment and three UI scales passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
