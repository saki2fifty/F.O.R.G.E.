#include "audio_source_access.hpp"
#include "audio_test_fixture.hpp"
#include <forge/sound_cue.hpp>
#include <limits>

namespace {
void prepare(Fixture& f, AssetId clip, double interval, bool repeat_pause = true,
             bool source_pause = true, bool startup = false, unsigned mode = 0) {
    auto document = source(clip);
    auto& value = document["entities"][0]["components"]["forge.audio_source"];
    value["play_on_start"] = value["loop"] = false;
    value["pause_with_game"] = source_pause;
    document["entities"].push_back({{"id", "cue"},
                                    {"name", "Repeating event"},
                                    {"components", {{"forge.sound_cue", sound_cue_json({})}}}});
    f.load(document);
    SoundCue graph{true, 19, 0, {{0, f.ref(), {}}}};
    graph.repeat_interval_seconds = interval;
    graph.repeat_pause_with_game = repeat_pause;
    graph.repeat_on_start = startup;
    graph.repeat_mode = mode;
    f.scene.entity("cue").set(graph);
    if (startup)
        f.audio->refresh_sources();
    else if (graph.repeat_enabled())
        AudioSourceAccess::control(*f.audio, f.ref("cue"), AudioSourceOperation::Restart);
    f.audio->paused(false);
}
Json status(Fixture& f) { return AudioSourceAccess::status(*f.audio, f.ref("cue")); }
void control(Fixture& f, AudioSourceOperation operation) {
    AudioSourceAccess::control(*f.audio, f.ref("cue"), operation);
}
std::vector<float> read(Fixture& f, unsigned frames) {
    std::vector<float> pcm(frames * 2);
    f.audio->read_offline(pcm);
    return pcm;
}
void validation() {
    for (double interval : {-.1, .001, .249, 3600.001, std::numeric_limits<double>::infinity()}) {
        SoundCue cue;
        cue.repeat_interval_seconds = interval;
        reject([&] { (void)SoundCueProgram::compile(cue); });
    }
    SoundCue cue;
    cue.repeat_on_start = true;
    reject([&] { (void)SoundCueProgram::compile(cue); });
    cue.repeat_interval_seconds = .25;
    check(sound_cue_from_json(sound_cue_json(cue)) == cue,
          "Cue repetition fields changed in the shared codec");
    auto legacy = sound_cue_json({});
    for (const char* key :
         {"repeat_interval_seconds", "repeat_on_start", "repeat_pause_with_game", "repeat_mode"})
        legacy.erase(key);
    check(sound_cue_from_json(legacy) == SoundCue{}, "Earlier cue did not default repetition off");
}
void completion(const std::filesystem::path& root, AssetId clip) {
    SoundCue value;
    value.repeat_mode = 1;
    value.repeat_on_start = true;
    (void)SoundCueProgram::compile(value);
    check(sound_cue_from_json(sound_cue_json(value)) == value, "Completion mode codec changed");
    value.repeat_mode = 2;
    reject([&] { (void)SoundCueProgram::compile(value); });
    {
        Fixture isolated(root);
        prepare(isolated, clip, 0, true, true, false, 1);
        (void)read(isolated, 36000);
        check(status(isolated).at("accepted_triggers") == 1,
              "Completion producer emitted before its first voice finished");
        AudioSourceAccess::control(*isolated.audio, isolated.ref(), AudioSourceOperation::OneShot);
        const auto other =
            authoring_command(isolated.scene, "entity.create", {{"name", "Other event"}})
                .at("selected")
                .get<std::string>();
        isolated.scene.entity(other).set(SoundCue{true, 1, 0, {{0, isolated.ref(), {}}}});
        AudioSourceAccess::control(*isolated.audio, isolated.scene.reference(other),
                                   AudioSourceOperation::OneShot);
        (void)read(isolated, 24000);
        check(status(isolated).at("accepted_triggers") == 2 &&
                  status(isolated).at("repeat_completed_events") == 1 &&
                  status(isolated).at("repeat_event_voices") == 1 &&
                  AudioSourceAccess::status(*isolated.audio, isolated.ref()).at("one_shots") >= 3,
              "Unrelated manual or other-cue voices delayed completion production");
        control(isolated, AudioSourceOperation::Stop);
        check(AudioSourceAccess::status(*isolated.audio, isolated.ref()).at("one_shots") >= 3,
              "Completion Stop took ownership of existing Source voices");
    }
    {
        Fixture restart(root);
        prepare(restart, clip, 0, true, true, false, 1);
        auto graph = restart.scene.entity("cue").get<SoundCue>();
        graph.nodes[0].delay_seconds = 1;
        restart.scene.entity("cue").set(graph);
        control(restart, AudioSourceOperation::Restart);
        graph.nodes[0].delay_seconds = 0;
        restart.scene.entity("cue").set(graph);
        control(restart, AudioSourceOperation::Restart);
        (void)read(restart, 60000);
        check(status(restart).at("accepted_triggers") == 2 &&
                  AudioSourceAccess::status(*restart.audio, restart.ref()).at("one_shots") >= 2,
              "Restart or definition reset reused a ticket belonging to an older delayed event");
    }
    {
        Fixture gap(root);
        prepare(gap, clip, .25, true, true, false, 1);
        AudioSourceAccess::control(*gap.audio, gap.ref(), AudioSourceOperation::Stop);
        gap.audio->poll_loading();
        check(status(gap).at("repeat_completed_events") == 1,
              "Source Stop did not retire its event through the existing owner");
        (void)read(gap, 6000);
        control(gap, AudioSourceOperation::Pause);
        (void)read(gap, 24000);
        check(status(gap).at("accepted_triggers") == 1, "Paused completion gap advanced");
        control(gap, AudioSourceOperation::Resume);
        (void)read(gap, 6000);
        check(status(gap).at("accepted_triggers") == 1, "Completion gap resumed too early");
        (void)read(gap, 2);
        check(status(gap).at("accepted_triggers") == 2, "Completion gap lost remaining time");
    }
    {
        Fixture tails(root);
        prepare(tails, clip, 0, true, true, false, 1);
        auto source = tails.scene.entity("speaker").get<AudioSource>();
        source.echo_enabled = true;
        source.echo_seconds = .064f;
        source.echo_feedback = .3f;
        tails.scene.entity("speaker").set(source);
        tails.audio->refresh_sources();
        (void)read(tails, 60000);
        check(status(tails).at("accepted_triggers") == 1,
              "Completion ignored its Source-owned effect tail");
        (void)read(tails, 24000);
        check(status(tails).at("accepted_triggers") == 2,
              "Completion did not advance after Source-owned tail retirement");
    }
    {
        Fixture held(root);
        prepare(held, clip, 0, true, true, false, 1);
        (void)read(held, 24000);
        AudioSourceAccess::control(*held.audio, held.ref(), AudioSourceOperation::Pause);
        (void)read(held, 96000);
        check(status(held).at("accepted_triggers") == 1, "Paused Source voice completed its event");
        AudioSourceAccess::control(*held.audio, held.ref(), AudioSourceOperation::Resume);
        (void)read(held, 36000);
        check(status(held).at("accepted_triggers") == 2,
              "Resumed Source voice did not finish its event");
    }
    {
        Fixture policy(root);
        prepare(policy, clip, 0, false, false, false, 1);
        policy.audio->game_paused(true);
        auto source = policy.scene.entity("speaker").get<AudioSource>();
        source.pause_with_game = true;
        policy.scene.entity("speaker").set(source);
        policy.audio->refresh_sources();
        const auto failed = status(policy);
        check(!failed.at("repeat_running").get<bool>() &&
                  !failed.at("repeat_error").get<std::string>().empty(),
              "Changed Source pause policy left independent completion waiting forever");
        check(AudioSourceAccess::status(*policy.audio, policy.ref()).at("one_shots") == 1,
              "Producer policy failure retired a Source-owned voice");
    }
    {
        Fixture budget(root);
        prepare(budget, clip, 0);
        for (unsigned i = 0; i < 16; ++i)
            AudioSourceAccess::control(*budget.audio, budget.ref(), AudioSourceOperation::OneShot);
        auto graph = budget.scene.entity("cue").get<SoundCue>();
        graph.repeat_mode = 1;
        budget.scene.entity("cue").set(graph);
        control(budget, AudioSourceOperation::Restart);
        const auto failed = status(budget);
        check(!failed.at("repeat_running").get<bool>() &&
                  !failed.at("repeat_error").get<std::string>().empty(),
              "Admission failure started an unbounded zero-gap event loop");
        (void)read(budget, 12000);
        check(status(budget).at("accepted_triggers") == failed.at("accepted_triggers"),
              "Rejected completion event kept producing");
        AudioSourceAccess::control(*budget.audio, budget.ref(), AudioSourceOperation::Stop);
        control(budget, AudioSourceOperation::Restart);
        check(status(budget).at("repeat_running").get<bool>() &&
                  status(budget).at("repeat_error") == "",
              "Completion producer could not recover after voice admission repair");
    }
}
void run(const std::filesystem::path& root, AssetId clip) {
    {
        Fixture repeating(root), oracle(root);
        prepare(repeating, clip, .25);
        prepare(oracle, clip, 0);
        const auto authored = repeating.scene.snapshot();
        const auto tick = repeating.audio->status().at("tick");
        for (unsigned event = 0; event < 6; ++event) {
            AudioSourceAccess::control(*oracle.audio, oracle.ref(), AudioSourceOperation::OneShot);
            const auto a = read(repeating, 12000), b = read(oracle, 12000);
            check(a == b,
                  "Repeated cue PCM differs from independent sample-deadline source requests");
            check(status(repeating).at("accepted_triggers") == event + 1,
                  "Output-time cue events required or advanced a fixed game tick");
        }
        control(repeating, AudioSourceOperation::Stop);
        check(!status(repeating).at("repeat_running").get<bool>() &&
                  repeating.audio->status().at("one_shots") != 0,
              "Stopping future events took ownership of already requested Source voices");
        (void)read(repeating, 96000);
        check(status(repeating).at("accepted_triggers") == 6 && repeating.energy() == 0 &&
                  repeating.scene.snapshot() == authored &&
                  repeating.audio->status().at("tick") == tick,
              "Stopped repetition emitted again, changed authored values or advanced game time");
    }
    {
        Fixture paused(root);
        prepare(paused, clip, .25);
        (void)read(paused, 6000);
        control(paused, AudioSourceOperation::Pause);
        check(paused.energy(24000) > 0 && status(paused).at("accepted_triggers") == 1,
              "Pausing future events paused existing Source sound or emitted another event");
        control(paused, AudioSourceOperation::Resume);
        (void)read(paused, 6000);
        check(status(paused).at("accepted_triggers") == 1,
              "Resuming future events discarded their remaining interval");
        (void)read(paused, 2);
        check(status(paused).at("accepted_triggers") == 2,
              "Resumed event did not arrive at its retained output deadline");
    }
    for (unsigned domain = 0; domain < 2; ++domain) {
        Fixture held(root);
        prepare(held, clip, .25);
        (void)read(held, 6000);
        if (domain == 0)
            held.audio->paused(true);
        else
            held.audio->game_paused(true);
        check(held.energy(24000) == 0 && status(held).at("accepted_triggers") == 1,
              "Suspension or game pause advanced a pausable cue event");
        if (domain == 0)
            held.audio->paused(false);
        else
            held.audio->game_paused(false);
        (void)read(held, 6000);
        check(status(held).at("accepted_triggers") == 1, "Paused interval restarted too early");
        (void)read(held, 2);
        check(status(held).at("accepted_triggers") == 2, "Paused interval did not resume");
    }
    {
        Fixture independent(root);
        prepare(independent, clip, .25, false, false);
        independent.audio->game_paused(true);
        check(independent.energy(36000) > 0 && status(independent).at("accepted_triggers") == 3,
              "Independent repeated audio stopped with game time");
    }
    {
        Fixture bad(root);
        reject([&] { prepare(bad, clip, .25, false, true); });
        check(status(bad).at("accepted_triggers") == 0,
              "Rejected independent repetition consumed an event choice");
    }
    {
        Fixture automatic(root);
        prepare(automatic, clip, .25, true, true, true);
        check(status(automatic).at("accepted_triggers") == 1,
              "Cue startup repetition did not begin");
        control(automatic, AudioSourceOperation::Stop);
        automatic.audio->refresh_sources();
        (void)read(automatic, 36000);
        check(status(automatic).at("accepted_triggers") == 1,
              "Source synchronization restarted a manually stopped startup cue");
    }
    {
        Fixture event(root);
        prepare(event, clip, 0);
        for (auto op : {AudioSourceOperation::Restart, AudioSourceOperation::Stop,
                        AudioSourceOperation::Pause, AudioSourceOperation::Resume})
            reject([&] { control(event, op); });
        control(event, AudioSourceOperation::OneShot);
        check(status(event).at("accepted_triggers") == 1, "Event-only cue behavior changed");
    }
    {
        Fixture bounded(root);
        prepare(bounded, clip, .25);
        bounded.audio->paused(true);
        const auto graph = bounded.scene.entity("cue").get<SoundCue>();
        for (unsigned i = 1; i <= sound_cue_repeat_instance_limit; ++i) {
            const auto entity =
                authoring_command(bounded.scene, "entity.create", {{"name", "Bounded repeat"}})
                    .at("selected")
                    .get<std::string>();
            bounded.scene.entity(entity).set(graph);
            const auto ref = *bounded.engine.world().reference(bounded.scene.entity(entity).id());
            if (i == sound_cue_repeat_instance_limit)
                reject([&] {
                    AudioSourceAccess::control(*bounded.audio, ref, AudioSourceOperation::Restart);
                });
            else
                AudioSourceAccess::control(*bounded.audio, ref, AudioSourceOperation::Restart);
        }
        control(bounded, AudioSourceOperation::Restart);
        check(status(bounded).at("accepted_triggers") == 0,
              "Restarting an admitted producer at its instance limit failed or retained selection");
        control(bounded, AudioSourceOperation::Stop);
        auto changed = graph;
        changed.repeat_interval_seconds = .5;
        bounded.scene.entity("cue").set(changed);
        bounded.audio->refresh_sources();
        check(status(bounded).at("accepted_triggers") == 0 &&
                  !status(bounded).at("repeat_running").get<bool>(),
              "Changing a stopped definition retained event state");
    }
    {
        Fixture retired(root);
        prepare(retired, clip, .25);
        retired.scene.entity("speaker").remove<AudioSource>();
        retired.audio->refresh_sources();
        (void)read(retired, 12001);
        const auto failed = status(retired);
        check(!failed.at("repeat_running").get<bool>() &&
                  !failed.at("repeat_error").get<std::string>().empty(),
              "Retired Source left its event producer running without an actionable failure");
    }
    {
        Fixture unavailable(root);
        prepare(unavailable, AssetId::generate(), .25);
        check(status(unavailable).at("accepted_triggers") == 0 &&
                  !status(unavailable).at("repeat_running").get<bool>() &&
                  !status(unavailable).at("repeat_error").get<std::string>().empty(),
              "Failed Source preparation left an idle producer without a diagnostic");
        auto repaired = unavailable.scene.entity("speaker").get<AudioSource>();
        repaired.clip.id = clip;
        unavailable.scene.entity("speaker").set(repaired);
        unavailable.audio->refresh_sources();
        check(unavailable.audio->wait_for_sources(std::chrono::seconds(5)),
              "Repaired Source did not prepare");
        control(unavailable, AudioSourceOperation::Restart);
        check(status(unavailable).at("accepted_triggers") == 1 &&
                  status(unavailable).at("repeat_running").get<bool>() &&
                  status(unavailable).at("repeat_error").get<std::string>().empty(),
              "Repaired Source could not restart fresh event production");
    }
    {
        Fixture stalled(root, 1, false, {}, AudioOutput::NullDeviceTest);
        prepare(stalled, clip, .25);
        const auto start = stalled.audio->status().at("mixed_frames").get<std::uint64_t>();
        const auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (stalled.audio->status().at("mixed_frames").get<std::uint64_t>() - start < 42000) {
            check(std::chrono::steady_clock::now() < timeout, "Null output clock did not advance");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        check(status(stalled).at("accepted_triggers") == 1,
              "Output callback accessed or dispatched Scene cue state");
        stalled.audio->poll_loading();
        const auto late = status(stalled);
        check(late.at("accepted_triggers") == 2 && late.at("repeat_skipped_events") >= 2,
              "Late owner service replayed an unbounded backlog instead of one current event");
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Test path required");
        const auto root = std::filesystem::path(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        wav(root / "Assets/tone.wav");
        const auto clip = AssetCatalog::register_audio_clip(root, "Assets/tone.wav");
        validation();
        completion(root, clip.id);
        run(root, clip.id);
        std::cout
            << "Output-time repeating cue PCM/pause/lifecycle/source-ownership checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
