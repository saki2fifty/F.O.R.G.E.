#include "capture_automation.hpp"
#include "game_recording_task.hpp"
#include "input_recording_files.hpp"
#include "input_recording_protocol.hpp"
#include <forge/game_settings.hpp>
#include <iostream>

using namespace forge;
namespace {
void check(bool value, const std::string& reason) {
    if (!value)
        throw std::runtime_error(reason);
}
void exercise(const std::filesystem::path& runtime, const std::filesystem::path& worker,
              const std::filesystem::path& root) {
    SceneDocument::create_project(root / "Game", "Recording workflow");
    std::filesystem::create_directory(root / "Output");
    EngineContext engine;
    Scene scene(engine.world());
    std::vector<std::string> recent;
    EditorFiles files(scene, nullptr, recent);
    files.start(root / "Game");
    auto settings = files.document.settings().document();
    settings["game"] = default_game_settings(
        "org.forge.recording-test-" + AssetId::generate().str(), "Recording test");
    files.document.save_settings(settings);
    PlaySession play;
    const auto ordinary_user_data = root / "OrdinaryUserData";
    play.set_user_data_override(ordinary_user_data);
    ui::ViewImageTask images(worker);
    std::string message;
    ui::GameRecordingHost host;
    host.start = [&](const std::filesystem::path& user_data) {
        play.configure(60, InputMap{}, {0, -9.81, 0}, files.document.project(), true, true);
        play.set_user_data_override(user_data);
        play.start(path_utf8(runtime), scene.snapshot(), false, Json(), true);
    };
    host.stop = [&] { play.stop(); };
    host.pixels = [&](unsigned) {
        const auto tick = play.timing().at("tick").get<unsigned>();
        Rgba8Pixels pixels{7, 5, std::vector<std::uint8_t>(140), true};
        for (unsigned i = 0; i < 35; ++i) {
            pixels.rgba[i * 4] = std::uint8_t(30 + tick * 35);
            pixels.rgba[i * 4 + 1] = std::uint8_t(i * 4);
            pixels.rgba[i * 4 + 2] = std::uint8_t(220 - tick * 30);
            pixels.rgba[i * 4 + 3] = 255;
        }
        ui::ViewImageTask::PixelInput input;
        input.source = {{"fixture", "detached game tick colors"}};
        input.load = [pixels = std::move(pixels)](ViewImageFormat) { return pixels; };
        return input;
    };
    const auto original = scene.snapshot();
    const auto revision = scene.revision();
    ui::GameRecordingRequest request;
    request.destination = root / "Output/Complete";
    request.fps = 30000. / 1001;
    request.frames = 3;
    request.output.width = 7;
    request.output.height = 5;
    RuntimeInput recorded_input;
    recorded_input.begin_input_recording(
        input_recording_source(scene.snapshot(), files.document.project()), 60);
    for (unsigned tick = 1; tick <= 6; ++tick)
        recorded_input.latch(tick);
    recorded_input.finish_input_recording();
    write_input_recording(files.document.project(), "Recordings/movie-input.json",
                          recorded_input.input_recording_document(), false);
    request.input_recording = "Recordings/movie-input.json";
    auto task =
        std::make_unique<ui::GameRecordingTask>(scene, files.document, play, worker, request, host);
    auto* recording = task.get();
    check(images.begin_work(std::move(task), files.document.project(), scene.asset_id(), message),
          "Recording workflow was not accepted by the image owner");
    std::uint64_t acknowledged = 0;
    const auto deadline = SDL_GetTicks() + 20000;
    while (images.busy() && SDL_GetTicks() < deadline) {
        play.pump();
        if (play.sdk_candidate_ticket() && play.sdk_candidate_ticket() != acknowledged &&
            !play.sdk_candidate_envelope().is_null()) {
            check(play.submit_editor_observation(false, "KeyboardMouse") &&
                      play.submit_sdk_candidate_ack(true, play.session(),
                                                    play.sdk_candidate_ticket()),
                  "Recording workflow candidate was not admitted");
            acknowledged = play.sdk_candidate_ticket();
        }
        images.poll_work(nullptr, message);
        SDL_Delay(1);
    }
    check(!images.busy() && recording->state().at("state") == "complete" &&
              play.user_data_override() == path_utf8(ordinary_user_data),
          "Recording workflow did not complete: " + recording->state().dump() + " | " +
              play.status());
    check(!play.active() && scene.snapshot() == original && scene.revision() == revision,
          "Recording retained Play or mutated authored Scene/history");
    const auto document =
        Json::parse(asset_storage::read(request.destination / "recording.json").value());
    check(document.at("input_recording").at("admitted") == true &&
              document.at("input_recording").at("ticks") == 6 &&
              Json::parse(
                  asset_storage::read(request.destination / "input-recording.json").value()) ==
                  recorded_input.input_recording_document(),
          "Input-driven movie did not retain the admitted take and receipt identity");
    check(document.at("completed_frames") == 3 && document.at("captured_png_frames") == 3 &&
              document.at("audio_frames") == 4805 && document.at("movie").at("audio") == true &&
              document.at("frames")[0].at("simulation_tick") == 0 &&
              document.at("frames")[1].at("simulation_tick") == 2 &&
              document.at("frames")[2].at("simulation_tick") == 4,
          "Recording receipt missed source timing, fixed ticks or complete audio");
    const auto status = CaptureAutomation::state(images, files);
    check(status.at("view") == "game_recording" &&
              status.at("game_recording").at("state") == "complete",
          "Capture discovery reported a game recording as another workflow");
    for (unsigned frame : {0u, 2u}) {
        check(images.begin_preview(status.at("artifact_operation"), frame, message),
              "Owned recording preview did not start");
        const auto preview_deadline = SDL_GetTicks() + 5000;
        while (images.busy() && SDL_GetTicks() < preview_deadline) {
            images.poll_files(message);
            SDL_Delay(1);
        }
        check(CaptureAutomation::state(images, files).at("preview_ready") == true &&
                  recording->preview_path(frame) ==
                      request.destination / request.plan().filename(frame),
              "Recording preview lost its owned frame list or image bytes");
    }
    auto run_case = [&](const char* name,
                        const std::function<void(ui::GameRecordingTask&)>& intervene) {
        auto next = request;
        next.destination = root / "Output" / name;
        auto work = std::make_unique<ui::GameRecordingTask>(scene, files.document, play, worker,
                                                            next, host);
        auto* active = work.get();
        check(
            images.begin_work(std::move(work), files.document.project(), scene.asset_id(), message),
            "Follow-up recording did not reuse the image owner");
        std::string accepted_launch;
        std::uint64_t accepted_ticket = 0;
        const auto end = SDL_GetTicks() + 20000;
        while (images.busy() && SDL_GetTicks() < end) {
            play.pump();
            if (play.launch_id() != accepted_launch) {
                accepted_launch = play.launch_id();
                accepted_ticket = 0;
            }
            if (play.sdk_candidate_ticket() && play.sdk_candidate_ticket() != accepted_ticket &&
                !play.sdk_candidate_envelope().is_null()) {
                check(play.submit_editor_observation(false, "KeyboardMouse") &&
                          play.submit_sdk_candidate_ack(true, play.session(),
                                                        play.sdk_candidate_ticket()),
                      "Follow-up recording candidate was not admitted");
                accepted_ticket = play.sdk_candidate_ticket();
            }
            images.poll_work(nullptr, message);
            intervene(*active);
            SDL_Delay(1);
        }
        check(!images.busy() && !play.active() &&
                  play.user_data_override() == path_utf8(ordinary_user_data),
              "Recording did not retire: " + active->state().dump());
        return Json::parse(asset_storage::read(next.destination / "recording.json").value());
    };
    bool cancelled = false;
    const auto cancelled_receipt = run_case("Cancelled", [&](ui::GameRecordingTask& active) {
        if (!cancelled && active.state().at("captured_png_frames") == 1) {
            check(images.cancel(message), "Active recording did not accept cancellation");
            cancelled = true;
        }
    });
    check(cancelled && cancelled_receipt.at("state") == "cancelled" &&
              cancelled_receipt.at("captured_png_frames") == 1 &&
              std::filesystem::exists(root / "Output/Cancelled/frame-000000.png") &&
              !std::filesystem::exists(root / "Output/Cancelled/game.avi"),
          "Cancellation lost completed PNGs or published a partial movie");
    bool damaged = false;
    const auto failed_receipt = run_case("FailedEncoder", [&](ui::GameRecordingTask& active) {
        const auto value = active.state();
        if (!damaged && value.at("state") == "pcm" && value.at("completed_frames") == 2) {
            asset_storage::replace(root / "Output/FailedEncoder/frame-000002.png", "invalid PNG");
            damaged = true;
        }
    });
    check(damaged && failed_receipt.at("state") == "failed" &&
              failed_receipt.at("completed_frames") == 3 &&
              std::filesystem::exists(root / "Output/FailedEncoder/audio.s16le") &&
              !std::filesystem::exists(root / "Output/FailedEncoder/game.avi") &&
              scene.snapshot() == original && scene.revision() == revision,
          "Encoder failure published a movie, lost retained PCM or changed authored data");
    const auto normal_start = host.start;
    host.start = [&](const std::filesystem::path& user_data) {
        play.set_user_data_override(user_data);
        throw std::runtime_error("Deliberate recording launch failure");
    };
    const auto start_failure = run_case("FailedStart", [](ui::GameRecordingTask&) {});
    host.start = normal_start;
    check(start_failure.at("state") == "failed" && start_failure.at("completed_frames") == 0 &&
              play.user_data_override() == path_utf8(ordinary_user_data),
          "Failed recording launch leaked its private user-data selection");
    for (const bool launch : {false, true}) {
        auto closing_request = request;
        closing_request.destination =
            root / "Output" / (launch ? "ClosedActive" : "ClosedPreparing");
        {
            ui::GameRecordingTask closing(scene, files.document, play, worker, closing_request,
                                          host);
            if (launch) {
                std::uint64_t accepted = 0;
                const auto end = SDL_GetTicks() + 20000;
                while (closing.state().at("completed_frames") == 0 && SDL_GetTicks() < end) {
                    play.pump();
                    if (play.sdk_candidate_ticket() && play.sdk_candidate_ticket() != accepted &&
                        !play.sdk_candidate_envelope().is_null()) {
                        check(play.submit_editor_observation(false, "KeyboardMouse") &&
                                  play.submit_sdk_candidate_ack(true, play.session(),
                                                                play.sdk_candidate_ticket()),
                              "Closing recording candidate was not admitted");
                        accepted = play.sdk_candidate_ticket();
                    }
                    closing.poll(nullptr);
                    SDL_Delay(1);
                }
                check(closing.state().at("completed_frames") == 1,
                      "Shutdown fixture did not reach an acknowledged frame");
            }
        }
        const auto receipt = Json::parse(
            asset_storage::read(closing_request.destination / "recording.json").value());
        check(!play.active() && play.user_data_override() == path_utf8(ordinary_user_data) &&
                  receipt.at("state") == "cancelled" && receipt.at("reason") == "Editor closed" &&
                  !receipt.at("started_at").get<std::string>().empty() &&
                  !receipt.at("finished_at").get<std::string>().empty() &&
                  receipt.at("completed_frames") == (launch ? 1 : 0) &&
                  !std::filesystem::exists(closing_request.destination / "game.avi") &&
                  scene.snapshot() == original && scene.revision() == revision,
              "Editor shutdown lost its receipt, leaked Play or modified the authored Scene");
    }
    bool changed = false;
    Json user_edit;
    const auto changed_receipt = run_case("SourceChanged", [&](ui::GameRecordingTask& active) {
        if (!changed && active.state().at("completed_frames") == 1) {
            (void)authoring_command(scene, "entity.create", {{"kind", 0}, {"name", "User edit"}});
            user_edit = scene.snapshot();
            changed = true;
        }
    });
    check(changed && changed_receipt.at("state") == "failed" && scene.snapshot() == user_edit &&
              !std::filesystem::exists(root / "Output/SourceChanged/game.avi"),
          "Source-change guard rolled back the user's edit or published a stale recording");
    std::cout << "Actual Play/process/image IO/PCM/movie workflow and owned frame previews PASS: "
              << request.destination << '\n';
}
} // namespace
int main(int argc, char** argv) {
    int result = 0;
    try {
        check(argc == 4, "Need runtime, worker and output paths");
        check(SDL_Init(SDL_INIT_EVENTS), SDL_GetError());
        const auto root = std::filesystem::absolute(argv[3]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        exercise(argv[1], argv[2], root);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        result = 1;
    }
    SDL_Quit();
    return result;
}
