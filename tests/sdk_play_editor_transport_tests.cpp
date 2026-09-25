// Real-runtime SDK Editor Play transport test. Drives a matching
// forge_runtime + matching native SDK probe through the same
// project fingerprint/modules fixture proven by editor_sdk_tests.cpp.

#include "../src/authored_inspection.hpp"
#include "play.hpp"
#include <SDL3/SDL.h>
#include <cstdint>
#include <filesystem>
#include <forge/game_settings.hpp>
#include <forge/native_sdk_identity.h>
#include <forge/project.hpp>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
void install_probe_sdk(const std::filesystem::path& root, const std::filesystem::path& library) {
    auto settings = forge::ProjectSettings::defaults("SDK editor transport test");
    settings["game"] = forge::default_game_settings("org.forge.sdk-editor-transport-" +
                                                        forge::AssetId::generate().str(),
                                                    "SDK editor transport test");
    settings["modules"] =
        forge::Json::array({{{"id", "project.sdk_probe"},
                             {"implementation", "1"},
                             {"sdk", "experimental-1"},
                             {"fingerprint", FORGE_NATIVE_SDK_FINGERPRINT},
                             {"library", library.filename().string()},
                             {"dependencies", {"forge.input", "forge.transforms"}}}});
    forge::atomic_write(root / "forge.project.json", settings.dump(2));
    std::filesystem::copy_file(library, root / library.filename(),
                               std::filesystem::copy_options::overwrite_existing);
}
// Cancellation subcases run a separate project with modules=[] so the
// native SDK probe never loads. The probe's controls() callback fires
// every control frame and queues pause requests without draining; the
// 400ms cancellation lifecycle overflows the bounded runtime queue and
// the runtime rejects every Pause with "SDK control request rejected",
// masking the cancel verdict we actually want to assert. Keeping the
// main SDK module/epoch/Step tests against the probe preserves the
// regression coverage; the cancellation subcases use only default
// game settings and an empty modules array so the runtime reaches
// the cancel verdict without a worker pumping commands.
void install_empty_project(const std::filesystem::path& root) {
    auto settings = forge::ProjectSettings::defaults("SDK editor cancel test");
    settings["game"] = forge::default_game_settings("org.forge.sdk-editor-cancel-" +
                                                        forge::AssetId::generate().str(),
                                                    "SDK editor cancel test");
    settings["modules"] = forge::Json::array();
    forge::atomic_write(root / "forge.project.json", settings.dump(2));
}
forge::Json make_scene() {
    return {{"version", 1},
            {"entities",
             forge::Json::array(
                 {{{"id", "cube"}, {"name", "Cube"}, {"components", forge::Json::object()}}})}};
}
// Run the documented lifecycle: configure SDK Play, wait for the
// candidate envelope, optionally let the ack ship through one
// pump, then submit a matching cancel. Returns the runtime status
// after a short observation window so each branch can assert.
void run_lifecycle(const std::string& runtime, const std::filesystem::path& project_root,
                   const std::filesystem::path& user_data, bool pump_before_cancel,
                   bool& out_active_after, bool& out_published_after, std::string& out_status) {
    forge::PlaySession play;
    play.configure(60, forge::InputMap{}, {0, -9.81, 0}, project_root, true, true);
    play.set_user_data_override(user_data);
    play.start(runtime, make_scene(), {}, true);
    auto wait = [&](auto done) {
        const auto deadline = SDL_GetTicks() + 10000;
        while (!done() && SDL_GetTicks() < deadline) {
            play.pump();
            SDL_Delay(1);
        }
    };
    wait([&] { return !play.active() || !play.sdk_candidate_envelope().is_null(); });
    require(play.active(), "Process failed: " + play.status() + " " + play.log());
    const auto ticket = play.sdk_candidate_ticket();
    require(ticket != 0, "No exact candidate ticket");
    require(play.submit_editor_observation(false, "KeyboardMouse"), "Initial observation rejected");
    require(play.submit_sdk_candidate_ack(true, play.session(), ticket), "Candidate ack rejected");
    if (pump_before_cancel)
        play.pump();
    require(play.submit_sdk_cancel_loading(ticket), "Cancel rejected");
    const auto deadline = SDL_GetTicks() + 400;
    while (play.active() && SDL_GetTicks() < deadline) {
        play.pump();
        SDL_Delay(1);
    }
    out_active_after = play.active();
    out_published_after = play.sdk_activation_active();
    out_status = play.status();
    play.stop();
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 3)
        return 2;
    const auto base = std::filesystem::current_path() /
                      ("sdk-editor-transport-" + forge::AssetId::generate().str());
    const auto root = base / "project";
    const auto cancel_root = base / "cancel-project";
    const auto user_data = base / "user-data";
    std::filesystem::create_directories(root);
    std::filesystem::create_directories(cancel_root);
    std::filesystem::create_directories(user_data);
    install_probe_sdk(root, argv[2]);
    install_empty_project(cancel_root);
    int result = 0;
    try {
        // Phase 1: candidate gate, strict monotonic epoch with
        // overflow, restart reset, foreign/wrong-ticket/wrong-session
        // rejection, activation pin, exact ack.
        {
            forge::PlaySession play;
            play.configure(60, forge::InputMap{}, {0, -9.81, 0}, root, true, true);
            play.set_user_data_override(user_data);
            auto wait = [&](auto done) {
                const auto deadline = SDL_GetTicks() + 10000;
                while (!done() && SDL_GetTicks() < deadline) {
                    play.pump();
                    SDL_Delay(1);
                }
                require(done(), "SDK transport timeout: " + play.status() + " " + play.log());
            };
            play.start(argv[1], make_scene(), {}, true);
            wait([&] { return !play.active() || !play.sdk_candidate_envelope().is_null(); });
            require(play.active(), "Process failed: " + play.status() + " " + play.log());
            require(!play.ready(), "Play reached ready before candidate admission");
            const auto ticket = play.sdk_candidate_ticket();
            require(ticket != 0, "No exact candidate ticket");
            require(!play.session().empty(), "Empty wire session");
            require(!play.submit_sdk_candidate_ack(true, std::string{"foreign"}, ticket),
                    "Foreign session ack was admitted");
            require(!play.submit_sdk_candidate_ack(true, play.session(), ticket + 1),
                    "Wrong-ticket ack was admitted");
            require(play.submit_sdk_editor_epoch(
                        {{"epoch", 1u}, {"captured", false}, {"input_device", "KeyboardMouse"}}),
                    "Valid epoch handoff was rejected");
            require(play.current_effective_epoch() == 1 && play.next_editor_epoch() == 2,
                    "Allocator did not advance the pending observation");
            require(!play.submit_sdk_editor_epoch(
                        {{"epoch", 1u}, {"captured", false}, {"input_device", "KeyboardMouse"}}),
                    "Duplicate queued epoch was admitted");
            require(!play.submit_sdk_editor_epoch(
                        {{"epoch", 2u}, {"captured", false}, {"input_device", "Touch"}}),
                    "Invented input device was admitted");
            require(play.submit_sdk_editor_epoch(
                        {{"epoch", 2u}, {"captured", false}, {"input_device", "EditorHost"}}),
                    "EditorHost epoch was rejected");
            require(play.current_effective_epoch() == 2 && play.next_editor_epoch() == 3,
                    "Allocator reused the pending epoch");
            require(play.submit_editor_observation(false, "KeyboardMouse"),
                    "Allocator could not queue an observation");
            require(play.current_effective_epoch() == 3 && play.next_editor_epoch() == 4,
                    "Allocator reused the pending epoch slot");
            require(play.submit_sdk_candidate_ack(true, play.session(), ticket),
                    "Matching candidate ack was rejected");
            require(!play.submit_sdk_candidate_ack(false, play.session(), ticket, "contradictory"),
                    "Pending verdict was overwritten by a contradictory ack");
            wait([&] { return !play.active() || play.ready(); });
            require(play.ready(), "Activation failed: " + play.status());
            require(play.sdk_activation_generation() == ticket,
                    "Generation differs from the prepared ticket");
            require(play.paused(), "Probe did not remain paused");
            play.step();
            wait([&] {
                return !play.active() || play.timing().value("tick", std::uint64_t{}) == 1;
            });
            require(play.active(), "Step closed the process");
            require(play.snapshot().at("entities").size() == 1, "Authored scene lost during Play");
            play.stop();
            // Restart: epoch counters MUST reset.
            play.start(argv[1], make_scene(), {}, true);
            wait([&] { return !play.active() || !play.sdk_candidate_envelope().is_null(); });
            require(play.active() && play.current_effective_epoch() == 0 &&
                        play.next_editor_epoch() == 1,
                    "Restart inherited stale epoch counters");
            require(play.submit_editor_observation(false, "KeyboardMouse"),
                    "Restart observation was rejected");
            require(
                play.submit_sdk_candidate_ack(true, play.session(), play.sdk_candidate_ticket()),
                "Restart candidate ack was rejected");
            wait([&] { return !play.active() || play.ready(); });
            require(play.ready(), "Restart did not reach ready");
            play.stop();
            // Overflow at the uint64 ceiling.
            play.start(argv[1], make_scene(), {}, true);
            wait([&] { return !play.active() || !play.sdk_candidate_envelope().is_null(); });
            require(play.submit_sdk_editor_epoch({{"epoch", UINT64_MAX},
                                                  {"captured", false},
                                                  {"input_device", "KeyboardMouse"}}),
                    "Final valid epoch was rejected");
            require(play.next_editor_epoch() == 0,
                    "Allocator did not advertise the overflow guard");
            require(!play.submit_editor_observation(false, "KeyboardMouse"),
                    "Epoch overflow was silently wrapped");
            play.stop();
        }
        // Phase 2: cancellation lifecycle. Two valid outcomes:
        //   a) Unsent cancel: cancel submitted before any ack
        //      shipping — initial Play must end cleanly with a
        //      "cancelled" status and no published world.
        //   b) Already-sent ack: pump once so the ack ships, then
        //      cancel arrives after the runtime may have published.
        //      The runtime rejects the cancel ticket; the editor
        //      keeps Play alive and either refreshes the snapshot or
        //      retains the already-observed publication. No process
        //      tear-down.
        // These subcases use cancel_root (modules=[]), not root
        // (probe SDK), so the probe's controls() callback never
        // queues an unbounded stream of pause requests and the
        // runtime's cancel verdict is the only verdict under test.
        {
            bool active_after = false, published_after = false;
            std::string status;
            run_lifecycle(argv[1], cancel_root, user_data,
                          /*pump_before_cancel=*/false, active_after, published_after, status);
            // Unsent cancel: no published world, clean cancel status.
            require(!active_after, "Unsent cancel did not end initial Play: " + status);
            require(!published_after, "Unsent cancel left a published world");
            require(status.find("cancelled") != std::string::npos,
                    "Unsent cancel lacked cancelled status: " + status);
        }
        {
            bool active_after = false, published_after = false;
            std::string status;
            run_lifecycle(argv[1], cancel_root, user_data,
                          /*pump_before_cancel=*/true, active_after, published_after, status);
            // Already-sent ack: the runtime rejects the cancel ticket
            // (no longer matches the prepared scene). The editor must
            // NOT tear down — it must keep the session alive and
            // surface a nonfatal "no longer applies" / "retained"
            // notice.
            require(active_after, "Sent cancel tore down published runtime: " + status);
            // A live but never-activated process must not pass old/new
            // world retention evidence: the previously published
            // activation must be retained after the rejected cancel.
            require(published_after, "Sent cancel cleared published world: " + status);
        }
        std::cout << "SDK Editor Play transport: candidate gate, monotonic "
                     "epochs, restart reset, overflow rejection, exact ack, "
                     "cancel lifecycle passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        result = 1;
    }
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    return result;
}
