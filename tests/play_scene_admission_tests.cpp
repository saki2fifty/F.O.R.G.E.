#include "play_scene_admission.hpp"
#include <forge/animation.hpp>
#include <forge/audio_components.hpp>
#include <forge/game_session.hpp>
#include <forge/navigation.hpp>
#include <forge/navigation_components.hpp>
#include <forge/physics.hpp>
#include <forge/runtime_world.hpp>
#include <forge/scene.hpp>
#include <iostream>
#include <memory>
#include <thread>
using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F&& f) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
Json scene_snapshot(const char* name) {
    WorldContext world;
    Scene scene(world);
    scene.replace(
        {{"version", 1},
         {"entities",
          Json::array({{{"id", name},
                        {"name", name},
                        {"components", {{"forge.position", {{"x", 0}, {"y", 0}, {"z", 0}}}}}}})}});
    return scene.snapshot();
}
RuntimeClock::Time at(int ms) { return RuntimeClock::Time{} + std::chrono::milliseconds(ms); }
struct SessionFixture {
    std::string session_name;
    PlaySceneAdmission channel;
    GameSessionConfig config;
    std::unique_ptr<GameSession> game;
    explicit SessionFixture(const char* s) : session_name(s), channel(s) {
        config.preparation = [&](std::uint64_t ticket) { return channel.prepare(ticket); };
        game = std::make_unique<GameSession>(config);
    }
};
void initial_scene_does_not_activate_before_ack() {
    SessionFixture f("init");
    check(f.game->status().at("state") == "empty", "New session must start empty");
    rejects([&] { f.game->active(); });
    auto ticket = f.game->prepare(scene_snapshot("first"));
    check(f.channel.has_pending(), "Prepare did not register pending candidate");
    check(!f.channel.snapshot().has_value(), "Snapshot appeared before poll_preparation ran");
    check(!f.channel.acknowledge(f.session_name, ticket, true),
          "Ack succeeded before snapshot publication");
    auto progress = f.game->poll_preparation(ticket);
    check(!progress.ready, "First poll reported ready before ack");
    check(progress.stage == "editor", "Pending stage must be editor");
    rejects([&] { f.game->activate(ticket, at(0), true); });
    check(f.channel.has_pending(), "Activate before ack discarded the candidate");
    check(!f.game->status().at("state").get<std::string>().empty() &&
              f.game->status().at("state") == "empty",
          "Activate before ack mutated session state");
    auto snap = f.channel.snapshot();
    check(snap.has_value(), "Snapshot missing after poll_preparation");
    check(snap->at("ticket") == ticket, "Snapshot ticket mismatch");
    check(snap->at("session") == f.session_name, "Snapshot session mismatch");
    check(snap->at("scene").contains("entities"), "Envelope missing scene payload");
    auto again = f.game->poll_preparation(ticket);
    check(again.stage == "editor" && !again.ready,
          "Repeated polls must not change snapshot or progress");
    rejects([&] { f.game->active(); });
}
void exported_snapshot_mutation_does_not_alter_gate() {
    SessionFixture f("immutable");
    auto ticket = f.game->prepare(scene_snapshot("alpha"));
    f.game->poll_preparation(ticket);
    auto snap = f.channel.snapshot();
    check(snap.has_value(), "Snapshot missing");
    const auto before_dump = snap->dump();
    auto copy = *snap;
    copy["ticket"] = 999;
    copy["session"] = "tampered";
    copy["scene"]["entities"] = Json::array();
    auto after = f.channel.snapshot();
    check(after.has_value(), "Snapshot disappeared");
    check(after->dump() == before_dump, "Exported snapshot mutation altered the envelope");
    check(!f.channel.acknowledge("tampered", ticket, true), "Tampered session name was accepted");
    check(!f.channel.acknowledge("immutable", ticket + 7, true), "Tampered ticket was accepted");
    check(f.channel.acknowledge("immutable", ticket, true), "Exact acknowledge was rejected");
}
void exact_ack_lifts_gate_and_activation_publishes() {
    SessionFixture f("accept");
    auto ticket = f.game->prepare(scene_snapshot("primary"));
    f.game->poll_preparation(ticket);
    check(f.channel.acknowledge(f.session_name, ticket, true),
          "Exact accepted ack rejected on first call");
    check(f.channel.acknowledge(f.session_name, ticket, true),
          "Identical accepted ack was not idempotent");
    check(!f.channel.acknowledge(f.session_name, ticket, false),
          "Contradictory ack after acceptance succeeded");
    check(!f.channel.acknowledge(f.session_name, ticket, true, "delta"),
          "Identical ack with different diagnostic succeeded");
    auto ready = f.game->poll_preparation(ticket);
    check(ready.ready, "Poll did not reach ready after accepted ack");
    f.game->activate(ticket, at(0), true);
    check(f.game->active().scene.entity("primary").is_alive(),
          "Accepted ack did not publish scene");
    check(!f.channel.snapshot().has_value(), "Snapshot still exposed after committal");
    check(!f.channel.acknowledge(f.session_name, ticket, true),
          "Snapshot/ack still exposed after committal");
}
void ui_absence_returns_null() {
    SessionFixture f("no-ui");
    auto ticket = f.game->prepare(scene_snapshot("absent"));
    f.game->poll_preparation(ticket);
    auto snap = f.channel.snapshot();
    check(snap.has_value(), "Snapshot missing for ui-less session");
    check(snap->at("ui").is_null(), "UI service absent but envelope reported ui payload");
}
void repeated_polls_are_idempotent() {
    SessionFixture f("repeat");
    auto ticket = f.game->prepare(scene_snapshot("repeat"));
    f.game->poll_preparation(ticket);
    auto first = f.channel.snapshot();
    for (int i = 0; i < 3; ++i) {
        auto p = f.game->poll_preparation(ticket);
        check(p.stage == "editor" && !p.ready, "Repeated poll advanced state");
    }
    auto last = f.channel.snapshot();
    check(first.has_value() && last.has_value() && first->at("ticket") == last->at("ticket") &&
              first->at("scene").dump() == last->at("scene").dump(),
          "Repeated polls mutated snapshot");
    check(f.game->status().at("clock").at("tick") == 0, "Repeated polls advanced active clock");
}
void rejected_ack_does_not_discard_active() {
    SessionFixture f("reject");
    auto prior = f.game->prepare(scene_snapshot("prior"));
    f.game->poll_preparation(prior);
    check(f.channel.acknowledge("reject", prior, true), "Prior scene acknowledgement failed");
    f.game->activate(prior, at(0), true);
    f.game->advance(at(20));
    const auto tick_before = f.game->status().at("clock").at("tick");
    auto& prior_world = f.game->active();
    auto rejected = f.game->prepare(scene_snapshot("replacement"));
    f.game->poll_preparation(rejected);
    check(f.channel.acknowledge("reject", rejected, false, "editor says no"),
          "Rejected ack was not accepted on first call");
    check(f.channel.acknowledge("reject", rejected, false, "editor says no"),
          "Identical rejected ack was not idempotent");
    check(!f.channel.acknowledge("reject", rejected, true, "editor says no"),
          "Contradictory verdict accepted on rejected candidate");
    check(!f.channel.acknowledge("reject", rejected, false, "different reason"),
          "Different diagnostic on rejected candidate accepted");
    bool threw = false;
    std::string what;
    try {
        (void)f.game->poll_preparation(rejected);
    } catch (const std::exception& e) {
        threw = true;
        what = e.what();
    }
    check(threw, "Poll after rejected ack did not throw");
    check(what.find("editor says no") != std::string::npos,
          "Diagnostic was not propagated into thrown error");
    check(&f.game->active() == &prior_world, "Rejected ack changed the active world identity");
    check(f.game->active().scene.entity("prior").is_alive(),
          "Rejected ack discarded the active world");
    check(f.game->status().at("clock").at("tick") == tick_before,
          "Rejected ack mutated the active clock");
}
void cancellation_drops_stale_ack() {
    SessionFixture f("cancel");
    auto stale = f.game->prepare(scene_snapshot("stale"));
    f.game->cancel(stale);
    check(!f.channel.has_pending(), "Pending remained after cancellation");
    check(!f.channel.acknowledge("cancel", stale, true),
          "Stale ack on cancelled candidate succeeded");
    auto next = f.game->prepare(scene_snapshot("next"));
    f.game->poll_preparation(next);
    check(f.channel.acknowledge("cancel", next, true),
          "Fresh candidate ack rejected after cancellation");
    f.game->activate(next, at(0), true);
    check(f.game->active().scene.entity("next").is_alive(), "Fresh candidate did not publish");
}
void supersession_drops_stale_ack() {
    SessionFixture f("supersede");
    auto stale = f.game->prepare(scene_snapshot("stale"));
    auto fresh = f.game->prepare(scene_snapshot("fresh"));
    check(!f.channel.acknowledge("supersede", stale, true),
          "Stale ack on superseded candidate succeeded");
    check(f.channel.has_pending(), "Supersession lost pending candidate");
    // After a non-cancelled supersede the stale record is gone, so
    // fetching its envelope must report no candidate available even
    // before the fresh candidate has been polled.
    check(!f.channel.fetch_envelope("supersede", stale).has_value(),
          "Stale envelope was still fetchable after non-cancelled supersede");
    f.game->poll_preparation(fresh);
    check(f.channel.acknowledge("supersede", fresh, true), "Fresh candidate ack rejected");
    f.game->activate(fresh, at(0), true);
    check(f.game->active().scene.entity("fresh").is_alive(), "Fresh candidate did not publish");
}
void old_committed_adapter_does_not_close_replacement() {
    SessionFixture f("commit-replace");
    auto first = f.game->prepare(scene_snapshot("first"));
    f.game->poll_preparation(first);
    check(f.channel.acknowledge("commit-replace", first, true), "First candidate ack rejected");
    f.game->activate(first, at(0), true);
    check(f.game->active().scene.entity("first").is_alive(), "First not activated");
    auto second = f.game->prepare(scene_snapshot("second"));
    check(f.channel.has_pending(), "Replacement candidate not pending");
    f.game->poll_preparation(second);
    check(f.channel.acknowledge("commit-replace", second, true),
          "Replacement ack rejected after committal");
    f.game->activate(second, at(0), true);
    check(f.game->active().scene.entity("second").is_alive(), "Second candidate not activated");
    auto third = f.game->prepare(scene_snapshot("third"));
    check(f.channel.has_pending(), "Old committed adapter destruction invalidated successor");
    f.game->poll_preparation(third);
    check(f.channel.acknowledge("commit-replace", third, true),
          "Third candidate ack rejected after committed predecessor");
    f.game->activate(third, at(0), true);
    check(f.game->active().scene.entity("third").is_alive(), "Third candidate not activated");
}
void bounds_session_tickets() {
    PlaySceneAdmission channel("bounds");
    rejects([&] { channel.prepare(0); });
    auto a = channel.prepare(1);
    rejects([&] { channel.prepare(1); });
    rejects([&] { channel.prepare(0); });
    rejects([&] { channel.prepare(2); });
    a.reset();
    rejects([&] { channel.prepare(1); });
    auto b = channel.prepare(2);
    b.reset();
    rejects([&] { channel.prepare(2); });
    auto c = channel.prepare(3);
    c.reset();
    rejects([] { PlaySceneAdmission bad(std::string(129, 'a')); });
    rejects([] { PlaySceneAdmission empty(std::string{}); });
    PlaySceneAdmission max_session(std::string(128, 'a'));
    auto d = max_session.prepare(1);
    d.reset();
}
void diagnostic_bound_after_export() {
    SessionFixture f("diagnostic");
    auto ticket = f.game->prepare(scene_snapshot("diag"));
    f.game->poll_preparation(ticket);
    check(!f.channel.acknowledge("diagnostic", ticket, false, std::string(8193, 'x')),
          "Oversized diagnostic accepted");
    check(f.channel.acknowledge("diagnostic", ticket, false, std::string(8192, 'x')),
          "Boundary diagnostic rejected");
}
void owner_thread_contract() {
    SessionFixture f("owner");
    auto ticket = f.game->prepare(scene_snapshot("owner"));
    f.game->poll_preparation(ticket);
    check(f.channel.acknowledge("owner", ticket, true), "Initial ack rejected");
    f.game->activate(ticket, at(0), true);
    RuntimeWorld* world_ptr = &f.game->active();
    auto adapter = f.channel.prepare(ticket + 1);
    bool pending_refused = false;
    bool snap_refused = false;
    bool ack_refused = false;
    bool prepare_refused = false;
    bool poll_refused = false;
    std::thread foreign([&] {
        if (f.channel.has_pending())
            pending_refused = true;
        try {
            (void)f.channel.snapshot();
        } catch (const std::exception&) {
            snap_refused = true;
        }
        if (f.channel.acknowledge("owner", ticket + 1, true))
            ack_refused = true;
        try {
            (void)f.channel.prepare(ticket + 2);
        } catch (const std::exception&) {
            prepare_refused = true;
        }
        try {
            (void)adapter->poll(*world_ptr);
        } catch (const std::exception&) {
            poll_refused = true;
        }
    });
    foreign.join();
    check(!pending_refused, "Foreign has_pending returned true");
    check(snap_refused, "Foreign snapshot did not throw");
    check(!ack_refused, "Foreign acknowledge returned true");
    check(prepare_refused, "Foreign prepare did not throw");
    check(poll_refused, "Foreign adapter poll did not throw");
}
void channel_close_invalidates_adapter() {
    std::unique_ptr<GameScenePreparation> adapter_holder;
    {
        PlaySceneAdmission channel("close");
        adapter_holder = channel.prepare(13);
    }
    GameSessionConfig config;
    config.preparation = [&](std::uint64_t) { return std::move(adapter_holder); };
    GameSession game(config);
    auto ticket = game.prepare(scene_snapshot("survives"));
    rejects([&] { game.poll_preparation(ticket); });
    check(game.status().at("state") == "empty", "Closed channel did not discard the candidate");
}
void runtime_readiness_failure_discards_candidate_and_keeps_active() {
    SessionFixture f("audio-fail");
    auto prior = f.game->prepare(scene_snapshot("prior"));
    f.game->poll_preparation(prior);
    check(f.channel.acknowledge("audio-fail", prior, true), "Prior candidate ack rejected");
    f.game->activate(prior, at(0), true);
    const auto tick_before = f.game->status().at("clock").at("tick");
    auto& prior_world = f.game->active();
    auto broken = f.game->prepare(scene_snapshot("broken"), [](Scene& s) {
        auto e = s.entity("broken").set<AudioSource>({});
        (void)e;
    });
    bool threw = false;
    std::string what;
    try {
        (void)f.game->poll_preparation(broken);
    } catch (const std::exception& e) {
        threw = true;
        what = e.what();
    }
    check(threw, "Runtime readiness failure did not throw");
    check(what.find("audio") != std::string::npos || what.find("Audio") != std::string::npos,
          "Thrown error did not mention audio");
    check(!f.channel.snapshot().has_value(), "Snapshot was published despite readiness failure");
    check(!f.channel.acknowledge("audio-fail", broken, true),
          "Ack succeeded despite readiness failure");
    check(&f.game->active() == &prior_world, "Readiness failure mutated the active world identity");
    check(f.game->status().at("clock").at("tick") == tick_before,
          "Readiness failure mutated the active clock");
}
void navigation_readiness_failure_discards_candidate_and_keeps_active() {
    SessionFixture f("nav-fail");
    auto prior = f.game->prepare(scene_snapshot("prior"));
    f.game->poll_preparation(prior);
    check(f.channel.acknowledge("nav-fail", prior, true), "Prior candidate ack rejected");
    f.game->activate(prior, at(0), true);
    const auto tick_before = f.game->status().at("clock").at("tick");
    auto& prior_world = f.game->active();
    auto broken = f.game->prepare(scene_snapshot("nav-broken"), [](Scene& s) {
        auto e = s.entity("nav-broken").set<NavigationAgent>({});
        (void)e;
    });
    bool threw = false;
    std::string what;
    try {
        (void)f.game->poll_preparation(broken);
    } catch (const std::exception& e) {
        threw = true;
        what = e.what();
    }
    check(threw, "Navigation readiness failure did not throw");
    check(what.find("Scene preparation") != std::string::npos,
          "Thrown navigation error missing scene-preparation prefix");
    check(!f.channel.snapshot().has_value(),
          "Snapshot was published despite navigation readiness failure");
    check(!f.channel.acknowledge("nav-fail", broken, true),
          "Ack succeeded despite navigation readiness failure");
    check(&f.game->active() == &prior_world,
          "Navigation failure mutated the active world identity");
    check(f.game->status().at("clock").at("tick") == tick_before,
          "Navigation failure mutated the active clock");
}
void envelope_size_bound() {
    SessionFixture f("size-bound");
    Json doc = scene_snapshot("anchor");
    doc["test_extension"] = std::string(kPlaySceneAdmissionEnvelopeBytes, 'x');
    auto ticket = f.game->prepare(doc);
    bool threw = false;
    std::string what;
    try {
        (void)f.game->poll_preparation(ticket);
    } catch (const std::exception& e) {
        threw = true;
        what = e.what();
    }
    check(threw, "Envelope overflow did not throw");
    check(what.find("8 MiB") != std::string::npos, "Envelope overflow did not report size limit");
    check(!f.channel.snapshot().has_value(), "Envelope published despite exceeding 8 MiB");
    check(!f.channel.acknowledge("size-bound", ticket, true),
          "Ack accepted for oversized envelope");
}
void validator_runs_once_with_actual_world_and_exact_ticket() {
    // The optional publication validator must run exactly once on the
    // owner thread, receive the actual prepared RuntimeWorld and the
    // exact ticket that the candidate was prepared under, BEFORE the
    // envelope is exported. A validator that just records its
    // arguments proves the contract; a normal-then-activate path
    // proves the candidate still publishes normally.
    auto session = std::string("validator-contract");
    PlaySceneAdmission channel(session);
    GameSessionConfig config;
    std::size_t calls = 0;
    RuntimeWorld* seen_world = nullptr;
    std::uint64_t seen_ticket = 0;
    config.preparation = [&](std::uint64_t ticket) {
        return channel.prepare(ticket, [&](RuntimeWorld& world, std::uint64_t t) {
            ++calls;
            seen_world = &world;
            seen_ticket = t;
        });
    };
    GameSession game(config);
    auto prior = game.prepare(scene_snapshot("prior"));
    game.poll_preparation(prior);
    check(calls == 1, "Validator did not run for the prior candidate");
    check(channel.acknowledge(session, prior, true), "Prior ack rejected");
    game.activate(prior, at(0), true);
    RuntimeWorld* active_before = &game.active();
    // Reset the counters AFTER prior activates so the guarded run is
    // measured independently. A validator that runs again under a
    // fresh prepare must do so exactly once with the new world's
    // pointer and the guarded ticket.
    calls = 0;
    seen_world = nullptr;
    seen_ticket = 0;
    auto ticket = game.prepare(scene_snapshot("guarded"));
    auto progress = game.poll_preparation(ticket);
    (void)progress;
    check(calls == 1, "Validator did not run exactly once for the guarded candidate");
    check(seen_world != nullptr, "Validator did not receive the prepared RuntimeWorld");
    check(seen_world != active_before,
          "Validator saw the prior active world instead of the prepared candidate");
    check(seen_ticket == ticket, "Validator did not receive the exact ticket");
    check(channel.acknowledge(session, ticket, true), "Validator-accepted candidate ack rejected");
    game.activate(ticket, at(0), true);
    check(game.active().scene.entity("guarded").is_alive(),
          "Validator-accepted candidate did not activate");
    check(&game.active() != active_before, "Activation did not swap the active world identity");
    // Repeated polls of the new active world never call the validator
    // again because the candidate has been committed; calling
    // poll_preparation with the now-stale ticket must surface the
    // canonical "Stale or missing prepared scene" diagnostic without
    // re-running the callback.
    game.advance(at(20));
    calls = 0;
    seen_world = nullptr;
    try {
        game.poll_preparation(ticket);
    } catch (const std::exception& e) {
        check(std::string(e.what()).find("Stale or missing prepared scene") != std::string::npos,
              "Post-committal poll did not surface the canonical stale ticket diagnostic");
    }
    check(calls == 0, "Validator re-ran after committal");
    (void)seen_world;
}
void throwing_validator_never_exports_or_acks() {
    // A validator that throws must prevent envelope publication, so
    // ack cannot succeed and the previous active scene survives.
    auto session = std::string("validator-reject");
    PlaySceneAdmission channel(session);
    GameSessionConfig config;
    bool reject = false;
    config.preparation = [&](std::uint64_t ticket) {
        return channel.prepare(ticket, [&reject](RuntimeWorld&, std::uint64_t) {
            if (reject)
                throw std::runtime_error("validator says no");
        });
    };
    GameSession game(config);
    auto prior = game.prepare(scene_snapshot("prior"));
    game.poll_preparation(prior);
    check(channel.acknowledge(session, prior, true), "Prior ack rejected");
    game.activate(prior, at(0), true);
    auto& prior_world = game.active();
    const auto tick_before = game.status().at("clock").at("tick");
    // Flip the validator into rejection mode only AFTER the prior
    // scene has been activated. The blocked candidate must now be
    // rejected while the prior world remains the active one.
    reject = true;
    auto ticket = game.prepare(scene_snapshot("blocked"));
    bool threw = false;
    std::string what;
    try {
        (void)game.poll_preparation(ticket);
    } catch (const std::exception& e) {
        threw = true;
        what = e.what();
    }
    check(threw, "Throwing validator did not surface as poll exception");
    check(what.find("validator says no") != std::string::npos,
          "Throwing validator diagnostic not propagated");
    check(!channel.snapshot().has_value(), "Throwing validator published envelope");
    check(!channel.acknowledge(session, ticket, true), "Throwing validator allowed successful ack");
    check(&game.active() == &prior_world, "Throwing validator disturbed active world identity");
    check(game.active().scene.entity("prior").is_alive(),
          "Throwing validator discarded active scene");
    check(game.status().at("clock").at("tick") == tick_before,
          "Throwing validator mutated active clock");
}
} // namespace
int main() {
    try {
        initial_scene_does_not_activate_before_ack();
        exported_snapshot_mutation_does_not_alter_gate();
        exact_ack_lifts_gate_and_activation_publishes();
        ui_absence_returns_null();
        repeated_polls_are_idempotent();
        rejected_ack_does_not_discard_active();
        cancellation_drops_stale_ack();
        supersession_drops_stale_ack();
        old_committed_adapter_does_not_close_replacement();
        bounds_session_tickets();
        diagnostic_bound_after_export();
        owner_thread_contract();
        channel_close_invalidates_adapter();
        runtime_readiness_failure_discards_candidate_and_keeps_active();
        navigation_readiness_failure_discards_candidate_and_keeps_active();
        envelope_size_bound();
        validator_runs_once_with_actual_world_and_exact_ticket();
        throwing_validator_never_exports_or_acks();
    } catch (const std::exception& e) {
        std::cerr << "play_scene_admission_tests failed: " << e.what() << "\n";
        return 1;
    }
    return 0;
}