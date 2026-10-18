#include "editor/actions.hpp"
#include "editor_input_workflow.hpp"
#include <iostream>
#include <stdexcept>
using namespace forge;
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void exercise(bool reject_start, bool reject_every_click) {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {400, 300};
    io.DeltaTime = 1.f / 60;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    auto workflow = test::EditorInputWorkflow::play_click_probe();
    test::play_action_invocations = 0;
    Json state{{"playing", false},
               {"control_ready", false},
               {"scene", {{"entities", Json::array()}}},
               {"play_status", "Stopped. Authored scene preserved."},
               {"play_log", "Log retained from an earlier successful Play"},
               {"problems", Json::array({{{"text", "Earlier unrelated graph error"}}})}};
    unsigned calls = 0, retries = 0;
    bool exhausted = false;
    ui::EditorActions actions;
    ui::EditorAction play;
    play.id = "play";
    play.label = "Play";
    play.execute = [&] {
        ++calls;
        if (reject_start)
            throw std::runtime_error("Native startup rejected");
        state["playing"] = true;
        state["control_ready"] = true;
    };
    actions.entries.push_back(std::move(play));
    auto draw = [&](unsigned frame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({400, 300});
        ImGui::Begin("Toolbar");
        // The target exists/enables on observation, then becomes disabled across
        // the first press/release. This reproduces an asynchronous action gate.
        actions.entries.front().available =
            reject_every_click ? !io.MouseDown[0] : (frame == 0 || frame > 10);
        try {
            actions.icon("play", ui::Icon::Play);
        } catch (const std::exception& e) {
            state["play_log"] = e.what();
        }
        ImGui::End();
        ImGui::Render();
    };
    draw(0);
    for (unsigned frame = 1; frame <= 120 && !workflow.done(); ++frame) {
        workflow.input();
        draw(frame);
        try {
            workflow.finish(
                state, [](const auto&) {},
                [&](const Json& report) {
                    for (const auto& entry : report.at("trace"))
                        if (entry.value("operation", "") == "retry unaccepted toolbar click")
                            retries = std::max(retries, entry.at("attempt").get<unsigned>() - 1);
                });
        } catch (const std::exception& e) {
            require(reject_every_click &&
                        std::string(e.what()).find("after three clicks") != std::string::npos,
                    "Unexpected Play input workflow failure");
            exhausted = true;
            break;
        }
        SDL_Delay(15);
    }
    require(calls == test::play_action_invocations, "Real action receipt lost a callback");
    if (reject_every_click)
        require(exhausted && calls == 0 && retries == 2, "Unaccepted clicks did not stop at three");
    else if (reject_start)
        require(!workflow.done() && calls == 1 && state.at("play_log") == "Native startup rejected",
                "A real startup failure was retried or reported as ready");
    else
        require(workflow.done() && calls == 1 && retries > 0,
                "Unaccepted Play click did not recover through the real toolbar");
    ImGui::DestroyContext();
}
} // namespace
int main() {
    try {
        require(SDL_Init(0), "SDL timer initialization failed");
        exercise(false, false);
        exercise(true, false);
        exercise(false, true);
        SDL_Quit();
        std::cout << "Real toolbar receipt/retry bound/startup failure preservation passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
