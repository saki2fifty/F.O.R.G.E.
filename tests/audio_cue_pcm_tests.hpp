#pragma once
// Uses the real offline AudioRuntime and fixed simulation from animation_audio_tests.
void audio_cue_pcm(const std::filesystem::path& root) {
    tone(root / "Assets/CueTone.wav");
    const auto sound = AssetCatalog::register_audio_clip(root, "Assets/CueTone.wav");
    const auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    EngineContext author;
    Scene scene(author.world());
    const auto create = [&](const char* name, const char* recipe, Json values) {
        return authoring_command(scene, "entity.create",
                                 {{"name", name}, {"recipe", recipe}, {"component_values", values}})
            .at("selected")
            .get<std::string>();
    };
    const auto controller = create("Sound sequence", "animation.sequence", {{"duration", 1}});
    const auto speaker = create("Cue speaker", "audio.source",
                                {{"clip", sound.id},
                                 {"play_on_start", false},
                                 {"loop", false},
                                 {"spatialized", false},
                                 {"gain", .5}});
    const EntityRef group{scene.asset_id(), EntityId::parse(controller)};
    const EntityRef emitter{scene.asset_id(), EntityId::parse(speaker)};
    for (const auto time : {0., .5})
        create("Sound point", "animation.audio_cue",
               {{"sequence", group}, {"source", emitter}, {"time_seconds", time}});
    const auto saved = scene.snapshot();
    Running run(root);
    run.load(saved, catalog);
    check(run.animation->prepare_initial_pose(), "Cue-only sequence did not admit");
    run.simulation.audio_paused(false);
    check(run.audio->status().at("one_shots") == 0, "Initial cue pose started audio");
    // The first fixed tick requests sound before AudioRuntime has admitted/decoded this source.
    run.simulation.tick(.125f);
    check(run.audio->wait_for_sources(std::chrono::seconds(5)), "Cue PCM did not load");
    check(run.audio->status().at("one_shots") == 1 &&
              !AudioSourceAccess::status(*run.audio, emitter).at("playing").get<bool>(),
          "Sequence cue did not start exactly one effect independently of primary playback");
    (void)run.signal(14400);
    const auto signal = run.signal();
    check(signal.energy > .0001 && std::abs(signal.frequency - 440) < 15,
          "Sequence cue did not produce expected real tone PCM");
    run.simulation.audio_paused(true);
    check(run.signal().energy == 0, "Paused cue voice leaked PCM");
    run.simulation.audio_paused(false);
    check(run.signal().energy > .0001, "Cue voice did not resume");
    run.simulation.tick(.375f);
    check(run.audio->status().at("one_shots") == 2,
          "Second cue did not overlap through the existing voice owner");
    const auto checkpoint = run.animation->checkpoint();
    AnimationPlaybackAccess::control(*run.animation, group,
                                     {{"operation", "seek"}, {"position_seconds", .5}});
    run.simulation.sync_audio();
    check(run.audio->status().at("one_shots") == 2,
          "Seeking added a cue or stopped an already requested effect");
    Running restored(root);
    restored.load(saved, catalog);
    restored.ready();
    restored.animation->restore(checkpoint);
    restored.simulation.tick(.25f);
    check(restored.audio->status().at("one_shots") == 0 && restored.signal().energy == 0,
          "Semantic recovery replayed previous sound effects");
    AnimationPlaybackAccess::control(*restored.animation, group, {{"operation", "restart"}});
    restored.simulation.tick(.125f);
    check(restored.audio->status().at("one_shots") == 1,
          "Restart did not request an effect through the recovered owner");
    AudioSourceAccess::control(*restored.audio, emitter, AudioSourceOperation::Stop);
    check(restored.audio->status().at("one_shots") == 0 && restored.signal().energy == 0,
          "Source Stop did not retire a sequence-requested effect");
    Running pending(root);
    pending.load(saved, catalog);
    pending.simulation.audio_paused(false);
    pending.simulation.tick(.125f);
    pending.audio->stop_source(emitter);
    pending.simulation.sync_audio();
    check(pending.audio->wait_for_sources(std::chrono::seconds(5)) &&
              pending.audio->status().at("one_shots") == 0 && pending.signal().energy == 0,
          "Source Stop lost its ordering behind an asynchronously loaded cue");
    AnimationPlaybackAccess::control(*pending.animation, group, {{"operation", "restart"}});
    pending.animation->tick(.125f, 2);
    pending.scene.delete_subtree(speaker);
    pending.simulation.sync_audio();
    check(pending.audio->status().at("one_shots") == 0 && pending.signal().energy == 0,
          "Deleted source retained a queued sequence effect");
    check(scene.snapshot() == saved, "Cue playback changed authored Scene intent");
    AssetId automation;
    for (const auto& [id, record] : catalog->records())
        if (record.source == "Assets/Audio.animation.json")
            automation = id;
    check(bool(automation), "Missing real typed-animation candidate fixture");
    EngineContext candidate_engine;
    Scene candidate(candidate_engine.world());
    candidate.restore_snapshot(saved);
    const auto faulty =
        authoring_command(
            candidate, "entity.create",
            {{"recipe", "audio.source"},
             {"component_values",
              {{"clip", sound.id}, {"play_on_start", false}, {"spatialized", false}}}})
            .at("selected")
            .get<std::string>();
    candidate.entity(faulty).set(TransformAnimator{{automation}});
    Running blocked(root);
    blocked.load(candidate.snapshot(), catalog);
    blocked.ready();
    auto invalid = blocked.scene.entity(faulty).get<AudioSource>();
    invalid.outer_angle = 1; // Incompatible with its retained inner cone angle.
    blocked.scene.entity(faulty).set(invalid);
    blocked.simulation.tick(.125f);
    check(blocked.audio->status().at("one_shots") == 0 && blocked.signal().energy == 0 &&
              !blocked.animation->checkpoint_ready(),
          "A rejected complete animation candidate still submitted a cue effect");
    invalid.outer_angle = 120;
    blocked.scene.entity(faulty).set(invalid);
    blocked.simulation.tick(.375f);
    check(blocked.audio->status().at("one_shots") == 1,
          "Correcting a rejected candidate did not restore future audio cue delivery");
    std::cout << "Sequence cue PCM energy=" << signal.energy << ", frequency=" << signal.frequency
              << '\n';
}
