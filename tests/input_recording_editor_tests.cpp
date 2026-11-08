#include "document.hpp"
#include "imgui_software_capture.hpp"
#include "input_recording_controls.hpp"
#include <forge/game_settings.hpp>
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
void exercise(const std::filesystem::path& runtime, const std::filesystem::path& scratch, int width,
              bool sdk, const std::filesystem::path& image) {
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(scratch);
    SceneDocument::create_project(root, "Input recording");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument document(scene);
    document.open_project(root);
    auto settings = document.settings().document();
    settings["game"] = default_game_settings("org.forge.input-recording-test", "Input recording");
    settings["input"] = {
        {"version", 1},
        {"actions", Json::array({{{"id", "23000000-0000-4230-8230-000000000001"},
                                  {"name", "Recorded action"},
                                  {"kind", "digital"},
                                  {"bindings", Json::array({{{"control", "key.space"}}})}}})}};
    document.save_settings(settings);
    document.save();
    const auto original = scene.snapshot();
    const auto other_root = scratch / AssetId::generate().str();
    SceneDocument::create_project(other_root, "Other project");
    EngineContext other_engine;
    Scene other_scene(other_engine.world());
    SceneDocument other_document(other_scene);
    other_document.open_project(other_root);
    auto* visible_document = &document;
    test::ImGuiSoftwareCapture capture(width, 700);
    ui::style(1);
    test::observe_ui = true;
    PlaySession play;
    ui::InputRecordingControls controls;
    const auto start = [&] {
        play.configure(60, document.settings().input(), {0, -9.81, 0}, root, sdk, sdk);
        play.set_headless_audio(true);
        play.set_user_data_override(root / "UserData");
        play.start(path_utf8(runtime), scene.snapshot(), false, Json(), false, true);
    };
    const auto frame = [&] {
        play.pump();
        if (sdk && !play.sdk_candidate_envelope().is_null()) {
            play.submit_editor_observation(false, "KeyboardMouse");
            play.submit_sdk_candidate_ack(true, play.session(), play.sdk_candidate_ticket());
        }
        test::ui_targets.clear();
        capture.new_frame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({float(width), 700});
        ImGui::Begin("Game", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
        controls.draw(play, *visible_document, start);
        ImGui::End();
        capture.render();
        SDL_Delay(2);
    };
    const auto wait = [&](const auto& predicate) {
        const auto deadline = SDL_GetTicks() + 15000;
        do {
            frame();
            if (predicate())
                return;
        } while (SDL_GetTicks() < deadline);
        throw std::runtime_error("Native input recording timed out: " + play.status() + " | " +
                                 play.input_recording_error());
    };
    const auto click = [&](const std::string& target) {
        frame();
        const auto box = test::ui_targets.at("input-recording:" + target);
        check(box.enabled, "Native input recording target is disabled: " + target);
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent((box.minimum.x + box.maximum.x) / 2,
                            (box.minimum.y + box.maximum.y) / 2);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        frame();
    };
    frame();
    click("section");
    click("record");
    wait([&] {
        return play.ready() && !play.paused() &&
               play.input_status().value("recording", Json::object()).value("mode", "") ==
                   "recording";
    });
    play.input_event({"key.space", 1});
    wait([&] { return play.input_status().at("actions").at(0).value("presses", 0u) == 1; });
    play.input_event({"key.space", 0});
    wait([&] { return play.input_status().at("actions").at(0).value("releases", 0u) == 1; });
    click("finish");
    const auto file = root / "Recordings/input-recording.json";
    wait([&] { return std::filesystem::is_regular_file(file); });
    const auto recorded = read_input_recording(root, "Recordings/input-recording.json");
    check(recorded.at("frames").size() > 0, "Native controls saved no actual ticks");
    if (!image.empty()) {
        ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        frame();
        std::filesystem::create_directories(image.parent_path());
        capture.save(image);
    }
    play.stop();
    frame();
    check(test::ui_targets.contains("input-recording:save"),
          "Retrieved take was not retained after Stop");
    visible_document = &other_document;
    frame();
    check(!test::ui_targets.contains("input-recording:save"),
          "Another project exposed the old cached take");
    visible_document = &document;
    frame();
    click("replay");
    wait([&] {
        return play.ready() &&
               play.input_status().value("recording", Json::object()).value("mode", "") ==
                   "finished";
    });
    const auto& action = play.input_status().at("actions").at(0);
    check(action.at("presses") == 1 && action.at("releases") == 1,
          "Native replay did not drive the original input consumer");
    check(scene.snapshot() == original && !document.dirty(),
          "Input workflow changed authored history");
    play.stop();
    test::observe_ui = false;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3 || argc == 4, "Expected runtime, scratch and optional capture path");
        check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        for (bool sdk : {false, true})
            for (int width : {1180, 700, 400})
                exercise(std::filesystem::absolute(argv[1]), std::filesystem::absolute(argv[2]),
                         width, sdk,
                         argc == 4 && !sdk && width == 700 ? std::filesystem::absolute(argv[3])
                                                           : std::filesystem::path{});
        SDL_Quit();
        std::cout
            << "Native record/save/replay controls passed in both Play hosts at three widths\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
