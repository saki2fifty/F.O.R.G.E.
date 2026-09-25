// Polled lifecycle-release tests for GameHostControls.
//
// Each scenario builds a real session, storage, content tree and
// GameHostControls inside its own block. A small backend tracks poll
// calls, dispatches, synchronous releases, prepare-side schema
// callbacks and pending captures so each assertion names a concrete
// counter. No Fixture struct, no returning containers, no captured
// references across scope.
#pragma once
#include <cstdint>
#include <filesystem>
#include <forge/game_content.hpp>
#include <forge/game_host_controls.hpp>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <string>

namespace polled_lifecycle_tests {
using namespace forge;

inline void check(bool value, const char* error) {
    if (!value)
        throw std::runtime_error(error);
}

inline std::string read_bytes(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>{});
}

// Per-scenario backend. Constructed after owners so lambdas capture
// only by reference within this scope. The polled cursor adapter is
// called with (token,false) for a release request — returns true once
// ack_pending[token] is set, throws if throw_pending[token] is set, and
// records the (token, want) pair for inspection. Synchronous
// release_cursor() also funnels through force_release() so a release
// dispatched with the desired==false path cancels every prior in-flight
// capture, mirroring the implementation contract.
struct Backend {
    int poll_calls = 0;
    int sync_releases = 0;
    int validations = 0;
    int migrations = 0;
    int scene_ready_calls = 0;
    int restore_calls = 0;
    std::set<std::uint64_t> ack_pending;
    std::set<std::uint64_t> throw_pending;
    std::set<std::uint64_t> cancelled;
    std::set<std::uint64_t> capture_pending;
    std::map<std::uint64_t, bool> poll_log;
    bool operator()(std::uint64_t token, bool want) {
        ++poll_calls;
        poll_log[token] = want;
        if (want) {
            // First-time capture poll: track it so a subsequent release
            // poll can cancel it.
            if (!capture_pending.contains(token))
                capture_pending.insert(token);
        } else {
            // Release poll: a dispatched release cancels every prior
            // in-flight capture, mirroring what a real editor/native
            // backend does before the release effect lands.
            force_release();
        }
        if (cancelled.contains(token))
            throw std::runtime_error("cursor effect cancelled");
        if (throw_pending.contains(token)) {
            throw_pending.erase(token);
            throw std::runtime_error("backend refused release");
        }
        if (ack_pending.contains(token)) {
            ack_pending.erase(token);
            return true;
        }
        return false;
    }
    void force_release() {
        for (auto token : capture_pending)
            cancelled.insert(token);
        capture_pending.clear();
    }
};

// Build session, queue, content tree, storage, and GameHostControls in
// place into the caller-owned Owners. WorldContext is nonmovable, so
// Owners itself is nonmovable; initialize_owners writes into a stable
// caller-owned slot rather than returning by value. The Backend is
// declared by the caller before Owners, so any captured pointer
// outlives the Owners scope. Lambdas capture &o and reference the
// caller-owned storage directly.
struct Owners {
    std::filesystem::path root, content;
    WorldContext authoring;
    std::shared_ptr<GameControlQueue> queue;
    std::unique_ptr<GameSession> session;
    std::unique_ptr<GameStorage> storage;
    std::unique_ptr<GameHostControls> host;
    Backend* backend = nullptr;
    AssetId asset;
    std::shared_ptr<GameControlService> service() {
        return session->active().engine.services().game();
    }
};

inline void initialize_owners(Owners& o, const std::filesystem::path& scratch,
                              const std::string& app_id, Backend& backend_ref) {
    o.backend = &backend_ref;
    o.root = scratch / app_id;
    std::filesystem::create_directories(o.root);
    o.content = o.root / "content";
    std::filesystem::create_directories(o.content);
    Scene scene(o.authoring);
    scene.replace({{"version", 1}, {"entities", Json::array()}});
    scene.save(o.content / "main.scene.json");
    AssetCatalog catalog(o.content);
    o.asset = catalog.add_scene("main.scene.json").id;
    catalog.save(AssetCatalog::project_index(o.content));
    InputMap input({{"version", 1}, {"actions", Json::array()}});
    o.queue = std::make_shared<GameControlQueue>();
    GameSessionConfig config;
    config.controls = o.queue;
    config.input = input;
    EngineModule module;
    module.id = "game.test";
    module.dependencies = {"forge.game"};
    module.allowed_services = capability(Capability::Game);
    module.runtime_roles = role_mask(WorldRole::Runtime);
    module.start = [&o](ModuleContext& c) {
        GameSaveSchema schema{2,
                              [&o](const GameSave&) { ++o.backend->validations; },
                              {{1, [&o](GameSave save) {
                                    ++o.backend->migrations;
                                    save.data["migrated"] = true;
                                    return save;
                                }}}};
        c.services.game()->save_schema(c.id, schema, c.code);
    };
    module.scene_ready = [&o](ModuleContext& c) {
        ++o.backend->scene_ready_calls;
        c.world.entity("game.prepared").set<int>(1);
    };
    module.restore = [&o](ModuleContext& c, const Json& data) {
        ++o.backend->restore_calls;
        if (data.contains("counter") && data.at("counter").is_number_integer())
            c.world.entity("game.saved").set<int>(data.at("counter").get<int>());
    };
    config.modules.push_back(module);
    o.session = std::make_unique<GameSession>(config);
    o.session->activate(o.session->prepare(load_game_scene(o.content, {o.asset})),
                        RuntimeClock::Time{}, true);
    o.storage = std::make_unique<GameStorage>(o.root / "users", app_id);
    const auto defaults = default_game_settings(app_id, "Lifecycle");
    GamePlatformControls platform;
    platform.cursor_poll = [&backend_ref](std::uint64_t token, bool want) {
        return backend_ref(token, want);
    };
    platform.cursor = [&backend_ref](bool want) {
        if (!want) {
            ++backend_ref.sync_releases;
            backend_ref.force_release();
        }
    };
    o.host = std::make_unique<GameHostControls>(*o.session, o.queue, *o.storage, o.content,
                                                defaults, input, Json::object(), platform);
}

// Dispatch and pump. The issuing service is captured before the pump
// so subsequent retired-receipt handling never touches the
// post-unload/activate world.
inline std::uint64_t enqueue(Owners& o, Json command,
                             std::shared_ptr<GameControlService>& issuing) {
    issuing = o.service();
    const auto token = issuing->request("game.test", command);
    o.host->pump(RuntimeClock::Time{});
    return token;
}

inline void run(const std::filesystem::path& scratch) {
    using namespace polled_lifecycle_tests;
    // ---- pause, quit, unload, prepare(true), prepare(false),
    // load(true): pending leaves lifecycle unchanged; ack runs effect
    // once; throw leaves world + save bytes preserved.
    const char* ops[] = {"pause", "quit", "unload", "prepare_true", "prepare_false", "load_true"};
    for (const char* op : ops) {
        for (const bool fail : {false, true}) {
            Backend backend;
            Owners o;
            initialize_owners(o, scratch / op / (fail ? "fail" : "ok"), "org.forge.host-pause",
                              backend);
            // No simulated prior capture: cursor_==false; the helper
            // still invokes the polled adapter per the contract.
            const auto active_before = reinterpret_cast<std::uintptr_t>(&o.session->active());
            const int validations_before = o.backend->validations;
            const int migrations_before = o.backend->migrations;
            const int restore_before = o.backend->restore_calls;
            const int scene_ready_before = o.backend->scene_ready_calls;
            const int sync_before = o.backend->sync_releases;
            const int poll_before = o.backend->poll_calls;
            // Save a slot for the load scenario. Write version 1 so
            // the registered v1→v2 migration fires exactly once on load.
            o.storage->save("slot-a", {o.asset, {{"counter", 7}}},
                            GameSaveSchema{1, [](const GameSave&) {}, {}});
            const auto save_before = read_bytes(o.storage->root() / "slot-slot-a.json");
            Json command;
            if (std::string(op) == "pause")
                command = {{"operation", "pause"}};
            else if (std::string(op) == "quit")
                command = {{"operation", "quit"}};
            else if (std::string(op) == "unload")
                command = {{"operation", "unload"}};
            else if (std::string(op) == "prepare_true")
                command = {{"operation", "prepare"}, {"asset", o.asset}, {"activate", true}};
            else if (std::string(op) == "prepare_false")
                command = {{"operation", "prepare"}, {"asset", o.asset}, {"activate", false}};
            else
                command = {{"operation", "load"}, {"slot", "slot-a"}, {"activate", true}};
            // Map the specific release token to failure. We can only do
            // this once the token is issued by the host queue.
            // ---- Pending phase: dispatch + two idle pumps, lifecycle
            // must be unchanged. The polled adapter is invoked once per
            // pump while the entry remains pending.
            std::shared_ptr<GameControlService> issuing;
            std::uint64_t token = enqueue(o, command, issuing);
            if (fail) {
                o.backend->throw_pending.insert(token);
                o.host->pump(RuntimeClock::Time{});
                o.host->pump(RuntimeClock::Time{});
                check(issuing->inspect("game.test", token).state == "failed",
                      "Throwing release did not fail receipt");
                check(o.backend->sync_releases == sync_before,
                      "Throwing release performed a synchronous release");
                check(reinterpret_cast<std::uintptr_t>(&o.session->active()) == active_before,
                      "Throwing release replaced active world");
                check(o.session->status().at("state") == "running",
                      "Throwing release mutated session state");
                check(o.session->status().at("prepared_ticket").get<std::uint64_t>() == 0,
                      "Throwing release left a pending candidate ticket");
                check(read_bytes(o.storage->root() / "slot-slot-a.json") == save_before,
                      "Throwing release rewrote save bytes");
                issuing->release("game.test", token);
                continue;
            }
            o.host->pump(RuntimeClock::Time{});
            o.host->pump(RuntimeClock::Time{});
            check(o.backend->poll_calls == poll_before + 3,
                  "Pending release did not invoke the polled adapter once per pump");
            check(o.backend->sync_releases == sync_before,
                  "Pending release performed a synchronous release");
            check(reinterpret_cast<std::uintptr_t>(&o.session->active()) == active_before,
                  "Pending release replaced active world");
            check(o.session->status().at("state") == "running",
                  "Pending release mutated session state");
            check(o.session->status().at("prepared_ticket").get<std::uint64_t>() == 0,
                  "Pending release produced a pending candidate ticket");
            check(o.backend->scene_ready_calls == scene_ready_before &&
                      o.backend->restore_calls == restore_before &&
                      o.backend->validations == validations_before &&
                      o.backend->migrations == migrations_before,
                  "Pending release ran prepare/restore/validate/migration");
            // ---- Ack phase ----
            o.backend->ack_pending.insert(token);
            o.host->pump(RuntimeClock::Time{});
            const std::string s = op;
            if (s == "pause") {
                check(o.session->status().at("state") == "paused",
                      "Pause did not reach paused state after ack");
                check(o.backend->sync_releases == 1,
                      "Synchronous release was not counted exactly once on ack");
            } else if (s == "quit") {
                check(o.host->quit_requested(), "Quit flag not set after ack");
                check(o.backend->sync_releases == 1,
                      "Synchronous release was not counted exactly once on ack");
            } else if (s == "unload") {
                // GameSession::unload swaps in an empty world. The
                // receipt is retired; the host has no active world.
                check(o.session->status().at("state") == "empty",
                      "Unload did not reach empty state");
                check(o.backend->sync_releases == 1,
                      "Synchronous release was not counted exactly once on ack");
                // Do not re-inspect or release the retired receipt.
                o.host->pump(RuntimeClock::Time{});
                check(o.backend->poll_calls == poll_before + 4,
                      "Idle pump after unload dispatched the polled adapter again");
                continue;
            } else if (s == "prepare_true") {
                // Activate=true: activation runs in the host pump, the
                // endpoint is retired, and inspect may no longer be
                // valid. Verify success by changed active world plus
                // scene_ready/restore counters.
                check(reinterpret_cast<std::uintptr_t>(&o.session->active()) != active_before,
                      "Prepare activate=true did not publish a new world");
                check(o.backend->scene_ready_calls == scene_ready_before + 1,
                      "Prepare activate=true did not invoke scene_ready exactly once");
                check(o.backend->sync_releases == 1,
                      "Synchronous release was not counted exactly once on ack");
            } else if (s == "prepare_false") {
                // Activate=false leaves the prior endpoint active; the
                // ticket may be inspected and cancelled cleanly.
                check(reinterpret_cast<std::uintptr_t>(&o.session->active()) == active_before,
                      "Prepare activate=false replaced the active world");
                check(issuing->inspect("game.test", token).state == "succeeded",
                      "Prepare activate=false did not succeed");
                const auto ticket =
                    issuing->inspect("game.test", token).value.at("ticket").get<std::uint64_t>();
                check(ticket != 0, "Prepare activate=false did not yield a candidate ticket");
                check(o.backend->sync_releases == 1,
                      "Synchronous release was not counted exactly once on ack");
                issuing->release("game.test", token);
                // Cancel the candidate so it does not race the
                // subsequent idle pump.
                std::shared_ptr<GameControlService> cancel_issuing;
                enqueue(o, {{"operation", "cancel"}, {"ticket", ticket}}, cancel_issuing);
            } else {
                // load_true: the activation pump retires the endpoint
                // for the receipt. Verify by changed active world plus
                // scene_ready/restore/migration counters, not by
                // inspecting the retired receipt.
                check(reinterpret_cast<std::uintptr_t>(&o.session->active()) != active_before,
                      "Load activate=true did not publish a new world");
                check(o.backend->scene_ready_calls == scene_ready_before + 1,
                      "Load activate=true did not invoke scene_ready exactly once");
                check(o.backend->restore_calls == restore_before + 1,
                      "Load did not trigger restore exactly once");
                check(o.backend->validations == validations_before + 1,
                      "Load did not trigger validation exactly once");
                check(o.backend->migrations == migrations_before + 1,
                      "Load did not trigger the v1→v2 migration exactly once");
                check(o.backend->sync_releases == 1,
                      "Synchronous release was not counted exactly once on ack");
            }
            o.host->pump(RuntimeClock::Time{});
            // No re-dispatch once the polled request has completed.
            check(o.backend->poll_calls == poll_before + 4,
                  "Idle pump produced an extra poll dispatch after completion");
        }
    }
    // ---- load activate=false: storage data returned, never
    // request.data. One validation, one migration. Read-only load does
    // not consult the polled cursor adapter.
    {
        Backend backend;
        Owners o;
        initialize_owners(o, scratch / "read_only", "org.forge.host-read", backend);
        // Save with a legacy version so load must migrate.
        GameSaveSchema legacy{1, [](const GameSave&) {}, {}};
        o.storage->save("slot-a", {o.asset, {{"counter", 7}}}, legacy);
        const auto save_before = read_bytes(o.storage->root() / "slot-slot-a.json");
        const int validations_before = o.backend->validations;
        const int migrations_before = o.backend->migrations;
        const int poll_before = o.backend->poll_calls;
        Json command = {{"operation", "load"}, {"slot", "slot-a"}, {"activate", false}};
        command["data"] = {{"sentinel", true}, {"counter", -999}};
        std::shared_ptr<GameControlService> issuing;
        std::uint64_t token = enqueue(o, command, issuing);
        check(issuing->inspect("game.test", token).state == "succeeded",
              "Read-only load did not succeed");
        const auto& result = issuing->inspect("game.test", token).value;
        check(!result.at("data").contains("sentinel"),
              "Read-only load returned command.data sentinel");
        check(result.at("data").at("counter") == 7, "Read-only load did not return stored counter");
        check(read_bytes(o.storage->root() / "slot-slot-a.json") == save_before,
              "Read-only load mutated save bytes");
        check(o.backend->validations == validations_before + 1,
              "Read-only load did not validate exactly once");
        check(o.backend->migrations == migrations_before + 1,
              "Read-only load did not migrate exactly once");
        check(o.backend->poll_calls == poll_before,
              "Read-only load invoked the polled cursor adapter");
        issuing->release("game.test", token);
    }
    // ---- Malformed run rejected before polled release dispatch. ----
    {
        Backend backend;
        Owners o;
        initialize_owners(o, scratch / "bad_run", "org.forge.host-bad-run", backend);
        GameSaveSchema legacy{1, [](const GameSave&) {}, {}};
        o.storage->save("slot-a", {o.asset, {{"counter", 1}}}, legacy);
        const int poll_before = o.backend->poll_calls;
        const auto active_before = reinterpret_cast<std::uintptr_t>(&o.session->active());
        const auto save_before = read_bytes(o.storage->root() / "slot-slot-a.json");
        std::shared_ptr<GameControlService> issuing;
        std::uint64_t token = enqueue(
            o, {{"operation", "load"}, {"slot", "slot-a"}, {"activate", true}, {"run", "yes"}},
            issuing);
        check(issuing->inspect("game.test", token).state == "failed", "Malformed run did not fail");
        check(o.backend->poll_calls == poll_before,
              "Malformed run invoked the polled cursor adapter");
        check(reinterpret_cast<std::uintptr_t>(&o.session->active()) == active_before,
              "Malformed run replaced active world");
        check(read_bytes(o.storage->root() / "slot-slot-a.json") == save_before,
              "Malformed load rewrote save bytes");
        issuing->release("game.test", token);
    }
    // ---- Pending capture before pause: host cursor_==false; a release
    // poll for the pause token cancels the prior in-flight capture
    // immediately through the sync release forced by the release poll
    // ack; a late ack of the capture token then throws. ----
    {
        Backend backend;
        Owners o;
        initialize_owners(o, scratch / "pending_capture", "org.forge.host-pend", backend);
        std::shared_ptr<GameControlService> issuing;
        // Issue a capture request that the backend leaves pending.
        std::uint64_t cap_token = enqueue(o, {{"operation", "cursor"}, {"capture", true}}, issuing);
        o.host->pump(RuntimeClock::Time{});
        check(!o.host->cursor_captured(), "Pending capture set host cursor_captured without ack");
        check(o.backend->poll_log.at(cap_token) == true,
              "Capture poll was not logged with want=true");
        // Issue pause. Both the still-pending capture and the pause
        // request are dispatched by the host pump, so poll_calls grows
        // by 2: one re-poll of the capture (want=true, stays pending)
        // and one release poll for pause (want=false, also stays
        // pending because no ack has been injected yet).
        const int poll_before = o.backend->poll_calls;
        std::shared_ptr<GameControlService> pause_issuing;
        std::uint64_t pause_token = enqueue(o, {{"operation", "pause"}}, pause_issuing);
        check(o.backend->poll_calls == poll_before + 2,
              "Pause did not dispatch both the pending capture and the release poll");
        check(o.backend->poll_log.at(pause_token) == false,
              "Pause release poll was not logged with want=false");
        check(o.backend->poll_log.at(cap_token) == true, "Capture poll lost its want=true payload");
        // Inject ack for BOTH pause_token and cap_token before the next
        // pump. The pause enqueue above already dispatched its release
        // poll with want=false, which ran force_release() and moved
        // cap_token into cancelled at that point. After both acks are
        // inserted, the next pump completes pause (release returns
        // true, game pauses) and rejects cap_token (cancelled, throws).
        o.backend->ack_pending.insert(pause_token);
        o.backend->ack_pending.insert(cap_token);
        o.host->pump(RuntimeClock::Time{});
        check(pause_issuing->inspect("game.test", pause_token).state == "succeeded" &&
                  o.session->status().at("state") == "paused",
              "Pause did not succeed after release ack");
        check(issuing->inspect("game.test", cap_token).state == "failed" &&
                  issuing->inspect("game.test", cap_token).error == "cursor effect cancelled",
              "Late capture ack did not surface backend cancellation");
        check(!o.host->cursor_captured(), "Late capture ack re-captured the host cursor");
        // Final idle pump: nothing left in flight, no repeated effect.
        o.host->pump(RuntimeClock::Time{});
        check(!o.host->cursor_captured(),
              "Idle pump after cancellation re-captured the host cursor");
        pause_issuing->release("game.test", pause_token);
        issuing->release("game.test", cap_token);
    }
}
} // namespace polled_lifecycle_tests
