#include "audio_source_access.hpp"
#include "audio_test_fixture.hpp"
#include <forge/sound_cue.hpp>

namespace {
void control(Fixture& f, AudioSourceOperation op) {
    AudioSourceAccess::control(*f.audio, f.ref("cue"), op);
}
Json status(Fixture& f) { return AudioSourceAccess::status(*f.audio, f.ref("cue")); }
void prepare(Fixture& f, AssetId clip, unsigned mode, unsigned limit, bool fast_first = false) {
    auto doc = source(clip);
    auto first = doc["entities"][0];
    auto& sound = first["components"]["forge.audio_source"];
    sound["play_on_start"] = sound["loop"] = false;
    sound["gain"] = .25f;
    sound["pitch"] = fast_first ? 2.f : .5f;
    auto second = first;
    second["id"] = "second";
    second["components"]["forge.audio_source"]["pitch"] = fast_first ? .5f : 2.f;
    auto manual = first;
    manual["id"] = "manual";
    manual["components"]["forge.audio_source"]["pitch"] = .25f;
    doc["entities"] = Json::array({first,
                                   second,
                                   manual,
                                   {{"id", "cue"},
                                    {"name", "Finite run"},
                                    {"components", {{"forge.sound_cue", sound_cue_json({})}}}}});
    f.load(doc);
    SoundCue cue{
        true, 1, 0, {{3, {}, {{1, 1}, {2, 1}}}, {0, f.ref(), {}}, {0, f.ref("second"), {}}}};
    cue.repeat_mode = mode;
    cue.repeat_interval_seconds = mode ? 0 : .25;
    cue.repeat_event_limit = limit;
    f.scene.entity("cue").set(cue);
    f.audio->paused(false);
}
void values() {
    SoundCue cue;
    auto legacy = sound_cue_json(cue);
    legacy.erase("repeat_event_limit");
    check(sound_cue_from_json(legacy).repeat_event_limit == 0,
          "Older cue values did not retain unlimited runs");
    cue.repeat_event_limit = sound_cue_event_limit_max;
    check(sound_cue_from_json(sound_cue_json(cue)) == cue, "Finite cue value roundtrip changed");
    ++cue.repeat_event_limit;
    reject([&] { (void)SoundCueProgram::compile(cue); });
    for (const Json& invalid : {Json(true), Json(-1), Json(1.), Json(65536), Json("2")}) {
        legacy["repeat_event_limit"] = invalid;
        reject([&] { (void)sound_cue_from_json(legacy); });
    }
}
void interval_pcm(const std::filesystem::path& root, AssetId clip) {
    Fixture actual(root), oracle(root);
    prepare(actual, clip, 0, 2);
    prepare(oracle, clip, 0, 2);
    control(actual, AudioSourceOperation::Restart);
    AudioSourceAccess::control(*oracle.audio, oracle.ref(), AudioSourceOperation::OneShot);
    for (auto* f : {&actual, &oracle})
        AudioSourceAccess::control(*f->audio, f->ref("manual"), AudioSourceOperation::OneShot);
    double energy = 0;
    for (unsigned frame = 0; frame < 120000; frame += 240) {
        if (frame == 12000)
            AudioSourceAccess::control(*oracle.audio, oracle.ref("second"),
                                       AudioSourceOperation::OneShot);
        std::array<float, 480> a{}, b{};
        actual.audio->read_offline(a);
        oracle.audio->read_offline(b);
        for (unsigned i = 0; i < a.size(); ++i) {
            check(std::isfinite(a[i]) && std::abs(a[i] - b[i]) < 1e-6f,
                  "Finite interval output differs from independent two-event ordinary PCM");
            energy += double(a[i]) * a[i];
        }
        if (frame == 48000) {
            const auto live = status(actual);
            check(live.at("repeat_issued_events") == 2 && live.at("repeat_finishing") &&
                      !live.at("repeat_finished") && live.at("repeat_run_voices") == 1,
                  "Finite run waited only for its last event instead of the earlier long voice");
        }
    }
    const auto done = status(actual);
    check(energy > .01 && done.at("repeat_finished") && !done.at("repeat_running") &&
              done.at("accepted_triggers") == 2 && done.at("repeat_run_voices") == 0 &&
              actual.audio->status().at("one_shots") == 1,
          "Finite interval completion counted unrelated manual sound or emitted extra events");
}
void completion_controls(const std::filesystem::path& root, AssetId clip) {
    Fixture f(root);
    prepare(f, clip, 1, 1);
    control(f, AudioSourceOperation::Restart);
    check(status(f).at("repeat_finishing") && status(f).at("repeat_issued_events") == 1,
          "One-event completion run did not enter finishing after its first request");
    control(f, AudioSourceOperation::Pause);
    (void)f.energy(120000);
    check(!status(f).at("repeat_finished") && status(f).at("repeat_paused"),
          "Paused producer reported completion before its owner could observe it");
    control(f, AudioSourceOperation::Resume);
    check(status(f).at("repeat_finished") && status(f).at("repeat_completed_events") == 1,
          "Finite completion run added a final gap or did not resume completion observation");
    control(f, AudioSourceOperation::Restart);
    control(f, AudioSourceOperation::Stop);
    check(!status(f).at("repeat_finished") && !status(f).at("repeat_running") &&
              f.audio->status().at("one_shots") == 1,
          "Cue Stop reported natural completion or took ownership of its Source voice");
}
void inherited_audio(const std::filesystem::path& root, AssetId clip) {
    Fixture f(root);
    prepare(f, clip, 1, 1);
    const auto listener = authoring_command(f.scene, "entity.create",
                                            {{"recipe", "audio.listener"},
                                             {"position", {{"x", 0}, {"y", 0}, {"z", 0}}}})
                              .at("selected")
                              .get<std::string>();
    auto& world = f.engine.world().world();
    auto sound = f.scene.entity("speaker").get<AudioSource>();
    sound.spatialized = true;
    const auto sound_base = world.prefab().set<AudioSource>(sound);
    const auto listener_base = world.prefab().set<AudioListener>({true});
    auto cue = f.scene.entity("cue").get<SoundCue>();
    cue.repeat_on_start = true;
    const auto cue_base = world.prefab().set<SoundCue>(cue);
    f.scene.entity("speaker").remove<AudioSource>().add(flecs::IsA, sound_base);
    f.scene.entity(listener).remove<AudioListener>().add(flecs::IsA, listener_base);
    f.scene.entity("cue").remove<SoundCue>().add(flecs::IsA, cue_base);
    f.audio->synchronize(0);
    check(f.audio->wait_for_sources(std::chrono::seconds(5)), "Inherited audio failed preparation");
    check(f.energy() > 1e-5 && status(f).at("accepted_triggers") == 1 &&
              !f.scene.entity("speaker").owns<AudioSource>() &&
              !f.scene.entity(listener).owns<AudioListener>() &&
              !f.scene.entity("cue").owns<SoundCue>(),
          "Inherited Source/listener/cue discovery was silent or materialized authored values");
}
void restart_isolation(const std::filesystem::path& root, AssetId clip) {
    Fixture f(root);
    prepare(f, clip, 0, 2, true);
    control(f, AudioSourceOperation::Restart);
    (void)f.energy(24000);
    f.audio->poll_loading();
    auto cue = f.scene.entity("cue").get<SoundCue>();
    cue.repeat_event_limit = 1;
    f.scene.entity("cue").set(cue);
    f.audio->refresh_sources();
    control(f, AudioSourceOperation::Restart);
    (void)f.energy(36000);
    f.audio->poll_loading();
    check(status(f).at("repeat_finished") && status(f).at("repeat_issued_events") == 1 &&
              f.audio->status().at("one_shots") == 1,
          "Older definition/run voices aliased the restarted finite run's completion");
}
void finishing_controls(const std::filesystem::path& root, AssetId clip) {
    for (unsigned domain = 0; domain < 3; ++domain) {
        Fixture f(root);
        prepare(f, clip, 0, 1, true);
        control(f, AudioSourceOperation::Restart);
        if (domain == 0)
            f.audio->paused(true);
        else if (domain == 1)
            f.audio->game_paused(true);
        else
            AudioSourceAccess::control(*f.audio, f.ref(), AudioSourceOperation::Pause);
        check(f.energy(60000) == 0 && !status(f).at("repeat_finished"),
              "Suspended/game-paused/Source-paused finite run completed early");
        if (domain == 0)
            f.audio->paused(false);
        else if (domain == 1)
            f.audio->game_paused(false);
        else
            AudioSourceAccess::control(*f.audio, f.ref(), AudioSourceOperation::Resume);
        (void)f.energy(48000);
        f.audio->poll_loading();
        check(status(f).at("repeat_finished"), "Finite finishing did not resume with its owner");
    }
    for (unsigned mode : {0u, 1u}) {
        Fixture f(root);
        prepare(f, clip, mode, 1);
        for (unsigned i = 0; i < 16; ++i)
            AudioSourceAccess::control(*f.audio, f.ref(), AudioSourceOperation::OneShot);
        control(f, AudioSourceOperation::Restart);
        const auto failed = status(f);
        check(!failed.at("repeat_running") && !failed.at("repeat_finished") &&
                  !failed.at("repeat_error").get<std::string>().empty(),
              "Rejected finite Source admission reported successful completion");
        AudioSourceAccess::control(*f.audio, f.ref(), AudioSourceOperation::Stop);
        control(f, AudioSourceOperation::Restart);
        check(status(f).at("repeat_running") && status(f).at("repeat_error") == "",
              "Finite run did not recover after Source voice-budget repair");
        AudioSourceAccess::control(*f.audio, f.ref(), AudioSourceOperation::Stop);
        f.audio->poll_loading();
        check(status(f).at("repeat_finished"),
              "Source Stop did not retire the final finite event through its existing owner");
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Scratch required");
        values();
        const auto root = std::filesystem::path(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        wav(root / "Assets/tone.wav");
        const auto clip = AssetCatalog::register_audio_clip(root, "Assets/tone.wav");
        interval_pcm(root, clip.id);
        completion_controls(root, clip.id);
        inherited_audio(root, clip.id);
        restart_isolation(root, clip.id);
        finishing_controls(root, clip.id);
        std::cout << "Finite cue values, ordinary PCM, overlapping completion, controls and "
                     "restart isolation passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
