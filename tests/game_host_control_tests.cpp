#include <forge/game_content.hpp>
#include <forge/game_host_controls.hpp>
#include <fstream>
#include <iostream>
#include <map>
#include <set>

using namespace forge;
namespace {
void check(bool value, const char* error) {
    if (!value)
        throw std::runtime_error(error);
}
void run(const std::filesystem::path& root) {
    const auto content = root / "content";
    std::filesystem::create_directories(content);
    WorldContext authoring;
    Scene scene(authoring);
    scene.replace({{"version", 1}, {"entities", Json::array()}});
    scene.save(content / "main.scene.json");
    AssetCatalog catalog(content);
    const auto asset = catalog.add_scene("main.scene.json").id;
    catalog.save(AssetCatalog::project_index(content));
    const auto action = ActionId::generate();
    InputMap input(
        {{"version", 1},
         {"actions", Json::array({{{"id", action},
                                   {"name", "Jump"},
                                   {"kind", "digital"},
                                   {"bindings", Json::array({{{"control", "key.space"}}})}}})}});
    const auto defaults = default_game_settings("org.forge.host-test", "Host test");
    auto queue = std::make_shared<GameControlQueue>();
    GameSessionConfig config;
    config.controls = queue;
    config.input = input;
    EngineModule module;
    module.id = "game.test";
    module.dependencies = {"forge.game"};
    module.allowed_services = capability(Capability::Game);
    module.runtime_roles = role_mask(WorldRole::Runtime);
    bool reject_migration = false;
    GameSaveSchema schema{2,
                          [](const GameSave& save) {
                              if (!save.data.at("counter").is_number_integer() ||
                                  save.data.at("counter") < 0)
                                  throw std::runtime_error("Invalid counter");
                          },
                          {}};
    schema.migrations[1] = [&](GameSave save) {
        if (reject_migration)
            throw std::runtime_error("Game migration rejected legacy state");
        save.data["migrated"] = true;
        return save;
    };
    module.start = [schema](ModuleContext& c) {
        c.services.game()->save_schema(c.id, schema, c.code);
    };
    module.scene_ready = [](ModuleContext& c) {
        check(!c.input && !c.controls, "Candidate initialization received gameplay input");
        c.world.entity("game.prepared").set<int>(1);
    };
    module.restore = [](ModuleContext& c, const Json& data) {
        check(c.world.lookup("game.prepared").get<int>() == 1,
              "Restore ran before scene initialization");
        c.world.entity("game.saved").set<int>(data.at("counter").get<int>());
    };
    config.modules.push_back(module);
    GameSession game(config);
    game.activate(game.prepare(load_game_scene(content, {asset})), RuntimeClock::Time{}, false);
    GameStorage storage(root / "users", "org.forge.host-test");
    unsigned applications = 0;
    int cursor_mode = 0;
    GamePlatformControls platform;
    platform.settings = [&](const Json& value) {
        ++applications;
        if (value.at("display").at("width") == 777)
            throw std::runtime_error("Rejected display fixture");
    };
    platform.cursor = [&cursor_mode](bool) {
        if (cursor_mode == 1)
            throw std::runtime_error("Cursor release refused by adapter");
        if (cursor_mode == 2)
            throw 42;
    };
    GameHostControls host(game, queue, storage, content, defaults, input, Json::object(), platform);
    auto request = [&](Json command) {
        auto service = game.active().engine.services().game();
        const auto token = service->request("game.test", command);
        host.pump(RuntimeClock::Time{});
        const auto result = service->inspect("game.test", token);
        service->release("game.test", token);
        return result;
    };
    check(request({{"operation", "resume"}}).state == "succeeded" &&
              game.status().at("state") == "running",
          "Queued resume did not reach session");
    check(request({{"operation", "pause"}}).state == "succeeded" &&
              game.status().at("state") == "paused",
          "Queued pause did not reach session");
    check(request(
              {{"operation", "set_settings"}, {"values", {{"input", {{"mouse_sensitivity", 2}}}}}})
                  .state == "succeeded",
          "Settings update failed");
    check(storage.load_settings([](const Json&) {}).at("input").at("mouse_sensitivity") == 2,
          "User settings not persisted");
    game.input({{"key.space", 1}});
    game.active().simulation.input().latch(0);
    check(
        request({{"operation", "set_settings"},
                 {"values", {{"display", {{"vsync", true}}}, {"audio", {{"master_volume", .5}}}}}})
                    .state == "succeeded" &&
            applications == 0 && game.active().simulation.input().latch(1).actions.at(action).held,
        "Volume/VSync change reset held controls or repositioned the window");
    check(request({{"operation", "set_settings"}, {"values", {{"display", {{"width", 777}}}}}})
                      .state == "failed" &&
              host.settings().at("display").at("width") == 1280 && applications == 2,
          "Failed setting was committed or platform was not restored");
    check(request({{"operation", "rebind_begin"}, {"action", action}, {"index", 0}}).state ==
              "succeeded",
          "Could not begin player rebind");
    game.input({{"key.j", 1}});
    host.pump(RuntimeClock::Time{});
    check(request({{"operation", "rebind_commit"}}).state == "succeeded", "Captured rebind failed");
    game.input({{"key.j", 1}});
    check(game.active().simulation.input().latch(1).actions.at(action).pressed,
          "Persisted rebind did not reach runtime");
    check(request({{"operation", "rebind_reset"}, {"action", action}}).state == "succeeded",
          "Restore defaults failed");
    game.input({{"key.space", 1}});
    check(game.active().simulation.input().latch(2).actions.at(action).pressed,
          "Default binding not restored");
    check(
        request(
            {{"operation", "save"}, {"slot", "one"}, {"scene", asset}, {"data", {{"counter", 43}}}})
                .state == "succeeded",
        "Save request did not reach storage");
    check(
        request(
            {{"operation", "save"}, {"slot", "one"}, {"scene", asset}, {"data", {{"counter", -1}}}})
                    .state == "failed" &&
            storage.load("one", schema).data.at("counter") == 43,
        "Invalid save replaced previous state");
    const auto slots = request({{"operation", "slots"}}).value;
    check(slots.size() == 1 && slots[0].at("valid") == true, "Slot metadata missing");
    check(request({{"operation", "prepare"}, {"asset", AssetId::generate()}}).state == "failed" &&
              game.active().scene.asset_id() == asset,
          "Failed scene request lost current scene");
    const auto candidate =
        request({{"operation", "prepare"}, {"asset", asset}, {"activate", false}});
    check(candidate.state == "succeeded" && game.active().scene.asset_id() == asset,
          "Prepared candidate replaced the active world before publication");
    check(request({{"operation", "cancel"}, {"ticket", candidate.value.at("ticket")}}).state ==
                  "succeeded" &&
              game.active().scene.asset_id() == asset && game.status().at("prepared_ticket") == 0,
          "Queued cancellation lost the current world or retained the candidate");
    check(request({{"operation", "rebind_begin"}, {"action", action}, {"index", 0}}).state ==
              "succeeded",
          "Could not begin cancelled rebind");
    check(request({{"operation", "rebind_reset"}}).state == "succeeded" &&
              !game.active().simulation.input().rebinding(),
          "Restoring defaults retained pending binding capture");
    check(request({{"operation", "rebind_begin"}, {"action", action}, {"index", 0}}).state ==
                  "succeeded" &&
              request({{"operation", "rebind_commit"}, {"clear", true}}).state == "succeeded" &&
              game.active().simulation.input().map().actions()[0].bindings.empty() &&
              !game.active().simulation.input().rebinding(),
          "Clear binding retained the binding or captured later input");
    request({{"operation", "rebind_reset"}});
    // Cursor release failure: std::runtime_error path.
    cursor_mode = 1;
    const auto active_world_ptr = &game.active();
    const auto active_scene_before = game.active().scene.asset_id();
    const auto prepared_marker_before =
        game.active().engine.world().world().lookup("game.prepared").get<int>();
    const auto save_one_before = [&] {
        std::ifstream file(storage.root() / "slot-one.json", std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), {});
    }();
    const auto fail_active =
        request({{"operation", "prepare"}, {"asset", asset}, {"activate", true}});
    check(fail_active.state == "failed" && fail_active.error == "Cursor release refused by adapter",
          "Failed prepare did not surface cursor release exception");
    check(&game.active() == active_world_ptr &&
              game.active().scene.asset_id() == active_scene_before &&
              game.active().engine.world().world().lookup("game.prepared").get<int>() ==
                  prepared_marker_before,
          "Failed activate=true prepare replaced the active world");
    check(game.status().at("prepared_ticket").get<std::uint64_t>() == 0,
          "Failed activate=true prepare left a pending candidate");
    const auto fail_inactive =
        request({{"operation", "prepare"}, {"asset", asset}, {"activate", false}});
    check(fail_inactive.state == "failed", "Failed activate=false prepare did not surface failure");
    check(&game.active() == active_world_ptr &&
              game.active().scene.asset_id() == active_scene_before,
          "Failed activate=false prepare replaced the active world");
    check(game.status().at("prepared_ticket").get<std::uint64_t>() == 0,
          "Failed activate=false prepare retained an unowned candidate");
    const auto fail_load = request({{"operation", "load"}, {"slot", "one"}, {"activate", true}});
    check(fail_load.state == "failed" && fail_load.error == "Cursor release refused by adapter",
          "Failed load did not surface cursor release exception");
    check(&game.active() == active_world_ptr &&
              game.active().engine.world().world().lookup("game.prepared").get<int>() ==
                  prepared_marker_before,
          "Failed activate=true load replaced the active world");
    check(game.status().at("prepared_ticket").get<std::uint64_t>() == 0,
          "Failed activate=true load left a pending candidate");
    {
        std::ifstream file(storage.root() / "slot-one.json", std::ios::binary);
        std::string bytes(std::istreambuf_iterator<char>(file), {});
        check(bytes == save_one_before, "Failed load rewrote the save file bytes");
    }
    host.pump(RuntimeClock::Time{});
    host.pump(RuntimeClock::Time{});
    check(&game.active() == active_world_ptr &&
              game.active().engine.world().world().lookup("game.prepared").get<int>() ==
                  prepared_marker_before &&
              game.status().at("prepared_ticket").get<std::uint64_t>() == 0,
          "Subsequent pumps activated or retained the failed candidate");
    // Catch-all path: non-std::exception.
    cursor_mode = 2;
    const auto nonstd = request({{"operation", "prepare"}, {"asset", asset}, {"activate", true}});
    check(nonstd.state == "failed" && nonstd.error == "Game operation callback failed",
          "Non-std cursor exception left an orphan receipt");
    check(&game.active() == active_world_ptr &&
              game.status().at("prepared_ticket").get<std::uint64_t>() == 0,
          "Non-std cursor exception replaced the world or retained the candidate");
    host.pump(RuntimeClock::Time{});
    // Successful path after the adapter is restored.
    cursor_mode = 0;
    auto success_service = game.active().engine.services().game();
    success_service->request("game.test",
                             {{"operation", "prepare"}, {"asset", asset}, {"activate", true}});
    host.pump(RuntimeClock::Time{});
    check(&game.active() != active_world_ptr && game.active().scene.asset_id() == asset &&
              game.status().at("prepared_ticket").get<std::uint64_t>() == 0,
          "Normal prepare did not publish a new world");
    auto success_service_2 = game.active().engine.services().game();
    success_service_2->request("game.test",
                               {{"operation", "load"}, {"slot", "one"}, {"activate", true}});
    host.pump(RuntimeClock::Time{});
    check(game.active().engine.world().world().lookup("game.saved").get<int>() == 43 &&
              game.status().at("prepared_ticket").get<std::uint64_t>() == 0,
          "Normal load did not restore saved state");
    {
        std::ofstream broken(storage.root() / "slot-broken.json");
        broken << "{\"payload\":";
    }
    check(request({{"operation", "load"}, {"slot", "broken"}}).state == "failed" &&
              game.active().scene.asset_id() == asset &&
              std::filesystem::file_size(storage.root() / "slot-broken.json") == 11,
          "Corrupt save request lost the world or modified the file");
    const GameSaveSchema legacy{1, schema.validate, {}};
    storage.save("legacy", {asset, {{"counter", 7}}}, legacy);
    auto legacy_bytes = [&] {
        std::ifstream file(storage.root() / "slot-legacy.json", std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), {});
    };
    const auto original_legacy = legacy_bytes();
    const auto migrated = request({{"operation", "load"}, {"slot", "legacy"}, {"activate", false}});
    check(migrated.state == "succeeded" && migrated.value.at("data").at("migrated") == true &&
              legacy_bytes() == original_legacy,
          "Queued save migration failed or rewrote the source slot");
    const auto multiple = request({{"operation", "slots"}}).value;
    check(multiple.size() == 3, "Queued enumeration omitted independent/corrupt slots");
    reject_migration = true;
    const auto failed = request({{"operation", "load"}, {"slot", "legacy"}});
    check(failed.state == "failed" && failed.error == "Game migration rejected legacy state" &&
              legacy_bytes() == original_legacy && game.active().scene.asset_id() == asset &&
              game.status().at("prepared_ticket") == 0,
          "Failed queued migration damaged the save or changed the current scene");
    reject_migration = false;
    check(request({{"operation", "erase"}, {"slot", "legacy"}}).state == "succeeded" &&
              !std::filesystem::exists(storage.root() / "slot-legacy.json") &&
              storage.load("one", schema).data.at("counter") == 43,
          "Deleting one slot affected another slot");
    auto old = game.active().engine.services().game();
    old->request("game.test", {{"operation", "load"}, {"slot", "one"}, {"run", false}});
    host.pump(RuntimeClock::Time{});
    check(game.active().scene.asset_id() == asset &&
              game.active().engine.world().world().lookup("game.saved").get<int>() == 43,
          "Save did not restore into prepared candidate");
    check(request({{"operation", "quit"}}).state == "succeeded" && host.quit_requested(),
          "Quit request did not reach host");
}
void polled_cursor_navigation(const std::filesystem::path& root) {
    const auto content = root / "content";
    std::filesystem::create_directories(content);
    WorldContext authoring;
    Scene scene(authoring);
    scene.replace({{"version", 1}, {"entities", Json::array()}});
    scene.save(content / "main.scene.json");
    AssetCatalog catalog(content);
    const auto asset = catalog.add_scene("main.scene.json").id;
    catalog.save(AssetCatalog::project_index(content));
    InputMap input({{"version", 1}, {"actions", Json::array()}});
    const auto defaults = default_game_settings("org.forge.host-poll-test", "Poll test");
    auto queue = std::make_shared<GameControlQueue>();
    GameSessionConfig config;
    config.controls = queue;
    config.input = input;
    EngineModule module;
    module.id = "game.test";
    module.dependencies = {"forge.game"};
    module.allowed_services = capability(Capability::Game);
    module.runtime_roles = role_mask(WorldRole::Runtime);
    module.start = [](ModuleContext&) {};
    config.modules.push_back(module);
    GameSession game(config);
    game.activate(game.prepare(load_game_scene(content, {asset})), RuntimeClock::Time{}, false);
    GameStorage storage(root / "users", "org.forge.host-poll-test");
    // Simulated backend with explicit ack/completion tokens and an explicit
    // forced release that mirrors what release_cursor() would do on a real
    // editor/native side. The host owns neither the queue nor the backend.
    struct Backend {
        bool captured = false;
        std::set<std::uint64_t> pending_cursor;
        std::set<std::uint64_t> pending_navigation;
        std::set<std::uint64_t> acknowledged;
        std::set<std::uint64_t> cancelled;
        std::map<std::uint64_t, std::string> nav_effects;
        int cursor_calls = 0;
        int navigation_calls = 0;
        int cursor_dispatches = 0;
        int navigation_dispatches = 0;
        int navigation_effects_recorded = 0;
        bool cursor_poll(std::uint64_t token, bool want) {
            ++cursor_calls;
            // Stale / cancelled tokens must throw, not falsely claim
            // completion. This mirrors the host adapter contract: the
            // backend owns idempotency and cancellation.
            if (cancelled.contains(token))
                throw std::runtime_error("cursor effect cancelled");
            if (pending_cursor.contains(token)) {
                // Subsequent poll of an already-pending token: only
                // complete (and apply the actual captured state) once
                // the test has explicitly acked the simulated backend.
                if (acknowledged.contains(token)) {
                    captured = want;
                    pending_cursor.erase(token);
                    return true;
                }
                return false;
            }
            // First poll: record the dispatch, simulate the effect having
            // been started in the native side, but do NOT yet mark the
            // host cursor as captured. The simulated effect lands only on
            // explicit ack.
            pending_cursor.insert(token);
            ++cursor_dispatches;
            return false;
        }
        bool navigation_poll(std::uint64_t token, const std::string& direction) {
            ++navigation_calls;
            if (cancelled.contains(token))
                throw std::runtime_error("navigation effect cancelled");
            if (pending_navigation.contains(token)) {
                if (acknowledged.contains(token)) {
                    pending_navigation.erase(token);
                    // Record the navigation effect exactly once, on the
                    // explicit ack that closes the polled request.
                    if (!nav_effects.contains(token)) {
                        nav_effects[token] = direction;
                        ++navigation_effects_recorded;
                    }
                    return true;
                }
                return false;
            }
            pending_navigation.insert(token);
            ++navigation_dispatches;
            // Do NOT record the effect on dispatch. The test asserts the
            // effect runs exactly once on ack.
            return false;
        }
        void ack_cursor(std::uint64_t token) { acknowledged.insert(token); }
        void ack_navigation(std::uint64_t token) { acknowledged.insert(token); }
        void force_release() {
            // Synchronous release invalidates every pending cursor capture;
            // it does not re-arm a delayed ack to recapture later.
            for (auto token : pending_cursor)
                cancelled.insert(token);
            pending_cursor.clear();
            captured = false;
        }
    } backend;
    GamePlatformControls platform;
    // Polled cursor adapter: delegates to the simulated backend. The adapter
    // is allowed to throw to signal failure (including stale/cancelled).
    platform.cursor_poll = [&](std::uint64_t token, bool want) {
        return backend.cursor_poll(token, want);
    };
    platform.navigation_poll = [&](std::uint64_t token, const std::string& direction) {
        return backend.navigation_poll(token, direction);
    };
    // release_cursor() routes through the synchronous adapter; for the polled
    // case we mirror the same effect by force-releasing the backend so the
    // pending capture is invalidated without a fresh dispatch.
    platform.cursor = [&](bool want) {
        if (!want)
            backend.force_release();
        else
            backend.captured = true;
    };
    GameHostControls host(game, queue, storage, content, defaults, input, Json::object(), platform);
    auto raw_service = game.active().engine.services().game();
    // Deferred cursor capture: pump once, the adapter is invoked but the
    // backend has not acknowledged yet, so the receipt stays queued and
    // cursor_captured is not set on the host.
    auto pending_token =
        raw_service->request("game.test", {{"operation", "cursor"}, {"capture", true}});
    host.pump(RuntimeClock::Time{});
    auto pending_status = raw_service->inspect("game.test", pending_token);
    check(pending_status.state == "queued", "Deferred cursor completed before backend ack");
    check(!host.cursor_captured(), "Cursor captured state set without confirmed adapter ack");
    check(!backend.captured, "Backend reported captured=true before explicit ack");
    // Pump again without ack: still queued, no extra dispatch, no captured.
    host.pump(RuntimeClock::Time{});
    check(raw_service->inspect("game.test", pending_token).state == "queued",
          "Deferred cursor did not remain queued without ack");
    check(!host.cursor_captured() && !backend.captured,
          "Idle second pump flipped captured state without ack");
    // Ack the same token from the test side; the next pump must complete
    // the receipt, set the host captured flag, and apply the backend
    // captured state. No second dispatch.
    backend.ack_cursor(pending_token);
    host.pump(RuntimeClock::Time{});
    auto acked_status = raw_service->inspect("game.test", pending_token);
    check(acked_status.state == "succeeded", "Cursor did not complete after backend ack");
    check(host.cursor_captured(), "Host cursor_captured() not set after backend ack");
    check(backend.captured, "Backend captured flag not applied on explicit ack");
    raw_service->release("game.test", pending_token);
    // Pumping after completion must not redispatch the same effect.
    int dispatches_before = backend.cursor_dispatches;
    host.pump(RuntimeClock::Time{});
    check(backend.cursor_dispatches == dispatches_before,
          "Idle pump produced an extra physical dispatch");
    // Deferred navigation: same pattern, effect recorded only on ack.
    auto nav_token =
        raw_service->request("game.test", {{"operation", "ui_navigation"}, {"direction", "next"}});
    host.pump(RuntimeClock::Time{});
    auto nav_status = raw_service->inspect("game.test", nav_token);
    check(nav_status.state == "queued", "Deferred navigation completed before ack");
    check(backend.navigation_effects_recorded == 0,
          "Navigation effect recorded on dispatch, not on ack");
    // Pump again without ack: still queued, still no effect recorded.
    host.pump(RuntimeClock::Time{});
    check(raw_service->inspect("game.test", nav_token).state == "queued",
          "Deferred navigation did not remain queued without ack");
    check(backend.navigation_effects_recorded == 0,
          "Idle second pump recorded the navigation effect");
    backend.ack_navigation(nav_token);
    host.pump(RuntimeClock::Time{});
    nav_status = raw_service->inspect("game.test", nav_token);
    check(nav_status.state == "succeeded", "Navigation did not succeed after backend ack");
    check(backend.navigation_effects_recorded == 1 && backend.nav_effects.at(nav_token) == "next",
          "Navigation effect was not recorded exactly once on ack");
    raw_service->release("game.test", nav_token);
    // Unsupported direction is validated by the executor on pump, NOT by
    // request admission. Enqueue, pump, observe failed receipt and no
    // adapter dispatch.
    int nav_calls_before = backend.navigation_calls;
    auto bad_nav = raw_service->request(
        "game.test", {{"operation", "ui_navigation"}, {"direction", "diagonal"}});
    host.pump(RuntimeClock::Time{});
    auto bad_status = raw_service->inspect("game.test", bad_nav);
    check(bad_status.state == "failed", "Unsupported direction did not produce a failed receipt");
    check(backend.navigation_calls == nav_calls_before,
          "Unsupported direction invoked the polled adapter");
    raw_service->release("game.test", bad_nav);
    // release_cursor while a polled cursor capture is pending: the synchronous
    // release invalidates the pending token. The next pump must NOT
    // recapture; the receipt fails because the adapter throws on the
    // cancelled token.
    auto in_flight_token =
        raw_service->request("game.test", {{"operation", "cursor"}, {"capture", true}});
    host.pump(RuntimeClock::Time{});
    auto in_flight_status = raw_service->inspect("game.test", in_flight_token);
    check(in_flight_status.state == "queued",
          "Initial release-while-pending token did not stay queued");
    // Backend captured state from the previous (completed) request must be
    // untouched by the new request's first dispatch.
    int calls_before_release = backend.cursor_calls;
    int dispatches_before_release = backend.cursor_dispatches;
    bool captured_before = backend.captured;
    host.release_cursor();
    check(!host.cursor_captured(), "Synchronous release did not clear host cursor_captured");
    check(!backend.captured, "Synchronous release did not clear backend captured flag");
    check(captured_before, "Backend captured flag was not preserved from prior completed request");
    // Now simulate a delayed ack for the cancelled token. The adapter must
    // throw rather than report completion, and the host must not
    // re-capture.
    backend.ack_cursor(in_flight_token);
    host.pump(RuntimeClock::Time{});
    auto cancelled_status = raw_service->inspect("game.test", in_flight_token);
    check(cancelled_status.state == "failed" && cancelled_status.error == "cursor effect cancelled",
          "Cancelled cursor receipt did not surface backend cancellation");
    check(!host.cursor_captured(),
          "Host re-captured cursor after synchronous release invalidated pending capture");
    check(!backend.captured, "Backend recaptured after delayed ack on cancelled token");
    check(backend.cursor_dispatches == dispatches_before_release,
          "Release-while-pending caused an extra physical dispatch");
    check(backend.cursor_calls > calls_before_release,
          "Adapter was not consulted after delayed ack on a cancelled token");
    raw_service->release("game.test", in_flight_token);
    // Adapter failure: a polled adapter that throws on every call surfaces
    // the failure in the receipt and leaves cursor_captured false.
    struct ThrowingCursor {
        bool operator()(std::uint64_t, bool) { throw std::runtime_error("cursor offline"); }
    };
    GamePlatformControls throwing;
    throwing.cursor_poll = ThrowingCursor();
    // Synchronous cursor required for release_cursor() to be meaningful when
    // a polled adapter is configured. The test owns the same fixture.
    throwing.cursor = [](bool) {};
    GameHostControls failing(game, queue, storage, content, defaults, input, Json::object(),
                             throwing);
    auto fail_service = game.active().engine.services().game();
    auto throw_token =
        fail_service->request("game.test", {{"operation", "cursor"}, {"capture", true}});
    failing.pump(RuntimeClock::Time{});
    auto throw_status = fail_service->inspect("game.test", throw_token);
    check(throw_status.state == "failed" && throw_status.error == "cursor offline",
          "Polled cursor failure was not surfaced in receipt");
    check(!failing.cursor_captured(), "Cursor captured state set despite adapter failure");
    fail_service->release("game.test", throw_token);
    // Constructor must reject a polled cursor adapter without a synchronous
    // cursor release callback, otherwise a host could capture but never
    // physically release on pause/unload. Navigation-only polled adapters
    // do not own capture/release and must remain constructible.
    {
        GamePlatformControls no_sync;
        no_sync.cursor_poll = [](std::uint64_t, bool) { return true; };
        bool rejected = false;
        try {
            GameHostControls(game, queue, storage, content, defaults, input, Json::object(),
                             no_sync);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check(rejected, "GameHostControls accepted polled cursor without synchronous cursor");
    }
    // Navigation-only polled host must construct without a synchronous
    // cursor callback and must dispatch navigation requests through the
    // polled adapter to a counted completion.
    int nav_completions = 0;
    {
        GamePlatformControls nav_only;
        nav_only.navigation_poll = [&nav_completions](std::uint64_t, const std::string&) {
            ++nav_completions;
            return true;
        };
        GameHostControls nav_host(game, queue, storage, content, defaults, input, Json::object(),
                                  nav_only);
        auto nav_service = game.active().engine.services().game();
        auto nav_only_token = nav_service->request(
            "game.test", {{"operation", "ui_navigation"}, {"direction", "next"}});
        nav_host.pump(RuntimeClock::Time{});
        auto nav_only_status = nav_service->inspect("game.test", nav_only_token);
        check(nav_only_status.state == "succeeded",
              "Navigation-only polled host did not succeed a request");
        check(nav_completions == 1, "Navigation-only polled adapter was not invoked exactly once "
                                    "for an immediate-confirmed request");
        nav_service->release("game.test", nav_only_token);
    }
}
} // namespace
#include "game_host_lifecycle_tests.hpp"

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need scratch directory");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        run(root);
        const auto poll_root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        polled_cursor_navigation(poll_root);
        polled_lifecycle_tests::run(poll_root / "lifecycle");
        std::filesystem::remove_all(root);
        std::filesystem::remove_all(poll_root);
        std::cout << "Game host session, settings, rebind and save/load requests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
