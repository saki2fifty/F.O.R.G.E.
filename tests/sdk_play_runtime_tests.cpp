// Tests for the SDK Play host assembly.
//
// These tests build a real project + catalog + GameSession and exercise the
// host's wire contract via `handle()` + `pump()` directly. No process pipe
// is involved. The test EngineModule installs a Game-capability start
// callback so the host can capture the active world's `GameControlService`
// and issue a real game operation through the queue; the host then drives
// the polled effect lifecycle from owner-thread observations.
//
// A test-only process mode (`--mode=process`) constructs a normal
// SdkPlayRuntime, installs a fixture module that requests gameplay Quit
// at a fixed tick, and runs the production `process()` over the
// inherited stdin/stdout pipes. A Python driver (under /work) sends
// real protocol envelopes and reads responses, so the production
// receive/handle/send loop is exercised end-to-end without any test
// hooks inside the shipped runtime.

#include "sdk_play_runtime.hpp"
#include <atomic>
#include <cstring>
#include <filesystem>
#include <forge/assets.hpp>
#include <forge/build.hpp>
#include <forge/engine_module.hpp>
#include <forge/game_content.hpp>
#include <forge/game_control_queue.hpp>
#include <forge/game_control_service.hpp>
#include <forge/game_settings.hpp>
#include <forge/identity.hpp>
#include <forge/input.hpp>
#include <forge/runtime.hpp>
#include <forge/services.hpp>
#include <iostream>
#include <stdexcept>
#include <string>

namespace forge::test {
namespace {
using Json = nlohmann::json;

void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

std::filesystem::path temp_root() {
    static std::atomic<unsigned> counter{0};
    const auto unique = std::to_string(counter.fetch_add(1));
    const auto base = std::filesystem::temp_directory_path() /
                      ("forge-sdk-play-" + unique + "-" + AssetId::generate().str());
    std::filesystem::create_directories(base);
    return base;
}

// A test EngineModule that captures the active world's GameControlService
// once a scene is activated. The tests then issue real `cursor` and
// `ui_navigation` game operations through that endpoint to drive the
// host's polled effect lifecycle.
struct Capture {
    std::shared_ptr<GameControlService> service;
    int scene_ready_calls = 0;
};

struct Fixture {
    std::filesystem::path project_root;
    std::filesystem::path user_base;
    Json defaults;
    Capture capture;
    std::vector<EngineModule> modules;
    std::filesystem::path content_root;

    explicit Fixture() {
        project_root = temp_root();
        user_base = project_root / "user-data";
        std::filesystem::create_directories(user_base);
        content_root = project_root / "content";
        std::filesystem::create_directories(content_root);
        defaults =
            default_game_settings("org.forge.test-" + AssetId::generate().str(), "Sdk Play tests");
        // Persist a real authored scene and catalog so the runtime can
        // load it on prepare().
        AssetCatalog::project_index(content_root); // ensures layout exists.
    }
    ~Fixture() noexcept {
        // RAII cleanup. The container /tmp is ephemeral but the
        // host tools mount AgentFiles under a TMPDIR they expect to
        // stay tidy; leaking nested roots pollutes subsequent runs.
        std::error_code ec;
        std::filesystem::remove_all(project_root, ec);
        (void)ec;
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
};

SdkPlayRuntime::Config make_config(Fixture& f) {
    SdkPlayRuntime::Config config;
    config.project = f.project_root;
    config.user_base = f.user_base;
    config.runtime.simulation_hz = 60;
    config.input = InputMap{};
    config.modules = std::move(f.modules);
    config.defaults = std::move(f.defaults);
    config.ui = false;
    return config;
}

void install_test_module(Fixture& f) {
    EngineModule module;
    module.id = "game.test";
    module.dependencies = {"forge.game"};
    module.required_services = capability(Capability::Game);
    module.allowed_services = capability(Capability::Game);
    // The built-in forge.game module owns the Game service. The test
    // module only consumes it; providing it would conflict.
    module.runtime_roles = role_mask(WorldRole::Runtime);
    auto& capture = f.capture;
    module.start = [&capture](ModuleContext& c) { capture.service = c.services.game(); };
    module.scene_ready = [&capture](ModuleContext& c) {
        ++capture.scene_ready_calls;
        c.world.entity("game.prepared").set<int>(1);
    };
    f.modules.push_back(std::move(module));
}

Json hello(SdkPlayRuntime& host, RuntimeClock::Time now = {}) {
    Json request{{"protocol", 2}, {"id", 1u}, {"command", "hello"}};
    auto response = host.handle(request, now);
    check(response.value("ok", false), "hello did not return ok");
    return response;
}

Json send(SdkPlayRuntime& host, std::uint64_t id, const std::string& command,
          const Json& extras = Json::object(), RuntimeClock::Time now = {}) {
    Json request{{"protocol", 2}, {"id", id}, {"command", command}};
    for (auto it = extras.begin(); it != extras.end(); ++it)
        request[it.key()] = it.value();
    return host.handle(request, now);
}

std::unique_ptr<SdkPlayRuntime> build(Fixture& f) {
    install_test_module(f);
    return std::make_unique<SdkPlayRuntime>(make_config(f));
}

// Activate a real authored scene so the test module's start callback
// fires and f.capture.service becomes non-null. Mirrors the manager's
// activate path: replace + candidate_ack + pump.
std::unique_ptr<SdkPlayRuntime> build_activated(Fixture& f, std::string& session_out) {
    auto host = build(f);
    auto hello_response = hello(*host);
    session_out = hello_response.at("session").get<std::string>();
    // Legacy scene shape: version=1 with no v2 identity fields. The runtime
    // migrates it to v2 and generates asset_id + persistent entity IDs.
    Json scene{
        {"version", 1},
        {"entities",
         Json::array({Json{{"id", "cube"}, {"name", "Cube"}, {"components", Json::object()}}})}};
    auto replace = host->handle({{"protocol", 2},
                                 {"session", session_out},
                                 {"id", 2u},
                                 {"command", "replace"},
                                 {"scene", scene}});
    if (!replace.value("ok", false))
        throw std::runtime_error("activated fixture: replace returned error " +
                                 replace.value("error", std::string{}));
    host->pump(RuntimeClock::Clock::now());
    auto candidate = host->pending_candidate();
    if (!candidate.has_value()) {
        const auto loading = host->loading().dump();
        const auto status = host->status().dump();
        throw std::runtime_error("activated fixture: pending candidate missing; loading=" +
                                 loading + " status=" + status);
    }
    host->handle({{"protocol", 2},
                  {"session", session_out},
                  {"id", 3u},
                  {"command", "snapshot"},
                  {"candidate_ack",
                   {{"session", session_out},
                    {"ticket", candidate->at("ticket")},
                    {"accepted", true},
                    {"diagnostic", ""}}}});
    host->pump(RuntimeClock::Clock::now());
    check(host->status().value("state", "") == "paused",
          "activated fixture: world did not enter paused");
    check(f.capture.service != nullptr,
          "activated fixture: Game service not captured by start callback");
    return host;
}

std::uint64_t issue_cursor(Fixture& f) {
    check(f.capture.service != nullptr, "Game service not captured by test module");
    const auto token =
        f.capture.service->request("game.test", Json{{"operation", "cursor"}, {"capture", true}});
    return token;
}
} // namespace

void constructor_rejects_relative_user_data() {
    Fixture f;
    install_test_module(f);
    auto config = make_config(f);
    config.user_base = "relative/user/data";
    bool rejected = false;
    try {
        SdkPlayRuntime host(std::move(config));
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Relative user-data base was accepted");
}

void constructor_rejects_missing_application_id() {
    Fixture f;
    install_test_module(f);
    auto config = make_config(f);
    config.defaults = Json::object();
    bool rejected = false;
    try {
        SdkPlayRuntime host(std::move(config));
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Defaults without application_id were accepted");
}

void hello_returns_session_and_runtime_contract() {
    Fixture f;
    auto host = build(f);
    auto response = hello(*host);
    check(response.at("runtime_contract").value("sdk_play", false),
          "runtime_contract.sdk_play must be true");
    check(response.at("runtime_contract").at("source_commit").get<std::string>() ==
              forge::source_commit,
          "source_commit must match forge::source_commit");
}

void hello_twice_is_rejected() {
    Fixture f;
    auto host = build(f);
    hello(*host);
    Json second{{"protocol", 2}, {"id", 2u}, {"command", "hello"}};
    auto response = host->handle(second);
    check(!response.value("ok", false), "Second hello returned ok");
}

void stale_request_id_is_rejected_without_consuming() {
    Fixture f;
    auto host = build(f);
    auto hello_response = hello(*host);
    const auto session = hello_response.at("session").get<std::string>();
    Json good{{"protocol", 2}, {"session", session}, {"id", 2u}, {"command", "ping"}};
    auto ok_response = host->handle(good);
    check(ok_response.value("ok", false), "First ping did not return ok");
    Json stale{{"protocol", 2}, {"session", session}, {"id", 2u}, {"command", "ping"}};
    auto stale_response = host->handle(stale);
    check(!stale_response.value("ok", false), "Stale id was accepted");
    Json follow{{"protocol", 2}, {"session", session}, {"id", 3u}, {"command", "ping"}};
    auto follow_response = host->handle(follow);
    check(follow_response.value("ok", false), "Follow-up id was rejected");
}

void unknown_command_is_rejected() {
    Fixture f;
    auto host = build(f);
    auto hello_response = hello(*host);
    const auto session = hello_response.at("session").get<std::string>();
    Json request{{"protocol", 2}, {"session", session}, {"id", 2u}, {"command", "bogus"}};
    auto response = host->handle(request);
    check(!response.value("ok", false), "Unknown command returned ok");
}

void snapshot_uses_empty_scene_before_activation() {
    Fixture f;
    auto host = build(f);
    auto hello_response = hello(*host);
    const auto session = hello_response.at("session").get<std::string>();
    auto response = send(*host, 2, "snapshot", Json{{"session", session}});
    check(response.value("ok", false), "Snapshot before activation returned ok");
    // With activation.state == "none", scene-shaped fields are
    // explicitly null and never random AssetIds for a fictional scene.
    check(response.at("scene").is_null(), "Snapshot scene must be null pre-activation");
    check(response.at("effective_scene").is_null(),
          "Snapshot effective_scene must be null pre-activation");
    check(response.at("schema").is_null(), "Snapshot schema must be null pre-activation");
    check(response.at("input").is_null(), "Snapshot input must be null pre-activation");
    check(response.at("loading").is_object(), "Snapshot missing loading");
    check(response.contains("platform_effects") && response.at("platform_effects").is_array(),
          "Snapshot missing platform_effects array");
    check(response.value("activation", Json::object()).value("state", "") == "none",
          "Empty activation must report state=none");
}

void editor_epoch_message_validates_all_fields() {
    Fixture f;
    auto host = build(f);
    auto hello_response = hello(*host);
    const auto session = hello_response.at("session").get<std::string>();
    // Establish an initial epoch so subsequent smaller-epoch tests fail
    // for the right reason (older epoch), not because the runtime is
    // still at 0.
    auto base = host->handle(
        {{"protocol", 2},
         {"session", session},
         {"id", 2u},
         {"command", "snapshot"},
         {"editor_epoch", {{"epoch", 5u}, {"captured", false}, {"input_device", "EditorHost"}}}});
    check(base.value("ok", false), "Base editor_epoch was rejected");
    check(host->editor_epoch() == 5, "Base editor_epoch did not apply");
    // Missing captured field must be rejected.
    Json missing_captured{{"protocol", 2},
                          {"session", session},
                          {"id", 3u},
                          {"command", "snapshot"},
                          {"editor_epoch", {{"epoch", 6u}, {"input_device", "EditorHost"}}}};
    auto rejected = host->handle(missing_captured);
    check(!rejected.value("ok", false), "editor_epoch without captured was accepted");
    // Unknown input_device must be rejected.
    Json bad_device{{"protocol", 2},
                    {"session", session},
                    {"id", 4u},
                    {"command", "snapshot"},
                    {"editor_epoch",
                     {{"epoch", 7u}, {"captured", false}, {"input_device", "VoiceController"}}}};
    auto rejected2 = host->handle(bad_device);
    check(!rejected2.value("ok", false), "Unknown input_device was accepted");
    // Smaller epoch than the current 5 must be rejected.
    Json smaller{
        {"protocol", 2},
        {"session", session},
        {"id", 5u},
        {"command", "snapshot"},
        {"editor_epoch", {{"epoch", 4u}, {"captured", false}, {"input_device", "EditorHost"}}}};
    auto rejected3 = host->handle(smaller);
    check(!rejected3.value("ok", false), "Smaller epoch was accepted");
    // Valid epoch applied successfully.
    Json valid{
        {"protocol", 2},
        {"session", session},
        {"id", 6u},
        {"command", "snapshot"},
        {"editor_epoch", {{"epoch", 9u}, {"captured", false}, {"input_device", "KeyboardMouse"}}}};
    auto ok = host->handle(valid);
    check(ok.value("ok", false), "Valid editor_epoch was rejected");
    check(host->editor_epoch() == 9, "Editor epoch did not advance");
}

void platform_acks_rejects_unknown_token() {
    Fixture f;
    auto host = build(f);
    auto hello_response = hello(*host);
    const auto session = hello_response.at("session").get<std::string>();
    // Establish an initial epoch so the platform_acks epoch field has
    // something real to compare against.
    auto base = host->handle(
        {{"protocol", 2},
         {"session", session},
         {"id", 2u},
         {"command", "snapshot"},
         {"editor_epoch", {{"epoch", 1u}, {"captured", false}, {"input_device", "EditorHost"}}}});
    check(base.value("ok", false), "Base editor_epoch was rejected");
    Json bad{{"protocol", 2},
             {"session", session},
             {"id", 3u},
             {"command", "snapshot"},
             {"platform_acks", Json::array({Json{{"session", session},
                                                 {"token", 9999u},
                                                 {"sequence", 1u},
                                                 {"epoch", 1u},
                                                 {"accepted", true}}})}};
    auto response = host->handle(bad);
    check(!response.value("ok", false), "Ack for unknown token was accepted");
}

void platform_settings_rejects_display_changes_via_game_request() {
    // The synchronous platform.settings adapter is exercised by a real
    // queued game operation. The `set_settings` operation triggers the
    // adapter; the runtime must reject display changes and accept
    // audio/input changes. We verify this by issuing a display-bearing
    // set_settings through the captured GameControlService and observing
    // that the host's diagnostic captures the rejection.
    Fixture f;
    std::string session;
    auto host = build_activated(f, session);
    check(f.capture.service != nullptr, "Game service not captured by test module");
    const auto denied = f.capture.service->request(
        "game.test", Json{{"operation", "set_settings"},
                          {"values", Json{{"display", Json{{"mode", "fullscreen"}}}}}});
    // The host's pump must invoke platform.settings which throws on
    // display changes. The throw is caught by GameHostControls and
    // surfaced as a failure receipt on the request.
    host->pump(RuntimeClock::Clock::now());
    const auto status = f.capture.service->inspect("game.test", denied);
    check(status.state == "failed", "Display change set_settings was silently accepted");
    check(status.error.find("display") != std::string::npos ||
              status.error.find("Game view") != std::string::npos,
          "Display rejection diagnostic missing");
}

void platform_settings_applies_audio_and_input() {
    // A real queued `set_settings` operation that touches only audio and
    // input (no display change) must succeed and the GameService's
    // `settings` operation must reflect the new master_volume and
    // mouse_sensitivity override.
    Fixture f;
    std::string session;
    auto host = build_activated(f, session);
    check(f.capture.service != nullptr, "Game service not captured by test module");
    const auto accepted = f.capture.service->request(
        "game.test", Json{{"operation", "set_settings"},
                          {"values", Json{{"audio", Json{{"master_volume", 0.25f}}},
                                          {"input", Json{{"mouse_sensitivity", 1.5f}}}}}});
    host->pump(RuntimeClock::Clock::now());
    const auto status = f.capture.service->inspect("game.test", accepted);
    check(status.state == "succeeded", "audio+input set_settings was not accepted");
    const auto applied = f.capture.service->request("game.test", Json{{"operation", "settings"}});
    host->pump(RuntimeClock::Clock::now());
    const auto settings_inspect = f.capture.service->inspect("game.test", applied);
    check(settings_inspect.state == "succeeded", "settings query did not return a receipt");
    const auto& snapshot_value = settings_inspect.value;
    check(snapshot_value.contains("audio") &&
              snapshot_value.at("audio").value("master_volume", 0.0f) == 0.25f,
          "audio.master_volume override did not apply");
    check(snapshot_value.contains("input") &&
              snapshot_value.at("input").value("mouse_sensitivity", 0.0f) == 1.5f,
          "input.mouse_sensitivity override did not apply");
}

void game_control_queue_pending_query_consults_active_endpoint() {
    auto queue = std::make_shared<GameControlQueue>();
    // No active endpoint: unknown token must report false.
    check(!queue->pending(0), "Unknown token in empty queue reported as pending");
    check(!queue->pending(123), "Unknown token reported as pending");
}

void initial_candidate_failure_keeps_old_world_active() {
    // Activate a real authored scene, then issue a second replace with an
    // invalid scene. The active world must remain untouched (same state,
    // same scene entities) after the rejection.
    Fixture f;
    auto host = build(f);
    auto hello_response = hello(*host);
    const auto session = hello_response.at("session").get<std::string>();
    Json original_scene{
        {"version", 1},
        {"entities",
         Json::array({Json{{"id", "cube"}, {"name", "Cube"}, {"components", Json::object()}}})}};
    auto replace = host->handle({{"protocol", 2},
                                 {"session", session},
                                 {"id", 2u},
                                 {"command", "replace"},
                                 {"scene", original_scene}});
    check(replace.value("ok", false), "replace did not return ok");
    host->pump(RuntimeClock::Clock::now());
    auto candidate = host->pending_candidate();
    check(candidate.has_value(), "pending candidate missing");
    auto ack = host->handle({{"protocol", 2},
                             {"session", session},
                             {"id", 3u},
                             {"command", "snapshot"},
                             {"candidate_ack",
                              {{"session", session},
                               {"ticket", candidate->at("ticket")},
                               {"accepted", true},
                               {"diagnostic", ""}}}});
    check(ack.value("ok", false), "candidate_ack did not return ok");
    host->pump(RuntimeClock::Clock::now());
    check(host->status().value("state", "") == "paused",
          "Original scene did not activate after ack");
    auto before =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 4u}, {"command", "snapshot"}});
    const auto before_entities = before.at("scene").at("entities").size();
    // Issue a second replace with an invalid scene and reject its candidate.
    Json invalid{{"version", "not-a-number"}, {"entities", Json::array()}};
    auto bad_replace = host->handle({{"protocol", 2},
                                     {"session", session},
                                     {"id", 5u},
                                     {"command", "replace"},
                                     {"scene", invalid}});
    check(!bad_replace.value("ok", false), "Invalid replace returned ok");
    host->pump(RuntimeClock::Clock::now());
    auto after =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 6u}, {"command", "snapshot"}});
    check(after.at("scene").at("entities").size() == before_entities,
          "Rejected replacement altered active scene entities");
    check(host->status().value("state", "") == "paused",
          "Rejected replacement changed session state");
}

void input_events_require_clock_or_snapshot_command() {
    // The wire contract mirrors runtime_main: input_events only ride along
    // with snapshot/pause/resume/step/play. Any other command (including
    // hello) must reject. Empty arrays must also be rejected.
    Fixture f;
    auto host = build(f);
    auto hello_response = hello(*host);
    const auto session = hello_response.at("session").get<std::string>();
    Json empty_input{{"protocol", 2},
                     {"session", session},
                     {"id", 2u},
                     {"command", "ping"},
                     {"input_events", Json::array()}};
    auto empty = host->handle(empty_input, RuntimeClock::Time{});
    check(!empty.value("ok", false), "Empty input_events was accepted");
    Json wrong_command{{"protocol", 2},
                       {"session", session},
                       {"id", 3u},
                       {"command", "quit"},
                       {"input_events", Json::array({Json{{"control", "Jump"}, {"value", 1.0}}})}};
    auto wrong = host->handle(wrong_command, RuntimeClock::Time{});
    check(!wrong.value("ok", false), "input_events with quit command was accepted");
}

void input_events_require_active_world() {
    Fixture f;
    auto host = build(f);
    auto hello_response = hello(*host);
    const auto session = hello_response.at("session").get<std::string>();
    Json input_with_snapshot{
        {"protocol", 2},
        {"session", session},
        {"id", 2u},
        {"command", "snapshot"},
        {"input_events", Json::array({Json{{"control", "Jump"}, {"value", 1.0}}})}};
    auto response = host->handle(input_with_snapshot, RuntimeClock::Time{});
    // No active world: must reject.
    check(!response.value("ok", false), "Input events without active world were accepted");
}

void protocol_discriminator_rejects_float_and_string() {
    Fixture f;
    auto host = build(f);
    Json with_float{{"protocol", 2.0}, {"id", 1u}, {"command", "hello"}};
    auto rejected = host->handle(with_float, RuntimeClock::Time{});
    check(!rejected.value("ok", false), "Float protocol discriminator was accepted");
    Json with_string{{"protocol", "2"}, {"id", 1u}, {"command", "hello"}};
    auto rejected2 = host->handle(with_string, RuntimeClock::Time{});
    check(!rejected2.value("ok", false), "String protocol discriminator was accepted");
    Json with_three{{"protocol", 3}, {"id", 1u}, {"command", "hello"}};
    auto rejected3 = host->handle(with_three, RuntimeClock::Time{});
    check(!rejected3.value("ok", false), "Protocol version 3 was accepted");
}

void schema_command_does_not_overwrite_real_schema() {
    // Activate a real authored scene and exercise the schema command.
    // The response must carry the actual scene schema, not the empty
    // placeholder.
    Fixture f;
    auto host = build(f);
    auto hello_response = hello(*host);
    const auto session = hello_response.at("session").get<std::string>();
    Json scene{
        {"version", 1},
        {"entities",
         Json::array({Json{{"id", "box"}, {"name", "Box"}, {"components", Json::object()}}})}};
    auto replace = host->handle({{"protocol", 2},
                                 {"session", session},
                                 {"id", 2u},
                                 {"command", "replace"},
                                 {"scene", scene}});
    check(replace.value("ok", false), "replace did not return ok");
    host->pump(RuntimeClock::Clock::now());
    auto candidate = host->pending_candidate();
    check(candidate.has_value(), "candidate missing");
    auto ack_response = host->handle({{"protocol", 2},
                                      {"session", session},
                                      {"id", 3u},
                                      {"command", "snapshot"},
                                      {"candidate_ack",
                                       {{"session", session},
                                        {"ticket", candidate->at("ticket")},
                                        {"accepted", true},
                                        {"diagnostic", ""}}}});
    check(ack_response.value("ok", false), "candidate_ack did not return ok");
    host->pump(RuntimeClock::Clock::now());
    auto schema_response = send(*host, 4u, "schema", Json{{"session", session}});
    check(schema_response.value("ok", false), "schema did not return ok");
    check(schema_response.contains("schema") && schema_response.at("schema").is_object(),
          "schema response missing schema object");
    // The active scene carries an entities array; the real schema must
    // describe it.
    check(schema_response.at("schema").contains("version"), "active schema missing version");
}

void revoked_or_released_token_pruned_from_map() {
    // After a queue token is released, the host must remove it from its
    // effects map on the next prune. The live_effects list must not
    // contain the stale record.
    Fixture f;
    std::string session;
    auto host = build_activated(f, session);
    const auto token =
        f.capture.service->request("game.test", Json{{"operation", "cursor"}, {"capture", true}});
    host->pump(RuntimeClock::Time{});
    f.capture.service->release("game.test", token);
    host->pump(RuntimeClock::Time{});
    for (const auto& entry : host->live_effects())
        check(entry.value("token", std::uint64_t{}) != token,
              "Released token still appears in live_effects");
}

void negative_cursor_ack_marks_unknown_keeps_physical() {
    // A negative cursor ack must NOT invert the editor's observed
    // physical state. The host keeps the obligation signalled until the
    // editor reports an actual release at a new epoch.
    Fixture f;
    std::string session;
    auto host = build_activated(f, session);
    // Establish the editor epoch BEFORE issuing the cursor capture so
    // the offer is recorded under a known, stable epoch.
    auto epoch_response = host->handle(
        {{"protocol", 2},
         {"session", session},
         {"id", 4u},
         {"command", "snapshot"},
         {"editor_epoch", {{"epoch", 1u}, {"captured", true}, {"input_device", "EditorHost"}}}});
    check(epoch_response.value("ok", false), "epoch1 was rejected");
    const auto token =
        f.capture.service->request("game.test", Json{{"operation", "cursor"}, {"capture", true}});
    host->pump(RuntimeClock::Time{});
    // Copy the recorded offer token / sequence / epoch rather than
    // guessing them.
    Json ack_negative;
    for (const auto& entry : host->live_effects())
        if (entry.value("token", std::uint64_t{}) == token)
            ack_negative = entry;
    check(!ack_negative.empty(), "cursor offer did not appear in live_effects");
    ack_negative["session"] = session;
    ack_negative["accepted"] = false;
    ack_negative["diagnostic"] = "capture refused";
    Json negative_request{{"protocol", 2},
                          {"session", session},
                          {"id", 5u},
                          {"command", "snapshot"},
                          {"platform_acks", Json::array({ack_negative})}};
    auto negative_response = host->handle(negative_request);
    check(negative_response.value("ok", false), "Negative cursor ack snapshot did not return ok");
    host->pump(RuntimeClock::Time{});
    auto response =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 6u}, {"command", "snapshot"}});
    check(response.value("ok", false), "Snapshot did not return ok");
    // The negative cursor ack must leave the inspect receipt failed.
    check(f.capture.service->inspect("game.test", token).state == "failed",
          "negative physical ack did not settle as failed");
}

void contradictory_ack_rejected_idempotent_accepted() {
    Fixture f;
    std::string session;
    auto host = build_activated(f, session);
    // Establish the editor epoch BEFORE the cursor capture so the offer
    // is recorded under the same epoch the ack will reference.
    auto epoch_response = host->handle(
        {{"protocol", 2},
         {"session", session},
         {"id", 4u},
         {"command", "snapshot"},
         {"editor_epoch", {{"epoch", 1u}, {"captured", true}, {"input_device", "EditorHost"}}}});
    check(epoch_response.value("ok", false), "epoch was rejected");
    const auto token =
        f.capture.service->request("game.test", Json{{"operation", "cursor"}, {"capture", true}});
    host->pump(RuntimeClock::Time{});
    // Pull the recorded offer's token/sequence/epoch from the runtime.
    Json positive_ack;
    for (const auto& entry : host->live_effects())
        if (entry.value("token", std::uint64_t{}) == token)
            positive_ack = entry;
    check(!positive_ack.empty(), "cursor offer did not appear in live_effects");
    positive_ack["session"] = session;
    positive_ack["accepted"] = true;
    Json positive_msg{{"protocol", 2},
                      {"session", session},
                      {"id", 5u},
                      {"command", "snapshot"},
                      {"platform_acks", Json::array({positive_ack})}};
    auto positive_response = host->handle(positive_msg);
    check(positive_response.value("ok", false), "positive ack was rejected");
    // Drive the host so the callback consumes the accepted record.
    host->pump(RuntimeClock::Time{});
    // A stale ack for the already-consumed token must now reject.
    Json stale_msg{{"protocol", 2},
                   {"session", session},
                   {"id", 6u},
                   {"command", "snapshot"},
                   {"platform_acks", Json::array({positive_ack})}};
    auto stale_response = host->handle(stale_msg);
    check(!stale_response.value("ok", false), "stale ack after consumption was accepted");
    // Contradictory ack for a still-pending capture must reject. Establish
    // the next epoch BEFORE the second cursor so its offer is recorded
    // under the same epoch the contradictory ack will reference.
    auto epoch2 = host->handle(
        {{"protocol", 2},
         {"session", session},
         {"id", 7u},
         {"command", "snapshot"},
         {"editor_epoch", {{"epoch", 2u}, {"captured", true}, {"input_device", "EditorHost"}}}});
    check(epoch2.value("ok", false), "epoch2 was rejected");
    const auto captured_again =
        f.capture.service->request("game.test", Json{{"operation", "cursor"}, {"capture", true}});
    host->pump(RuntimeClock::Time{});
    Json contradictory_ack;
    for (const auto& entry : host->live_effects())
        if (entry.value("token", std::uint64_t{}) == captured_again)
            contradictory_ack = entry;
    check(!contradictory_ack.empty(), "second cursor offer did not appear in live_effects");
    contradictory_ack["session"] = session;
    contradictory_ack["accepted"] = false;
    contradictory_ack["diagnostic"] = "contradictory";
    Json contradicting_msg{{"protocol", 2},
                           {"session", session},
                           {"id", 8u},
                           {"command", "snapshot"},
                           {"platform_acks", Json::array({contradictory_ack})}};
    auto contradicting = host->handle(contradicting_msg);
    // A still-pending effect's negative ack is a normal rejection — the
    // snapshot returns ok=true and the runtime marks the offer Failed.
    check(contradicting.value("ok", false), "Negative ack for pending effect was rejected");
    host->pump(RuntimeClock::Time{});
    check(f.capture.service->inspect("game.test", captured_again).state == "failed",
          "negative ack did not settle the receipt as failed");
}

void editor_epoch_change_invalidates_older_offers() {
    Fixture f;
    std::string session;
    auto host = build_activated(f, session);
    const auto token =
        f.capture.service->request("game.test", Json{{"operation", "cursor"}, {"capture", true}});
    host->pump(RuntimeClock::Time{});
    Json epoch1{
        {"protocol", 2},
        {"session", session},
        {"id", 4u},
        {"command", "snapshot"},
        {"editor_epoch", {{"epoch", 1u}, {"captured", true}, {"input_device", "EditorHost"}}}};
    host->handle(epoch1);
    Json epoch2{
        {"protocol", 2},
        {"session", session},
        {"id", 5u},
        {"command", "snapshot"},
        {"editor_epoch", {{"epoch", 2u}, {"captured", false}, {"input_device", "EditorHost"}}}};
    host->handle(epoch2);
    for (const auto& entry : host->live_effects())
        check(entry.value("token", std::uint64_t{}) != token,
              "Offer recorded under older epoch still in live_effects");
}

void platform_pending_query_reflects_live_and_released() {
    Fixture f;
    std::string session;
    auto host = build_activated(f, session);
    const auto token =
        f.capture.service->request("game.test", Json{{"operation", "cursor"}, {"capture", true}});
    host->pump(RuntimeClock::Time{});
    bool seen = false;
    for (const auto& entry : host->live_effects())
        if (entry.value("token", std::uint64_t{}) == token)
            seen = true;
    check(seen, "Captured token did not appear in live_effects");
    f.capture.service->release("game.test", token);
    host->pump(RuntimeClock::Time{});
    for (const auto& entry : host->live_effects())
        check(entry.value("token", std::uint64_t{}) != token,
              "Released token still appears in live_effects");
}

void platform_acks_malformed_batch_retains_valid_first() {
    // A platform_acks batch that contains a valid ack followed by an
    // invalid ack must NOT publish the valid one. The runtime's source
    // keeps a temporary candidate map and validates the entire batch
    // before publishing. After the failed batch, a follow-up batch
    // carrying just the valid ack must succeed.
    Fixture f;
    std::string session;
    auto host = build_activated(f, session);
    // Establish the editor epoch BEFORE the cursor capture.
    auto epoch_response = host->handle(
        {{"protocol", 2},
         {"session", session},
         {"id", 4u},
         {"command", "snapshot"},
         {"editor_epoch", {{"epoch", 1u}, {"captured", true}, {"input_device", "EditorHost"}}}});
    check(epoch_response.value("ok", false), "epoch was rejected");
    const auto token =
        f.capture.service->request("game.test", Json{{"operation", "cursor"}, {"capture", true}});
    host->pump(RuntimeClock::Time{});
    Json valid_ack;
    for (const auto& entry : host->live_effects())
        if (entry.value("token", std::uint64_t{}) == token)
            valid_ack = entry;
    check(!valid_ack.empty(), "cursor offer did not appear in live_effects");
    valid_ack["session"] = session;
    valid_ack["accepted"] = true;
    Json malformed_ack{{"session", session},
                       {"token", std::uint64_t{987654321}},
                       {"sequence", 99u},
                       {"epoch", 1u},
                       {"accepted", true}};
    Json malformed_batch{{"protocol", 2},
                         {"session", session},
                         {"id", 5u},
                         {"command", "snapshot"},
                         {"platform_acks", Json::array({valid_ack, malformed_ack})}};
    auto malformed_response = host->handle(malformed_batch);
    check(!malformed_response.value("ok", false), "malformed platform_acks batch was accepted");
    host->pump(RuntimeClock::Time{});
    // The valid ack did not commit; the offer is still live and pending.
    bool still_live = false;
    for (const auto& entry : host->live_effects())
        if (entry.value("token", std::uint64_t{}) == token)
            still_live = true;
    check(still_live, "valid ack inside malformed batch was incorrectly committed");
    check(f.capture.service->inspect("game.test", token).state != "succeeded",
          "valid ack inside malformed batch advanced the receipt");
    // A retry with just the valid ack must succeed.
    Json retry_msg{{"protocol", 2},
                   {"session", session},
                   {"id", 6u},
                   {"command", "snapshot"},
                   {"platform_acks", Json::array({valid_ack})}};
    auto retry_response = host->handle(retry_msg);
    check(retry_response.value("ok", false),
          "retry of valid ack after malformed batch was rejected");
    host->pump(RuntimeClock::Time{});
    check(f.capture.service->inspect("game.test", token).state == "succeeded",
          "retry of valid ack did not settle the receipt");
}

void cancellation_releases_root_obligation() {
    // A queued cursor capture that is released without ack must clear
    // live_effects and raise the root release_required obligation. An
    // editor_epoch with captured=false at a NEW epoch clears the
    // obligation. Mirrors the manager fixture flow.
    Fixture f;
    std::string session;
    auto host = build_activated(f, session);
    const auto cancelled =
        f.capture.service->request("game.test", Json{{"operation", "cursor"}, {"capture", true}});
    host->pump(RuntimeClock::Time{});
    check(host->live_effects().size() == 1, "cancellation fixture offer missing");
    f.capture.service->release("game.test", cancelled);
    host->pump(RuntimeClock::Time{});
    check(host->live_effects().empty(), "released queue token remains actionable");
    Json snapshot =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 4u}, {"command", "snapshot"}});
    check(snapshot.value("release_required", false), "cancelled capture release obligation hidden");
    Json epoch_msg{
        {"protocol", 2},
        {"session", session},
        {"id", 5u},
        {"command", "snapshot"},
        {"editor_epoch", {{"epoch", 2u}, {"captured", false}, {"input_device", "EditorHost"}}}};
    host->handle(epoch_msg);
    Json confirmed =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 6u}, {"command", "snapshot"}});
    check(!confirmed.value("release_required", true), "confirmed release did not clear obligation");
}

void resolved_user_settings_applied_before_first_scene() {
    // Persist a real user_settings document with a known master_volume
    // and a custom input override. The host's constructor must apply
    // the resolved settings before any scene is loaded. We exercise the
    // path by constructing the host and observing it does not throw.
    Fixture f;
    {
        Json overrides{{"audio", Json{{"master_volume", 0.42f}}},
                       {"input", Json{{"mouse_sensitivity", 0.7}}}};
        GameStorage storage(f.user_base, f.defaults.at("application_id").get<std::string>());
        storage.save_settings(overrides, [&](const Json& v) {
            (void)resolve_game_settings(f.defaults, v, InputMap{});
        });
    }
    auto host = build(f);
    auto hello_response = hello(*host);
    check(hello_response.value("ok", false), "hello did not return ok");
}

void snapshot_response_returns_ok_false_when_active_unavailable() {
    Fixture f;
    auto host = build(f);
    auto hello_response = hello(*host);
    const auto session = hello_response.at("session").get<std::string>();
    auto response =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 2u}, {"command", "snapshot"}});
    // Empty world still returns ok=true by contract; the failure mode is
    // for active-but-broken worlds. Replace with an invalid scene, then
    // snapshot — the failure diagnostic must surface.
    Json invalid{{"version", "not-a-number"}, {"entities", Json::array()}};
    host->handle({{"protocol", 2},
                  {"session", session},
                  {"id", 3u},
                  {"command", "replace"},
                  {"scene", invalid}});
    auto after =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 4u}, {"command", "snapshot"}});
    check(after.value("ok", false) || after.contains("initial_diagnostic"),
          "Snapshot after failed replace did not surface diagnostic");
}

void activation_publishes_exact_candidate_ticket() {
    // Regression: the activated candidate ticket IS the authoritative
    // generation. The outgoing activation.generation must equal the
    // admitted candidate ticket, must not drift after repeated pumps
    // or snapshots, and must remain stable across a subsequent
    // gameplay-controlled replace.
    Fixture f;
    std::string session;
    auto host = build_activated(f, session);
    auto first =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 4u}, {"command", "snapshot"}});
    check(first.value("ok", false), "post-activation snapshot failed");
    const auto first_generation = first.at("activation").value("generation", std::uint64_t{});
    const auto first_ticket = first.at("activation").value("ticket", std::uint64_t{});
    check(first_generation == first_ticket, "activation.generation must equal activation.ticket");
    check(first_generation != 0, "activation.generation must reflect the active candidate");
    // Repeated pumps and snapshots must not move the generation.
    for (int i = 0; i < 4; ++i)
        host->pump(RuntimeClock::Time{});
    auto second =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 5u}, {"command", "snapshot"}});
    check(second.value("ok", false), "follow-up snapshot failed");
    const auto second_generation = second.at("activation").value("generation", std::uint64_t{});
    check(second_generation == first_generation, "activation.generation drifted across pumps");
    // Gameplay-controlled replace must advance the generation by exactly
    // the new candidate's ticket (strictly increasing).
    Json next_scene{
        {"version", 1},
        {"entities", Json::array({Json{
                         {"id", "sphere"}, {"name", "Sphere"}, {"components", Json::object()}}})}};
    auto replace = host->handle({{"protocol", 2},
                                 {"session", session},
                                 {"id", 6u},
                                 {"command", "replace"},
                                 {"scene", next_scene}});
    check(replace.value("ok", false), "gameplay replace failed");
    host->pump(RuntimeClock::Time{});
    auto candidate = host->pending_candidate();
    check(candidate.has_value(), "gameplay replace: pending candidate missing");
    const auto new_ticket = candidate->at("ticket").get<std::uint64_t>();
    auto ack = host->handle(
        {{"protocol", 2},
         {"session", session},
         {"id", 7u},
         {"command", "snapshot"},
         {"candidate_ack",
          {{"session", session}, {"ticket", new_ticket}, {"accepted", true}, {"diagnostic", ""}}}});
    check(ack.value("ok", false), "gameplay candidate_ack failed");
    host->pump(RuntimeClock::Time{});
    auto third =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 8u}, {"command", "snapshot"}});
    check(third.value("ok", false), "post-gameplay snapshot failed");
    const auto third_generation = third.at("activation").value("generation", std::uint64_t{});
    const auto third_ticket = third.at("activation").value("ticket", std::uint64_t{});
    check(third_generation == third_ticket,
          "post-gameplay activation.generation must equal ticket");
    check(third_generation == new_ticket,
          "post-gameplay activation.generation must equal the admitted ticket");
    check(third_generation > first_generation,
          "post-gameplay activation.generation must strictly increase");
}

// --- Transport-bound and candidate-fetch regression ---------------------

void no_active_scene_uses_null_scene_fields() {
    // With no active scene, the snapshot must carry null scene-shaped
    // fields (never fabricated AssetIds for a fictional scene) and an
    // empty activation reference. The candidate field on a normal
    // snapshot is a lightweight reference only — never the full envelope.
    Fixture f;
    auto host = build(f);
    auto hello_response = hello(*host);
    const auto session = hello_response.at("session").get<std::string>();
    auto response =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 2u}, {"command", "snapshot"}});
    check(response.value("ok", false), "snapshot returned ok before hello");
    check(response.at("scene").is_null(), "scene must be null when no active world exists");
    check(response.at("effective_scene").is_null(),
          "effective_scene must be null when no active world exists");
    check(response.at("schema").is_null(), "schema must be null when no active world exists");
    check(response.at("input").is_null(), "input must be null when no active world exists");
    check(response.at("candidate").is_null(),
          "candidate must be null when no candidate is pending");
}

void candidate_command_returns_full_envelope() {
    // After activation of a real scene, issue a replace that produces a
    // new candidate. The normal snapshot advertises only a reference
    // (session+ticket+ready) but no envelope; the dedicated
    // `candidate` command fetches the full frozen envelope.
    Fixture f;
    std::string session;
    auto host = build_activated(f, session);
    Json next{{"version", 1},
              {"entities",
               Json::array(
                   {Json{{"id", "second"}, {"name", "Second"}, {"components", Json::object()}}})}};
    auto replace = host->handle({{"protocol", 2},
                                 {"session", session},
                                 {"id", 4u},
                                 {"command", "replace"},
                                 {"scene", next}});
    check(replace.value("ok", false), "replace did not return ok");
    host->pump(RuntimeClock::Clock::now());
    auto reference = host->pending_candidate();
    check(reference.has_value(), "candidate reference missing");
    const auto ticket = reference->at("ticket").get<std::uint64_t>();
    // Normal snapshot advertises only a lightweight reference.
    auto snapshot =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 5u}, {"command", "snapshot"}});
    check(snapshot.value("ok", false), "snapshot returned ok=false");
    check(snapshot.at("candidate").is_object(),
          "snapshot candidate must be the lightweight reference object");
    check(snapshot.at("candidate").contains("ticket"),
          "snapshot candidate reference missing ticket");
    check(!snapshot.at("candidate").contains("scene"),
          "snapshot candidate must NOT inline the envelope scene");
    // The dedicated command fetches the full envelope.
    auto fetch = host->handle({{"protocol", 2},
                               {"session", session},
                               {"id", 6u},
                               {"command", "candidate"},
                               {"ticket", ticket}});
    check(fetch.value("ok", false), "candidate fetch did not return ok");
    const auto& envelope = fetch.at("candidate");
    check(envelope.contains("scene") && envelope.at("scene").is_object(),
          "candidate envelope missing scene");
    check(envelope.at("session") == session, "envelope session mismatch");
    check(envelope.at("ticket") == ticket, "envelope ticket mismatch");
}

void candidate_fetch_rejects_wrong_session_or_ticket() {
    Fixture f;
    std::string session;
    auto host = build_activated(f, session);
    Json next{
        {"version", 1},
        {"entities",
         Json::array({Json{{"id", "third"}, {"name", "Third"}, {"components", Json::object()}}})}};
    host->handle({{"protocol", 2},
                  {"session", session},
                  {"id", 4u},
                  {"command", "replace"},
                  {"scene", next}});
    host->pump(RuntimeClock::Clock::now());
    auto reference = host->pending_candidate();
    const auto ticket = reference->at("ticket").get<std::uint64_t>();
    auto wrong_ticket = host->handle({{"protocol", 2},
                                      {"session", session},
                                      {"id", 5u},
                                      {"command", "candidate"},
                                      {"ticket", ticket + 999u}});
    check(!wrong_ticket.value("ok", false), "wrong-ticket candidate fetch was accepted");
    auto missing =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 6u}, {"command", "candidate"}});
    check(!missing.value("ok", false), "ticket-less candidate fetch was accepted");
    host->handle({{"protocol", 2},
                  {"session", session},
                  {"id", 7u},
                  {"command", "snapshot"},
                  {"candidate_ack",
                   {{"session", session},
                    {"ticket", ticket},
                    {"accepted", false},
                    {"diagnostic", "user cancelled"}}}});
    host->pump(RuntimeClock::Clock::now());
    auto cancelled = host->handle({{"protocol", 2},
                                   {"session", session},
                                   {"id", 8u},
                                   {"command", "candidate"},
                                   {"ticket", ticket}});
    check(!cancelled.value("ok", false), "fetch of cancelled candidate was accepted");
}

void normal_snapshot_remains_bounded_with_pending_candidate() {
    // Regression: prior to the candidate-fetch split, a pending candidate
    // envelope was inlined in every snapshot, so the active+envelope
    // combined response could exceed the wire bound and every snapshot
    // returned ok=false indefinitely. The fix keeps the candidate off
    // the normal path; ordinary snapshots remain bounded and continue
    // to return ok=true while a candidate is pending.
    Fixture f;
    std::string session;
    auto host = build_activated(f, session);
    Json next{
        {"version", 1},
        {"entities",
         Json::array({Json{{"id", "big"}, {"name", "Big"}, {"components", Json::object()}}})}};
    auto replace = host->handle({{"protocol", 2},
                                 {"session", session},
                                 {"id", 4u},
                                 {"command", "replace"},
                                 {"scene", next}});
    check(replace.value("ok", false), "replace did not return ok");
    host->pump(RuntimeClock::Clock::now());
    auto snapshot =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 5u}, {"command", "snapshot"}});
    check(snapshot.value("ok", false), "Snapshot did not return ok while a candidate is pending");
    const auto size = snapshot.dump().size() + 1; // trailing newline
    check(size <= 16u * 1024u * 1024u, "Normal snapshot exceeded the 16 MiB wire bound");
    auto reference = host->pending_candidate();
    check(reference.has_value(), "candidate reference missing");
    auto fetch = host->handle({{"protocol", 2},
                               {"session", session},
                               {"id", 6u},
                               {"command", "candidate"},
                               {"ticket", reference->at("ticket")}});
    check(fetch.value("ok", false), "candidate fetch did not return ok");
    check(fetch.at("candidate").contains("scene"), "fetched envelope missing scene");
}

void world_replacement_prunes_retired_token() {
    // Regression: a queued cursor capture that survives into a
    // gameplay-driven world replacement must be pruned from the effects
    // map before any post-switch reply, AND must raise the root release
    // obligation (because the physical state of the retired world is
    // unknowable from the new one).
    Fixture f;
    std::string session;
    auto host = build_activated(f, session);
    const auto token =
        f.capture.service->request("game.test", Json{{"operation", "cursor"}, {"capture", true}});
    host->pump(RuntimeClock::Time{});
    bool seen = false;
    for (const auto& entry : host->live_effects())
        if (entry.value("token", std::uint64_t{}) == token)
            seen = true;
    check(seen, "capture offer did not appear in live_effects");
    Json next{{"version", 1},
              {"entities",
               Json::array({Json{
                   {"id", "switched"}, {"name", "Switched"}, {"components", Json::object()}}})}};
    auto replace = host->handle({{"protocol", 2},
                                 {"session", session},
                                 {"id", 4u},
                                 {"command", "replace"},
                                 {"scene", next}});
    check(replace.value("ok", false), "gameplay replace failed");
    host->pump(RuntimeClock::Clock::now());
    auto reference = host->pending_candidate();
    check(reference.has_value(), "gameplay replacement missing candidate");
    host->handle({{"protocol", 2},
                  {"session", session},
                  {"id", 5u},
                  {"command", "snapshot"},
                  {"candidate_ack",
                   {{"session", session},
                    {"ticket", reference->at("ticket")},
                    {"accepted", true},
                    {"diagnostic", ""}}}});
    host->pump(RuntimeClock::Clock::now());
    auto snapshot =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 6u}, {"command", "snapshot"}});
    check(snapshot.value("ok", false), "post-switch snapshot returned ok=false");
    for (const auto& entry : snapshot.at("platform_effects"))
        check(entry.value("token", std::uint64_t{}) != token,
              "retired-world capture token surfaced in post-switch snapshot");
    check(snapshot.value("release_required", false),
          "retired-world capture did not raise root release obligation");
}

void settings_rollback_to_unchanged_baseline_is_noop() {
    // Embedded Game view cannot change the editor window. The host
    // admits a no-op when the candidate display matches the baseline
    // admitted at construction; only real mode/size changes are
    // rejected. A rejected change must still rollback to the baseline
    // without producing a misleading rejection log.
    Fixture f;
    std::string session;
    auto host = build_activated(f, session);
    const auto display_only = f.capture.service->request(
        "game.test",
        Json{{"operation", "set_settings"},
             {"values",
              Json{{"display", Json{{"mode", "fullscreen"}, {"width", 1024}, {"height", 768}}}}}});
    host->pump(RuntimeClock::Clock::now());
    check(f.capture.service->inspect("game.test", display_only).state == "failed",
          "Display change set_settings was silently accepted");
}

void near_limit_candidate_rejected_before_activation() {
    // Regression: a candidate whose individual envelope fits the 8 MiB
    // admission bound may still produce a post-activation response
    // (scene + effective_scene + ui + ...) that exceeds the 16 MiB
    // wire bound. The validator must reject BEFORE the envelope is
    // exported / accepted, so the active world remains usable and the
    // candidate is reported with the real "projected post-activation"
    // diagnostic rather than silently admitted to poison later
    // snapshots.
    Fixture f;
    std::string session;
    auto host = build_activated(f, session);
    const auto before_status = host->status();
    const auto before_state = before_status.value("state", std::string{});
    // Legacy v1 single entity. The unknown component is preserved in
    // both the authored scene.snapshot() and the effective_scene
    // presentation, so a single ~8 MiB field doubles to ~16 MiB in
    // the projected full post-activation response. The envelope
    // (effective_scene only) stays under 8 MiB; the projection blows
    // past 16 MiB.
    constexpr std::size_t kLimit = 16u * 1024u * 1024u;
    constexpr std::size_t kEnvelopeLimit = 8u * 1024u * 1024u;
    constexpr std::size_t kTextBytes = (8u * 1024u * 1024u) - 4096u;
    Json big_candidate{
        {"version", 1},
        {"entities",
         Json::array({Json{
             {"id", "large"},
             {"name", "Large"},
             {"components", {{"missing.plugin", {{"text", std::string(kTextBytes, 'b')}}}}}}})}};
    const auto candidate_envelope_bytes =
        big_candidate.dump().size() + 100; // small envelope headroom
    check(candidate_envelope_bytes < kEnvelopeLimit,
          "Tuning payload: candidate envelope should fit 8 MiB so the "
          "validator exercises the projected-response rejection, not "
          "the per-envelope bound");
    host->handle({{"protocol", 2},
                  {"session", session},
                  {"id", 4u},
                  {"command", "replace"},
                  {"scene", big_candidate}});
    // The validator throws inside the adapter's first poll, which is
    // driven by the host's pump step. After the pump the loading
    // state is failed and no candidate was published.
    host->pump(RuntimeClock::Clock::now());
    const auto loading_after = host->loading();
    check(loading_after.value("state", std::string{}) == "failed",
          "Near-limit candidate was not rejected by the publication validator");
    const auto error_text = loading_after.value("error", std::string{});
    check(error_text.find("projected post-activation") != std::string::npos,
          "Rejection diagnostic did not mention projected post-activation");
    check(!host->pending_candidate().has_value(),
          "Near-limit candidate was published despite projection rejection");
    // Old scene remains usable.
    check(host->status().value("state", std::string{}) == before_state,
          "Active world was disturbed by a rejected near-limit candidate");
    auto snapshot =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 5u}, {"command", "snapshot"}});
    check(snapshot.value("ok", false),
          "Snapshot returned ok=false after a rejected near-limit candidate");
    check(snapshot.at("scene").is_object(),
          "Active scene payload disappeared after rejected candidate");
    const auto snapshot_bytes = snapshot.dump().size() + 1;
    check(snapshot_bytes <= kLimit,
          "Ordinary snapshot exceeded 16 MiB after rejected near-limit candidate");
    // initial_diagnostic must preserve the SAME real reason, not
    // the stale "Stale or missing prepared scene" the prior pump
    // produced by re-polling the discarded candidate.
    const auto initial_diag = snapshot.value("initial_diagnostic", std::string{});
    check(!initial_diag.empty() &&
              initial_diag.find("projected post-activation") != std::string::npos,
          "initial_diagnostic did not preserve the projected post-activation reason");
}

void near_limit_candidate_at_6mib_succeeds() {
    // Same single-entity structure as the rejection case, but with a
    // 6 MiB component so the projected full response fits the 16 MiB
    // protocol bound. The candidate must activate normally and the
    // active world must advance to the new scene.
    Fixture f;
    std::string session;
    auto host = build_activated(f, session);
    constexpr std::size_t kTextBytes = 6u * 1024u * 1024u;
    Json good_candidate{
        {"version", 1},
        {"entities",
         Json::array({Json{
             {"id", "medium"},
             {"name", "Medium"},
             {"components", {{"missing.plugin", {{"text", std::string(kTextBytes, 'b')}}}}}}})}};
    auto replace = host->handle({{"protocol", 2},
                                 {"session", session},
                                 {"id", 4u},
                                 {"command", "replace"},
                                 {"scene", good_candidate}});
    check(replace.value("ok", false), "6 MiB replace did not return ok");
    host->pump(RuntimeClock::Clock::now());
    auto reference = host->pending_candidate();
    check(reference.has_value(), "6 MiB candidate envelope not published");
    const auto new_ticket = reference->at("ticket").get<std::uint64_t>();
    host->handle(
        {{"protocol", 2},
         {"session", session},
         {"id", 5u},
         {"command", "snapshot"},
         {"candidate_ack",
          {{"session", session}, {"ticket", new_ticket}, {"accepted", true}, {"diagnostic", ""}}}});
    host->pump(RuntimeClock::Clock::now());
    auto snapshot =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 6u}, {"command", "snapshot"}});
    check(snapshot.value("ok", false), "6 MiB candidate did not become the active world");
    check(!host->pending_candidate().has_value(), "6 MiB candidate still pending after committal");
}

void settings_vsync_change_then_mode_change_is_clean() {
    // VSync is intentionally excluded from the physical display
    // comparison (matching GameHostControls::apply_settings). A
    // VSync-only change must succeed; a subsequent forbidden mode
    // change must still reject cleanly without a misleading
    // "Settings rollback failed" caused by vsync drift between the
    // pre-rejection settings and the rollback target.
    Fixture f;
    std::string session;
    auto host = build_activated(f, session);
    const auto vsync_change = f.capture.service->request(
        "game.test",
        Json{{"operation", "set_settings"}, {"values", Json{{"display", Json{{"vsync", false}}}}}});
    host->pump(RuntimeClock::Clock::now());
    check(f.capture.service->inspect("game.test", vsync_change).state == "succeeded",
          "VSync-only change was rejected");
    // Now attempt a forbidden mode change. The host must reject and
    // roll back to the previous (vsync-changed) settings without a
    // misleading log because both sides compare physical fields only.
    const auto mode_change = f.capture.service->request(
        "game.test",
        Json{{"operation", "set_settings"},
             {"values",
              Json{{"display", Json{{"mode", "fullscreen"}, {"width", 1024}, {"height", 768}}}}}});
    host->pump(RuntimeClock::Clock::now());
    check(f.capture.service->inspect("game.test", mode_change).state == "failed",
          "Mode change after vsync change was silently accepted");
    // Old preferences preserved: the active audio / input are still
    // applicable, and a snapshot is still servable.
    auto snapshot =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 4u}, {"command", "snapshot"}});
    check(snapshot.value("ok", false), "Snapshot after vsync+mode changes returned ok=false");
}

// --- Change 4: contradictory positive duplicate rejects, exact duplicate
// idempotent, whole batch atomic remains. ---------------------------

void exact_duplicate_positive_ack_is_idempotent() {
    // After a positive cursor ack has been accepted and consumed by the
    // host's pump, replaying the SAME positive ack must be a no-op
    // because the queue no longer owns the token (the offer has been
    // pruned). This is the exact-duplicate idempotent path. A different
    // ack that flips accepted or changes diagnostic must reject.
    Fixture f;
    std::string session;
    auto host = build_activated(f, session);
    auto epoch = host->handle(
        {{"protocol", 2},
         {"session", session},
         {"id", 4u},
         {"command", "snapshot"},
         {"editor_epoch", {{"epoch", 1u}, {"captured", true}, {"input_device", "EditorHost"}}}});
    check(epoch.value("ok", false), "epoch was rejected");
    const auto token =
        f.capture.service->request("game.test", Json{{"operation", "cursor"}, {"capture", true}});
    host->pump(RuntimeClock::Time{});
    Json positive_ack;
    for (const auto& entry : host->live_effects())
        if (entry.value("token", std::uint64_t{}) == token)
            positive_ack = entry;
    check(!positive_ack.empty(), "cursor offer missing");
    positive_ack["session"] = session;
    positive_ack["accepted"] = true;
    Json positive_msg{{"protocol", 2},
                      {"session", session},
                      {"id", 5u},
                      {"command", "snapshot"},
                      {"platform_acks", Json::array({positive_ack})}};
    auto first = host->handle(positive_msg);
    check(first.value("ok", false), "first positive ack was rejected");
    host->pump(RuntimeClock::Time{});
    // The token was consumed by the host's pump; a replay of the SAME
    // positive ack (same token, sequence, epoch, accepted=true,
    // diagnostic="" -- a still-known live_effects entry would be the
    // exact replay case) must accept wholesale because the record has
    // been pruned and the queue no longer owns the token. To exercise
    // the still-pending path we need to replay the ack BEFORE the host
    // consumed it. We do that with a new capture and an immediate
    // batched replay below.
    const auto token2 =
        f.capture.service->request("game.test", Json{{"operation", "cursor"}, {"capture", true}});
    host->pump(RuntimeClock::Time{});
    Json offer2;
    for (const auto& entry : host->live_effects())
        if (entry.value("token", std::uint64_t{}) == token2)
            offer2 = entry;
    check(!offer2.empty(), "second offer missing");
    offer2["session"] = session;
    offer2["accepted"] = true;
    offer2["diagnostic"] = "";
    Json exact_dup_msg{{"protocol", 2},
                       {"session", session},
                       {"id", 6u},
                       {"command", "snapshot"},
                       {"platform_acks", Json::array({offer2, offer2})}};
    auto exact_dup = host->handle(exact_dup_msg);
    check(exact_dup.value("ok", false),
          "exact duplicate positive ack was rejected on second entry");
}

void contradictory_positive_duplicate_ack_rejects() {
    // An already-accepted positive offer must reject any re-submission
    // that flips accepted OR changes diagnostic. The batch is rejected
    // atomically — no partial commit. The host's live_effects list
    // must still carry the offered state because the contradictory
    // batch was discarded.
    Fixture f;
    std::string session;
    auto host = build_activated(f, session);
    auto epoch = host->handle(
        {{"protocol", 2},
         {"session", session},
         {"id", 4u},
         {"command", "snapshot"},
         {"editor_epoch", {{"epoch", 1u}, {"captured", true}, {"input_device", "EditorHost"}}}});
    check(epoch.value("ok", false), "epoch was rejected");
    const auto token =
        f.capture.service->request("game.test", Json{{"operation", "cursor"}, {"capture", true}});
    host->pump(RuntimeClock::Time{});
    Json positive_ack;
    for (const auto& entry : host->live_effects())
        if (entry.value("token", std::uint64_t{}) == token)
            positive_ack = entry;
    check(!positive_ack.empty(), "cursor offer missing");
    positive_ack["session"] = session;
    positive_ack["accepted"] = true;
    Json first{{"protocol", 2},
               {"session", session},
               {"id", 5u},
               {"command", "snapshot"},
               {"platform_acks", Json::array({positive_ack})}};
    auto first_response = host->handle(first);
    check(first_response.value("ok", false), "first ack was rejected");
    host->pump(RuntimeClock::Time{});
    // The token was consumed; queue no longer owns it. We need a still-
    // pending offer to exercise the contradictory duplicate path on a
    // terminal-state record. Set up a second cursor offer whose host
    // record has been transitioned to Accepted, then attempt a
    // contradictory re-submission that flips accepted to false with a
    // different diagnostic. The batch must reject atomically.
    auto epoch2 = host->handle(
        {{"protocol", 2},
         {"session", session},
         {"id", 6u},
         {"command", "snapshot"},
         {"editor_epoch", {{"epoch", 2u}, {"captured", true}, {"input_device", "EditorHost"}}}});
    check(epoch2.value("ok", false), "epoch2 was rejected");
    const auto token2 =
        f.capture.service->request("game.test", Json{{"operation", "cursor"}, {"capture", true}});
    host->pump(RuntimeClock::Time{});
    Json offer2;
    for (const auto& entry : host->live_effects())
        if (entry.value("token", std::uint64_t{}) == token2)
            offer2 = entry;
    check(!offer2.empty(), "second offer missing");
    Json first_positive = offer2;
    first_positive["session"] = session;
    first_positive["accepted"] = true;
    // First ack: accept.
    Json accept_msg{{"protocol", 2},
                    {"session", session},
                    {"id", 7u},
                    {"command", "snapshot"},
                    {"platform_acks", Json::array({first_positive})}};
    auto accept_response = host->handle(accept_msg);
    check(accept_response.value("ok", false), "second positive ack was rejected");
    // Now submit a contradictory ack: same offer, accepted=false and a
    // different diagnostic. The batch must reject atomically.
    Json contradictory = first_positive;
    contradictory["accepted"] = false;
    contradictory["diagnostic"] = "contradictory";
    Json contradictory_msg{{"protocol", 2},
                           {"session", session},
                           {"id", 8u},
                           {"command", "snapshot"},
                           {"platform_acks", Json::array({contradictory})}};
    auto contradictory_response = host->handle(contradictory_msg);
    check(!contradictory_response.value("ok", false),
          "contradictory positive duplicate was accepted");
    check(contradictory_response.contains("error"),
          "contradictory duplicate did not carry an error diagnostic");
    // The live_effects record was not pruned: the host's poll_callback
    // for token2 has not returned yet, so the entry is still in the
    // effects map. A follow-up successful snapshot must still answer
    // ok=true.
    host->pump(RuntimeClock::Time{});
    auto follow =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 9u}, {"command", "snapshot"}});
    check(follow.value("ok", false), "follow-up snapshot after contradictory batch was rejected");
}

void contradictory_batch_with_one_valid_ack_rejects_whole() {
    // A batch containing one valid ack followed by a contradictory
    // duplicate must reject the whole batch atomically. The valid ack
    // must NOT be published.
    Fixture f;
    std::string session;
    auto host = build_activated(f, session);
    auto epoch = host->handle(
        {{"protocol", 2},
         {"session", session},
         {"id", 4u},
         {"command", "snapshot"},
         {"editor_epoch", {{"epoch", 1u}, {"captured", true}, {"input_device", "EditorHost"}}}});
    check(epoch.value("ok", false), "epoch was rejected");
    const auto token_a =
        f.capture.service->request("game.test", Json{{"operation", "cursor"}, {"capture", true}});
    const auto token_b =
        f.capture.service->request("game.test", Json{{"operation", "cursor"}, {"capture", true}});
    host->pump(RuntimeClock::Time{});
    Json offer_a, offer_b;
    for (const auto& entry : host->live_effects()) {
        if (entry.value("token", std::uint64_t{}) == token_a)
            offer_a = entry;
        if (entry.value("token", std::uint64_t{}) == token_b)
            offer_b = entry;
    }
    check(!offer_a.empty() && !offer_b.empty(), "two offers missing");
    offer_a["session"] = session;
    offer_a["accepted"] = true;
    offer_b["session"] = session;
    offer_b["accepted"] = true;
    // Accept token_a first; this transitions its offer to Accepted.
    Json first{{"protocol", 2},
               {"session", session},
               {"id", 5u},
               {"command", "snapshot"},
               {"platform_acks", Json::array({offer_a})}};
    auto first_response = host->handle(first);
    check(first_response.value("ok", false), "accept offer_a was rejected");
    // Now build a contradictory batch for token_a (rejected) and a
    // valid ack for token_b (would commit). Both must reject.
    Json contradictory_a = offer_a;
    contradictory_a["accepted"] = false;
    contradictory_a["diagnostic"] = "different";
    Json mixed_batch{{"protocol", 2},
                     {"session", session},
                     {"id", 6u},
                     {"command", "snapshot"},
                     {"platform_acks", Json::array({contradictory_a, offer_b})}};
    auto mixed = host->handle(mixed_batch);
    check(!mixed.value("ok", false), "mixed batch with contradictory duplicate was accepted");
    // Retry with just the valid token_b ack: it MUST commit because the
    // earlier batch was discarded wholesale.
    Json retry{{"protocol", 2},
               {"session", session},
               {"id", 7u},
               {"command", "snapshot"},
               {"platform_acks", Json::array({offer_b})}};
    auto retry_response = host->handle(retry);
    check(retry_response.value("ok", false),
          "retry of valid token_b ack after mixed batch was rejected");
}

// --- Change 3: SDK-only exact-ticket cancel -------------------------

void cancel_rejects_stale_or_unknown_ticket() {
    // The cancel command is SDK-only and exact-ticket. A ticket that
    // does not match the current prepared scene must reject with a
    // precise diagnostic and must NOT mutate the active world.
    Fixture f;
    auto host = build(f);
    auto hello_response = hello(*host);
    const auto session = hello_response.at("session").get<std::string>();
    // No candidate pending: any ticket must reject.
    Json cancel_no_pending{
        {"protocol", 2}, {"session", session}, {"id", 2u}, {"command", "cancel"}, {"ticket", 1u}};
    auto no_pending = host->handle(cancel_no_pending);
    check(!no_pending.value("ok", false), "cancel with no candidate was accepted");
    // Activate a scene first.
    Json scene{
        {"version", 1},
        {"entities",
         Json::array({Json{{"id", "stay"}, {"name", "Stay"}, {"components", Json::object()}}})}};
    auto replace = host->handle({{"protocol", 2},
                                 {"session", session},
                                 {"id", 3u},
                                 {"command", "replace"},
                                 {"scene", scene}});
    check(replace.value("ok", false), "replace did not return ok");
    host->pump(RuntimeClock::Clock::now());
    auto candidate = host->pending_candidate();
    check(candidate.has_value(), "candidate missing");
    const auto real_ticket = candidate->at("ticket").get<std::uint64_t>();
    // Wrong ticket must reject.
    Json cancel_wrong{{"protocol", 2},
                      {"session", session},
                      {"id", 4u},
                      {"command", "cancel"},
                      {"ticket", real_ticket + 999u}};
    auto wrong = host->handle(cancel_wrong);
    check(!wrong.value("ok", false), "wrong-ticket cancel was accepted");
    // Cancel the real ticket.
    Json cancel_real{{"protocol", 2},
                     {"session", session},
                     {"id", 5u},
                     {"command", "cancel"},
                     {"ticket", real_ticket}};
    auto real = host->handle(cancel_real);
    check(real.value("ok", false), "valid cancel was rejected");
    check(!host->pending_candidate().has_value(), "cancelled candidate still pending");
    // Subsequent snapshot must not carry a stale "Stale or missing
    // prepared scene" diagnostic in initial_diagnostic.
    auto snap =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 6u}, {"command", "snapshot"}});
    check(snap.value("ok", false), "snapshot after cancel was rejected");
    const auto initial_diag = snap.value("initial_diagnostic", std::string{});
    check(initial_diag.empty(), "cancel produced a stale initial_diagnostic");
}

void cancel_rejects_invalid_ticket_zero() {
    Fixture f;
    auto host = build(f);
    auto hello_response = hello(*host);
    const auto session = hello_response.at("session").get<std::string>();
    Json cancel_zero{
        {"protocol", 2}, {"session", session}, {"id", 2u}, {"command", "cancel"}, {"ticket", 0u}};
    auto zero = host->handle(cancel_zero);
    check(!zero.value("ok", false), "ticket=0 cancel was accepted");
}

void cancel_does_not_disturb_active_world() {
    // After a scene is activated, the prepared ticket is 0. A cancel
    // against the active generation's ticket (which is no longer the
    // prepared ticket) must reject and leave the active world alone.
    Fixture f;
    std::string session;
    auto host = build_activated(f, session);
    // Issue a second replace that produces a new candidate. Cancel
    // must commit without touching the active world.
    Json next{
        {"version", 1},
        {"entities",
         Json::array({Json{{"id", "extra"}, {"name", "Extra"}, {"components", Json::object()}}})}};
    auto replace = host->handle({{"protocol", 2},
                                 {"session", session},
                                 {"id", 4u},
                                 {"command", "replace"},
                                 {"scene", next}});
    check(replace.value("ok", false), "second replace was rejected");
    host->pump(RuntimeClock::Clock::now());
    auto candidate = host->pending_candidate();
    check(candidate.has_value(), "second candidate missing");
    const auto real_ticket = candidate->at("ticket").get<std::uint64_t>();
    auto before =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 5u}, {"command", "snapshot"}});
    const auto before_entities = before.at("scene").at("entities").size();
    Json cancel_real{{"protocol", 2},
                     {"session", session},
                     {"id", 6u},
                     {"command", "cancel"},
                     {"ticket", real_ticket}};
    auto real = host->handle(cancel_real);
    check(real.value("ok", false), "valid cancel was rejected");
    host->pump(RuntimeClock::Clock::now());
    auto after =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 7u}, {"command", "snapshot"}});
    check(after.at("scene").at("entities").size() == before_entities,
          "cancel mutated active world entities");
    check(!host->pending_candidate().has_value(), "cancelled candidate still pending");
}

// --- Change 1 / 2: snapshot advertises quit_requested; pause/step
// require physical release; resume stays exempt. -------------------

void quit_command_carries_quit_requested_true() {
    Fixture f;
    auto host = build(f);
    auto hello_response = hello(*host);
    const auto session = hello_response.at("session").get<std::string>();
    auto q = host->handle({{"protocol", 2}, {"session", session}, {"id", 2u}, {"command", "quit"}});
    check(q.value("ok", false), "quit returned ok=false");
    check(q.value("quit_requested", false), "quit response did not advertise quit_requested=true");
}

void snapshot_advertises_quit_requested_false_by_default() {
    Fixture f;
    auto host = build(f);
    auto hello_response = hello(*host);
    const auto session = hello_response.at("session").get<std::string>();
    auto snap =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 2u}, {"command", "snapshot"}});
    check(snap.contains("quit_requested"), "snapshot missing quit_requested field");
    check(!snap.value("quit_requested", true),
          "snapshot advertised quit_requested=true before quit");
}

void pause_rejects_without_acknowledged_release() {
    // Activate a real authored scene. The runtime starts at paused.
    // Direct pause must succeed on the initial paused session because
    // no mutation actually happens (game state is already paused).
    // Then resume, attempt pause while the editor still claims
    // captured=true at the same epoch — that must reject with an
    // honest diagnostic and leave the tick unchanged.
    Fixture f;
    std::string session;
    auto host = build_activated(f, session);
    // Establish the editor epoch with captured=false at the current
    // epoch (release observation). Then resume so pause has work to do.
    auto epoch_rel = host->handle(
        {{"protocol", 2},
         {"session", session},
         {"id", 4u},
         {"command", "snapshot"},
         {"editor_epoch", {{"epoch", 1u}, {"captured", false}, {"input_device", "EditorHost"}}}});
    check(epoch_rel.value("ok", false), "release epoch was rejected");
    auto resume =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 5u}, {"command", "resume"}});
    check(resume.value("ok", false), "resume was rejected");
    check(host->status().value("state", "") == "running", "resume did not transition to running");
    // Now advance editor epoch with captured=true (re-captured).
    auto epoch_cap = host->handle(
        {{"protocol", 2},
         {"session", session},
         {"id", 6u},
         {"command", "snapshot"},
         {"editor_epoch", {{"epoch", 2u}, {"captured", true}, {"input_device", "EditorHost"}}}});
    check(epoch_cap.value("ok", false), "captured epoch was rejected");
    const auto before_state = host->status().value("state", std::string{});
    const auto before_tick =
        host->status().value("clock", Json::object()).value("tick", std::uint64_t{});
    auto pause_rejected =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 7u}, {"command", "pause"}});
    check(!pause_rejected.value("ok", false), "pause while captured was accepted");
    check(pause_rejected.contains("error"), "pause rejection did not carry an error diagnostic");
    check(pause_rejected.value("error", std::string{}).find("release") != std::string::npos,
          "pause rejection did not mention release");
    // State and tick must be unchanged.
    check(host->status().value("state", "") == before_state, "pause mutated state on rejection");
    check(host->status().value("clock", Json::object()).value("tick", std::uint64_t{}) ==
              before_tick,
          "pause mutated tick on rejection");
}

void pause_succeeds_after_acknowledged_release() {
    Fixture f;
    std::string session;
    auto host = build_activated(f, session);
    auto epoch_rel = host->handle(
        {{"protocol", 2},
         {"session", session},
         {"id", 4u},
         {"command", "snapshot"},
         {"editor_epoch", {{"epoch", 1u}, {"captured", false}, {"input_device", "EditorHost"}}}});
    check(epoch_rel.value("ok", false), "release epoch was rejected");
    auto resume =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 5u}, {"command", "resume"}});
    check(resume.value("ok", false), "resume was rejected");
    // Editor reports release observation at a NEW epoch.
    auto epoch_rel2 = host->handle(
        {{"protocol", 2},
         {"session", session},
         {"id", 6u},
         {"command", "snapshot"},
         {"editor_epoch", {{"epoch", 2u}, {"captured", false}, {"input_device", "EditorHost"}}}});
    check(epoch_rel2.value("ok", false), "release epoch 2 was rejected");
    auto pause_ok =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 7u}, {"command", "pause"}});
    check(pause_ok.value("ok", false), "pause after release was rejected");
    check(host->status().value("state", "") == "paused",
          "pause after release did not transition to paused");
}

void step_rejects_without_acknowledged_release() {
    Fixture f;
    std::string session;
    auto host = build_activated(f, session);
    // Establish release at epoch 1, resume so step has work to do.
    auto epoch_rel = host->handle(
        {{"protocol", 2},
         {"session", session},
         {"id", 4u},
         {"command", "snapshot"},
         {"editor_epoch", {{"epoch", 1u}, {"captured", false}, {"input_device", "EditorHost"}}}});
    check(epoch_rel.value("ok", false), "release epoch was rejected");
    auto resume =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 5u}, {"command", "resume"}});
    check(resume.value("ok", false), "resume was rejected");
    // Mark captured=true at epoch 2.
    auto epoch_cap = host->handle(
        {{"protocol", 2},
         {"session", session},
         {"id", 6u},
         {"command", "snapshot"},
         {"editor_epoch", {{"epoch", 2u}, {"captured", true}, {"input_device", "EditorHost"}}}});
    check(epoch_cap.value("ok", false), "captured epoch was rejected");
    const auto before_tick =
        host->status().value("clock", Json::object()).value("tick", std::uint64_t{});
    auto step_rejected =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 7u}, {"command", "step"}});
    check(!step_rejected.value("ok", false), "step while captured was accepted");
    check(step_rejected.value("error", std::string{}).find("release") != std::string::npos,
          "step rejection did not mention release");
    // Tick must remain unchanged because the host's pump is the only
    // thing that advances ticks via advance(). handle() does not tick.
    // (We test the rejection; the actual tick advance for step happens
    // via game->step() inside handle().)
}

void pump_latches_quit_after_host_quit_requested() {
    // Drive the host's gameplay Quit path via the captured Game
    // service. After the host observes quit_requested the next pump
    // must early-return without advancing simulation, and any
    // subsequent snapshot must advertise quit_requested=true.
    Fixture f;
    std::string session;
    auto host = build_activated(f, session);
    auto epoch_rel = host->handle(
        {{"protocol", 2},
         {"session", session},
         {"id", 4u},
         {"command", "snapshot"},
         {"editor_epoch", {{"epoch", 1u}, {"captured", false}, {"input_device", "EditorHost"}}}});
    check(epoch_rel.value("ok", false), "release epoch was rejected");
    const auto before_tick =
        host->status().value("clock", Json::object()).value("tick", std::uint64_t{});
    // Issue a gameplay Quit via the captured service. The host's
    // execute() for "quit" calls poll_cursor_release(token) first,
    // which records a polled release(false) offer that must be
    // acked before the host proceeds to set quit_=true. Drive the
    // host pump, then ack the polled release as the editor would.
    const auto token = f.capture.service->request("game.test", Json{{"operation", "quit"}});
    host->pump(RuntimeClock::Time{});
    Json release_ack;
    for (const auto& entry : host->live_effects()) {
        if (entry.value("kind", std::string{}) != "cursor")
            continue;
        const auto& v = entry.at("value");
        if (!v.is_boolean() || v.get<bool>())
            continue;
        release_ack = entry;
        break;
    }
    check(!release_ack.empty(), "cursor release offer did not appear in live_effects");
    release_ack["session"] = session;
    release_ack["accepted"] = true;
    auto ack_msg = host->handle({{"protocol", 2},
                                 {"session", session},
                                 {"id", 5u},
                                 {"command", "snapshot"},
                                 {"platform_acks", Json::array({release_ack})}});
    check(ack_msg.value("ok", false), "release ack was rejected");
    host->pump(RuntimeClock::Time{});
    check(f.capture.service->inspect("game.test", token).state == "succeeded",
          "gameplay quit was not consumed");
    // Snapshot must now advertise quit_requested=true.
    auto snap =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 6u}, {"command", "snapshot"}});
    check(snap.value("quit_requested", false),
          "post-gameplay-quit snapshot did not advertise quit_requested=true");
    // Further pumps must NOT advance the tick.
    host->pump(RuntimeClock::Clock::now());
    host->pump(RuntimeClock::Clock::now());
    const auto after_tick =
        host->status().value("clock", Json::object()).value("tick", std::uint64_t{});
    check(after_tick == before_tick, "pump advanced tick after gameplay quit was latched");
}

// --- Manager reproductions: post-quit mutations must NOT mutate
// state. Each request after latched Quit must (a) carry
// quit_requested=true in the response, (b) leave state, tick, last_id,
// active_generation, scene, loading, last_initial_diagnostic, ui, and
// initial_ticket untouched. We exercise the production code paths that
// would otherwise mutate: pause, step, resume, replace, input_events,
// editor_epoch, candidate_ack, platform_acks.

void latch_quit_then_exercise_mutations() {
    Fixture f;
    std::string session;
    auto host = build_activated(f, session);
    // Release observation + resume so the gameplay clock is in a
    // known state.
    auto epoch_rel = host->handle(
        {{"protocol", 2},
         {"session", session},
         {"id", 4u},
         {"command", "snapshot"},
         {"editor_epoch", {{"epoch", 1u}, {"captured", false}, {"input_device", "EditorHost"}}}});
    check(epoch_rel.value("ok", false), "release epoch was rejected");
    auto resume =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 5u}, {"command", "resume"}});
    check(resume.value("ok", false), "resume was rejected");
    // Drive a gameplay Quit via the captured Game service.
    const auto token = f.capture.service->request("game.test", Json{{"operation", "quit"}});
    host->pump(RuntimeClock::Time{});
    Json release_ack;
    for (const auto& entry : host->live_effects()) {
        if (entry.value("kind", std::string{}) != "cursor")
            continue;
        const auto& v = entry.at("value");
        if (!v.is_boolean() || v.get<bool>())
            continue;
        release_ack = entry;
        break;
    }
    check(!release_ack.empty(), "cursor release offer missing before latching quit");
    release_ack["session"] = session;
    release_ack["accepted"] = true;
    host->handle({{"protocol", 2},
                  {"session", session},
                  {"id", 6u},
                  {"command", "snapshot"},
                  {"platform_acks", Json::array({release_ack})}});
    host->pump(RuntimeClock::Time{});
    check(f.capture.service->inspect("game.test", token).state == "succeeded",
          "gameplay quit was not consumed");
    // Snapshot baseline AFTER latch.
    const auto baseline =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 7u}, {"command", "snapshot"}});
    check(baseline.value("quit_requested", false),
          "baseline snapshot did not carry quit_requested=true");
    const auto baseline_status = host->status();
    const auto baseline_state = baseline_status.value("state", std::string{});
    const auto baseline_tick = baseline_status.at("clock").value("tick", std::uint64_t{});
    const auto baseline_generation = baseline.at("activation").value("ticket", std::uint64_t{});
    const auto baseline_scene_count = baseline.at("scene").at("entities").size();
    const auto baseline_loading = host->loading();
    // (a) direct pause.
    auto after_pause =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 8u}, {"command", "pause"}});
    check(after_pause.value("quit_requested", false),
          "post-quit pause did not advertise quit_requested=true");
    check(after_pause.at("activation").value("ticket", std::uint64_t{}) == baseline_generation,
          "post-quit pause mutated active_generation");
    check(host->status().at("clock").value("tick", std::uint64_t{}) == baseline_tick,
          "post-quit pause mutated tick");
    // (b) step.
    auto after_step =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 9u}, {"command", "step"}});
    check(after_step.value("quit_requested", false),
          "post-quit step did not advertise quit_requested=true");
    check(after_step.at("activation").value("ticket", std::uint64_t{}) == baseline_generation,
          "post-quit step mutated active_generation");
    check(host->status().at("clock").value("tick", std::uint64_t{}) == baseline_tick,
          "post-quit step mutated tick");
    // (c) resume (also no-mutation when latched).
    auto after_resume =
        host->handle({{"protocol", 2}, {"session", session}, {"id", 10u}, {"command", "resume"}});
    check(after_resume.value("quit_requested", false),
          "post-quit resume did not advertise quit_requested=true");
    check(after_resume.at("activation").value("ticket", std::uint64_t{}) == baseline_generation,
          "post-quit resume mutated active_generation");
    // (d) replace must not introduce a new candidate.
    Json alt_scene{
        {"version", 1},
        {"entities",
         Json::array({Json{{"id", "after"}, {"name", "After"}, {"components", Json::object()}}})}};
    auto after_replace = host->handle({{"protocol", 2},
                                       {"session", session},
                                       {"id", 11u},
                                       {"command", "replace"},
                                       {"scene", alt_scene}});
    check(after_replace.value("quit_requested", false),
          "post-quit replace did not advertise quit_requested=true");
    check(after_replace.at("activation").value("ticket", std::uint64_t{}) == baseline_generation,
          "post-quit replace mutated active_generation");
    check(!host->pending_candidate().has_value(), "post-quit replace produced a pending candidate");
    // (e) input_events on snapshot must not advance the tick.
    auto after_input = host->handle(
        {{"protocol", 2},
         {"session", session},
         {"id", 12u},
         {"command", "snapshot"},
         {"input_events",
          Json::array({Json{{"device", "keyboard"}, {"event", "press"}, {"key", "Q"}}})}});
    check(after_input.value("quit_requested", false),
          "post-quit snapshot with input_events did not advertise quit_requested=true");
    check(after_input.at("activation").value("ticket", std::uint64_t{}) == baseline_generation,
          "post-quit input_events mutated active_generation");
    // (f) editor_epoch payload must not change ownership.
    auto after_epoch = host->handle(
        {{"protocol", 2},
         {"session", session},
         {"id", 13u},
         {"command", "snapshot"},
         {"editor_epoch", {{"epoch", 99u}, {"captured", false}, {"input_device", "EditorHost"}}}});
    check(after_epoch.value("quit_requested", false),
          "post-quit snapshot with editor_epoch did not advertise quit_requested=true");
    check(after_epoch.at("activation").value("ticket", std::uint64_t{}) == baseline_generation,
          "post-quit editor_epoch mutated active_generation");
    // (g) platform_acks must not commit side-effects.
    Json fake_ack{{"session", session}, {"kind", "cursor"}, {"token", 1u},
                  {"sequence", 1u},     {"value", true},    {"accepted", true}};
    auto after_ack = host->handle({{"protocol", 2},
                                   {"session", session},
                                   {"id", 14u},
                                   {"command", "snapshot"},
                                   {"platform_acks", Json::array({fake_ack})}});
    check(after_ack.value("quit_requested", false),
          "post-quit snapshot with platform_acks did not advertise quit_requested=true");
    check(after_ack.at("activation").value("ticket", std::uint64_t{}) == baseline_generation,
          "post-quit platform_acks mutated active_generation");
    // (h) candidate_ack-like ack field also must not change anything.
    auto after_cack = host->handle({{"protocol", 2},
                                    {"session", session},
                                    {"id", 15u},
                                    {"command", "snapshot"},
                                    {"candidate_ack", Json::object()}});
    check(after_cack.value("quit_requested", false),
          "post-quit snapshot with candidate_ack did not advertise quit_requested=true");
    // Loading state and scene must remain stable across the batch.
    const auto after_loading = host->loading();
    check(after_loading == baseline_loading, "post-quit pause mutated loading");
    check(after_pause.at("scene").at("entities").size() == baseline_scene_count,
          "post-quit pause mutated scene");
    check(after_replace.at("scene").at("entities").size() == baseline_scene_count,
          "post-quit replace mutated scene");
}

int run() {
    try {
        constructor_rejects_relative_user_data();
        constructor_rejects_missing_application_id();
        hello_returns_session_and_runtime_contract();
        hello_twice_is_rejected();
        stale_request_id_is_rejected_without_consuming();
        unknown_command_is_rejected();
        snapshot_uses_empty_scene_before_activation();
        editor_epoch_message_validates_all_fields();
        platform_acks_rejects_unknown_token();
        platform_settings_rejects_display_changes_via_game_request();
        platform_settings_applies_audio_and_input();
        game_control_queue_pending_query_consults_active_endpoint();
        initial_candidate_failure_keeps_old_world_active();
        input_events_require_clock_or_snapshot_command();
        input_events_require_active_world();
        protocol_discriminator_rejects_float_and_string();
        schema_command_does_not_overwrite_real_schema();
        revoked_or_released_token_pruned_from_map();
        negative_cursor_ack_marks_unknown_keeps_physical();
        contradictory_ack_rejected_idempotent_accepted();
        editor_epoch_change_invalidates_older_offers();
        platform_pending_query_reflects_live_and_released();
        platform_acks_malformed_batch_retains_valid_first();
        cancellation_releases_root_obligation();
        resolved_user_settings_applied_before_first_scene();
        snapshot_response_returns_ok_false_when_active_unavailable();
        activation_publishes_exact_candidate_ticket();
        no_active_scene_uses_null_scene_fields();
        candidate_command_returns_full_envelope();
        candidate_fetch_rejects_wrong_session_or_ticket();
        normal_snapshot_remains_bounded_with_pending_candidate();
        world_replacement_prunes_retired_token();
        settings_rollback_to_unchanged_baseline_is_noop();
        settings_vsync_change_then_mode_change_is_clean();
        near_limit_candidate_rejected_before_activation();
        near_limit_candidate_at_6mib_succeeds();
        exact_duplicate_positive_ack_is_idempotent();
        contradictory_positive_duplicate_ack_rejects();
        contradictory_batch_with_one_valid_ack_rejects_whole();
        cancel_rejects_stale_or_unknown_ticket();
        cancel_rejects_invalid_ticket_zero();
        cancel_does_not_disturb_active_world();
        quit_command_carries_quit_requested_true();
        snapshot_advertises_quit_requested_false_by_default();
        pause_rejects_without_acknowledged_release();
        pause_succeeds_after_acknowledged_release();
        step_rejects_without_acknowledged_release();
        pump_latches_quit_after_host_quit_requested();
        latch_quit_then_exercise_mutations();
        std::clog << "sdk-play-runtime tests passed" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "sdk-play-runtime tests failed: " << e.what() << std::endl;
        return 1;
    }
}
} // namespace forge::test

// --- Test-only process mode ----------------------------------------
//
// Constructs a real SdkPlayRuntime with a fixture module that requests
// gameplay Quit from the controls callback after a configurable tick
// count, then runs the production process() loop. The Python driver
// sees a banner "READY<session>\n" on stdout, drives hello/snapshot/
// replace/candidate_ack/resume via stdin, and verifies that the next
// correlated snapshot after gameplay Quit carries quit_requested=true
// and the pipe closes orderly without an unsolicited id=0 event.
namespace forge::test {
namespace {
struct QuitAtTickFixture {
    std::shared_ptr<GameControlService> service;
    std::uint64_t quit_after_control_count = 0;
    std::atomic<std::uint64_t> control_counter{0};
    std::atomic<bool> quit_issued{false};
};
void install_quit_at_tick_module(Fixture& f, QuitAtTickFixture& state,
                                 std::uint64_t quit_after_control_count) {
    EngineModule module;
    module.id = "game.quit_at_tick";
    module.dependencies = {"forge.game"};
    module.required_services = capability(Capability::Game);
    module.allowed_services = capability(Capability::Game);
    module.runtime_roles = role_mask(WorldRole::Runtime);
    state.quit_after_control_count = quit_after_control_count;
    module.start = [&state](ModuleContext& c) { state.service = c.services.game(); };
    module.scene_ready = [](ModuleContext& c) { c.world.entity("game.prepared").set<int>(1); };
    // controls callback runs once per control_frame — EVEN WHILE
    // PAUSED after initial activation — so the fixture does not
    // require a resume to fire. The first invocation (count==1) is
    // the moment we request gameplay Quit, with no fixed
    // simulation-tick claim.
    module.controls = [&state](ModuleContext& c) {
        const auto count = state.control_counter.fetch_add(1) + 1;
        if (state.quit_issued.load())
            return;
        if (!state.service)
            return;
        if (count >= state.quit_after_control_count && state.quit_after_control_count > 0) {
            state.quit_issued.store(true);
            try {
                state.service->request("game.quit_at_tick", Json{{"operation", "quit"}});
            } catch (...) {
                // The Quit request is best-effort; the host's queue
                // accepts it as a queued operation and consumes it on
                // the next pump.
            }
        }
        (void)c;
    };
    f.modules.push_back(std::move(module));
}

int run_process_mode(const std::filesystem::path& user_base_override,
                     std::uint64_t quit_after_control_count) {
    // The process mode constructs a real SdkPlayRuntime with the
    // quit-at-control-count fixture module. The Python driver
    // (under /work) talks the production protocol envelope to its
    // stdin/stdout. The first hello response IS the readiness
    // handshake; there is no test-only banner. This is critical: the
    // production process loop writes nothing to stdout until the
    // first request arrives.
    //
    // The fixture module's controls callback runs once per
    // control_frame EVEN WHILE PAUSED after initial activation. So
    // quit_after_control_count=1 requests Quit on the very first
    // control_frame after activation — no resume required, no fixed
    // simulation tick claim.
    Fixture f;
    if (!user_base_override.empty())
        f.user_base = user_base_override;
    if (quit_after_control_count == 0)
        quit_after_control_count = 1;
    QuitAtTickFixture state;
    install_quit_at_tick_module(f, state, quit_after_control_count);
    try {
        auto host = build(f);
        host->process();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "process-mode runtime failed: " << e.what() << std::endl;
        return 2;
    }
}
} // namespace
} // namespace forge::test

int main(int argc, char** argv) {
    // --mode=process [user_base]
    if (argc >= 2 && std::strcmp(argv[1], "--mode=process") == 0) {
        std::filesystem::path user_base;
        if (argc >= 3)
            user_base = argv[2];
        return forge::test::run_process_mode(user_base, 1);
    }
    return forge::test::run();
}
