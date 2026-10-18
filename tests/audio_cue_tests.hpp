#pragma once
// Included with the actual sequence Runtime fixture; no replacement clock or audio runtime.
struct CueRecorder final : AudioService {
    std::vector<EntityRef> calls;
    unsigned attempts = 0;
    bool reject = false;
    void play_one_shot(EntityRef source) override {
        ++attempts;
        if (reject)
            throw std::runtime_error("Cue recorder rejected request");
        calls.push_back(source);
    }
    void play(EntityRef) override { throw std::runtime_error("Unexpected primary play"); }
    void stop_source(EntityRef) override { throw std::runtime_error("Unexpected source stop"); }
    void pause_source(EntityRef) override { throw std::runtime_error("Unexpected source pause"); }
    void resume_source(EntityRef) override { throw std::runtime_error("Unexpected source resume"); }
};
void audio_cue_identity(const std::filesystem::path& root) {
    const auto project = root / "cue-identity";
    std::filesystem::create_directories(project);
    EngineContext engine;
    Scene scene(engine.world());
    const auto create = [&](const char* name, const char* recipe, Json values = Json::object()) {
        return authoring_command(scene, "entity.create",
                                 {{"name", name}, {"recipe", recipe}, {"component_values", values}})
            .at("selected")
            .get<std::string>();
    };
    const auto controller = create("Director", "animation.sequence");
    const auto speaker = create("Speaker", "audio.source", {{"clip", AssetId::generate()}});
    const EntityRef group{scene.asset_id(), EntityId::parse(controller)};
    const EntityRef source{scene.asset_id(), EntityId::parse(speaker)};
    const auto cue =
        create("Cue", "animation.audio_cue", {{"sequence", group}, {"source", source}});
    scene.reparent_entity(speaker, controller, ReparentMode::KeepLocal);
    scene.reparent_entity(cue, controller, ReparentMode::KeepLocal);
    auto original = scene.snapshot();
    for (auto& item : original["entities"])
        if (item["id"] == cue)
            item["components"]["future.cue"] = {{"sequence", group}, {"source", source}};
    scene.restore_snapshot(original);
    const auto duplicate = scene.duplicate_subtree(controller);
    const auto copied = scene.entity(named(scene, "Cue", duplicate)).get<AudioCue>();
    check(copied.sequence == EntityRef{scene.asset_id(), EntityId::parse(duplicate)} &&
              copied.source ==
                  EntityRef{scene.asset_id(), EntityId::parse(named(scene, "Speaker", duplicate))},
          "Subtree copy did not remap both Audio Cue references");
    const auto detached = scene.duplicate_subtree(cue);
    check(scene.entity(detached).get<AudioCue>().sequence == group &&
              scene.entity(detached).get<AudioCue>().source == source,
          "Cue-only copy rebound its external references");
    const auto definition = create_prefab_source(scene, controller);
    check(definition.source.at("version") == 3, "Audio Cue Prefab omitted member links");
    PrefabLibrary library(project);
    const auto prefab = library.create(scene, definition, "AudioRig.prefab.json");
    const auto a = instantiate_prefab(scene, prefab), b = instantiate_prefab(scene, prefab);
    const auto verify = [&](const Scene& target, const std::string& actor) {
        const auto value = target.entity(named(target, "Cue", actor)).get<AudioCue>();
        check(value.sequence == EntityRef{target.asset_id(), EntityId::parse(actor)} &&
                  value.source == EntityRef{target.asset_id(),
                                            EntityId::parse(named(target, "Speaker", actor))},
              "Portable Audio Cue references crossed Prefab instances");
    };
    verify(scene, a);
    verify(scene, b);
    const auto before = scene.document();
    authoring_command(scene, "property.set_many",
                      {{"entity", cue},
                       {"component", "forge.audio_cue"},
                       {"values", {{"time_seconds", .5}, {"source", nullptr}}}});
    const auto edited = scene.document();
    scene.undo();
    check(scene.document() == before, "Cue batch edit did not undo atomically");
    scene.redo();
    check(scene.document() == edited, "Cue batch edit did not redo atomically");
    scene.undo();
    scene.save(project / "Cues.scene.json");
    EngineContext reopened_engine;
    Scene reopened(reopened_engine.world());
    library.load_scene(reopened, read_scene_file(project / "Cues.scene.json"));
    verify(reopened, a);
    verify(reopened, b);
    const Json opaque{{"sequence", group}, {"source", source}};
    check(row(reopened.document(), cue).at("components").at("future.cue") == opaque &&
              row(reopened.document(), named(reopened, "Cue", duplicate))
                      .at("components")
                      .at("future.cue") == opaque,
          "Cue copy/save interpreted opaque reference-like metadata");
}
void audio_cue_tests(const std::filesystem::path& root) {
    audio_cue_identity(root);
    EngineContext author;
    Scene scene(author.world());
    const auto create = [&](const char* name, const char* recipe, Json values = Json::object()) {
        return authoring_command(scene, "entity.create",
                                 {{"name", name}, {"recipe", recipe}, {"component_values", values}})
            .at("selected")
            .get<std::string>();
    };
    const auto controller = create("Cue sequence", "animation.sequence", {{"duration", 1}});
    const EntityRef group{scene.asset_id(), EntityId::parse(controller)};
    const auto source_a = create("A", "audio.source", {{"clip", AssetId::generate()}});
    const auto source_b = create("B", "audio.source", {{"clip", AssetId::generate()}});
    const auto source_c = create("C", "audio.source", {{"clip", AssetId::generate()}});
    const EntityRef a{scene.asset_id(), EntityId::parse(source_a)};
    const EntityRef b{scene.asset_id(), EntityId::parse(source_b)};
    const EntityRef c{scene.asset_id(), EntityId::parse(source_c)};
    const auto cue = [&](const char* name, EntityRef source, double time) {
        return create(name, "animation.audio_cue",
                      {{"sequence", group}, {"source", source}, {"time_seconds", time}});
    };
    const auto zero = cue("Entry", a, 0);
    cue("Quarter", b, .25);
    const auto end = cue("End", c, 1);
    const auto before = scene.document();
    rejects([&] {
        authoring_command(scene, "property.set",
                          {{"entity", zero},
                           {"component", "forge.audio_cue"},
                           {"field", "time_seconds"},
                           {"value", -1}});
    });
    check(scene.document() == before, "Invalid cue edit changed Scene intent/history");
    {
        Runtime runtime(root, scene.snapshot());
        const auto recorder = std::make_shared<CueRecorder>();
        runtime.engine.services().publish_audio(recorder);
        check(runtime.animation->prepare_initial_pose() && recorder->calls.empty(),
              "Initial pose preparation emitted a cue");
        runtime.simulation.tick(.25f);
        check(recorder->calls == std::vector<EntityRef>{a, b},
              "Entry and exact quarter cue boundaries differ");
        runtime.simulation.tick(.75f);
        check(recorder->calls == std::vector<EntityRef>{a, b, c},
              "Nonloop end cue did not fire once");
        runtime.simulation.tick(.25f);
        check(recorder->calls.size() == 3, "Finished sequence replayed a cue");
        AnimationPlaybackAccess::control(*runtime.animation, group, {{"operation", "restart"}});
        recorder->calls.clear();
        runtime.animation->event_delivery(false);
        runtime.simulation.tick(.25f);
        check(recorder->calls.empty(), "Copied preview emitted audio cues");
        runtime.animation->event_delivery(true);
        runtime.simulation.tick(.75f);
        check(recorder->calls == std::vector<EntityRef>{c},
              "Preview policy replayed suppressed entry or quarter cues");
        recorder->calls.clear();
        AnimationPlaybackAccess::control(*runtime.animation, group,
                                         {{"operation", "seek"}, {"position_seconds", 0}});
        check(recorder->calls.empty(), "Seeking itself emitted sound");
        AnimationPlaybackAccess::control(*runtime.animation, group, {{"operation", "play"}});
        runtime.simulation.tick(.25f);
        check(recorder->calls == std::vector<EntityRef>{b},
              "Seek replayed entry cue or lost forward crossing");
        AnimationPlaybackAccess::control(*runtime.animation, group, {{"operation", "restart"}});
        recorder->calls.clear();
        runtime.simulation.tick(.25f);
        check(recorder->calls == std::vector<EntityRef>{a, b},
              "Restart failed to rearm entry exactly once");
    }
    auto loop = scene.snapshot();
    for (auto& entity : loop["entities"])
        if (entity["id"] == controller)
            entity["components"]["forge.animation_sequence"]["loop"] = true;
    {
        Runtime initial(root, scene.snapshot());
        const auto saved = initial.animation->checkpoint();
        check(saved.at("version") == 5 && saved.at("audio_cues").size() == 3 &&
                  saved.at("sequence_entries")[0].at("entry_pending") == true,
              "Initial cue recovery omitted its configuration or unplayed entry");
        Runtime restored(root, scene.snapshot());
        const auto recorder = std::make_shared<CueRecorder>();
        restored.engine.services().publish_audio(recorder);
        restored.animation->restore(saved);
        check(recorder->calls.empty(), "Recovery itself emitted sound");
        restored.simulation.tick(.25f);
        check(recorder->calls == std::vector<EntityRef>{a, b},
              "Restored unplayed entry did not fire once at its first fixed tick");
        const auto progressed = restored.animation->checkpoint();
        auto malformed = progressed;
        malformed["sequence_entries"][0].erase("entry_pending");
        rejects([&] { restored.animation->restore(malformed); });
        check(restored.animation->checkpoint() == progressed,
              "Rejected missing cue-entry state changed playback");
        malformed = progressed;
        malformed["sequence_entries"][0]["entry_pending"] = 1;
        rejects([&] { restored.animation->restore(malformed); });
        check(restored.animation->checkpoint() == progressed,
              "Rejected nonboolean cue-entry state changed playback");
        auto legacy = progressed;
        legacy["version"] = 4;
        legacy.erase("audio_cues");
        legacy["sequence_entries"][0].erase("entry_pending");
        rejects([&] { restored.animation->restore(legacy); });
        check(restored.animation->checkpoint() == progressed,
              "Legacy recovery bypassed active cue configuration");
        Runtime replay(root, scene.snapshot());
        const auto after = std::make_shared<CueRecorder>();
        replay.engine.services().publish_audio(after);
        replay.animation->restore(progressed);
        replay.simulation.tick(.75f);
        check(after->calls == std::vector<EntityRef>{c},
              "Recovery replayed past cues or lost the future end cue");
    }
    {
        Runtime runtime(root, loop);
        runtime.simulation.tick(1.f);
        const auto saved = runtime.animation->checkpoint();
        check(saved.at("sequence_entries")[0].at("entry_pending") == false,
              "Exact-zero loop boundary rearmed the private entry flag");
        Runtime replay(root, loop);
        const auto recorder = std::make_shared<CueRecorder>();
        replay.engine.services().publish_audio(recorder);
        replay.animation->restore(saved);
        replay.simulation.reset_presentation();
        replay.simulation.tick(.25f);
        check(recorder->calls == std::vector<EntityRef>{b},
              "Restoring an exact-zero loop boundary replayed an entry cue");
    }
    {
        Runtime runtime(root, loop);
        const auto recorder = std::make_shared<CueRecorder>();
        runtime.engine.services().publish_audio(recorder);
        runtime.simulation.tick(2.25f);
        const bool zero_first = EntityId::parse(zero) < EntityId::parse(end);
        const auto first = zero_first ? a : c, second = zero_first ? c : a;
        check(recorder->calls == std::vector<EntityRef>{a, b, first, second, b, first, second, b},
              "Multi-loop cue count, time order or stable equal-time identity differs");
        runtime.simulation.tick(.75f);
        check(recorder->calls.size() == 10, "Exact loop endpoint omitted its two authored cues");
        runtime.simulation.reset_presentation();
        runtime.simulation.tick(.25f);
        check(recorder->calls.size() == 11 && recorder->calls.back() == b,
              "Exact-zero wrap or visual reset rearmed entry and duplicated sound");
        auto config = runtime.scene.entity(controller).get<AnimationSequence>();
        config.playback_speed = 0;
        runtime.scene.entity(controller).set(config);
        runtime.simulation.tick(.25f);
        check(recorder->calls.size() == 11, "Zero-speed sequence emitted sound");
        config.playback_speed = 1;
        runtime.scene.entity(controller).set(config);
        AnimationPlaybackAccess::control(*runtime.animation, group, {{"operation", "pause"}});
        runtime.simulation.tick(.25f);
        check(recorder->calls.size() == 11, "Paused sequence emitted sound");
    }
    {
        Runtime silent(root, scene.snapshot());
        silent.simulation.tick(.25f);
        near(silent.animation->playback(group)->time, .25);
    }
    auto diagnostics = [](const Runtime& runtime) {
        std::size_t count = 0;
        for (const auto& item : runtime.engine.services().diagnostics())
            count += item.at("category") == "animation.audio_cue";
        return count;
    };
    {
        Runtime runtime(root, scene.snapshot());
        const auto recorder = std::make_shared<CueRecorder>();
        runtime.engine.services().publish_audio(recorder);
        runtime.simulation.tick(.125f);
        auto config = runtime.scene.entity(controller).get<AnimationSequence>();
        config.duration = std::numeric_limits<double>::quiet_NaN();
        runtime.scene.entity(controller).set(config);
        runtime.simulation.tick(.125f);
        check(recorder->calls == std::vector<EntityRef>{a},
              "Invalid live controller emitted a cue through retained clock state");
    }
    {
        Runtime runtime(root, scene.snapshot());
        const auto saved = runtime.animation->checkpoint();
        for (const auto& field : {"source", "sequence", "time_seconds", "enabled"}) {
            auto mismatch = saved;
            auto& value = mismatch["audio_cues"][0][field];
            if (std::string_view(field) == "enabled")
                value = false;
            else if (std::string_view(field) == "time_seconds")
                value = .125;
            else
                value = nullptr;
            rejects([&] { runtime.animation->restore(mismatch); });
            check(runtime.animation->checkpoint() == saved,
                  "Mismatched cue recovery changed playback");
        }
        auto value = runtime.scene.entity(zero).get<AudioCue>();
        value.enabled = false;
        value.sequence = {};
        runtime.scene.entity(zero).set(value);
        runtime.simulation.tick(.125f);
        const auto detached = runtime.animation->checkpoint();
        check(detached.at("version") == 5 && detached.at("audio_cues").size() == 3,
              "Disabled or unbound cue intent disappeared from exact recovery");
        value.time_seconds = std::numeric_limits<double>::quiet_NaN();
        runtime.scene.entity(zero).set(value);
        runtime.simulation.tick(.125f);
        check(!runtime.animation->checkpoint_ready() && runtime.animation->checkpoint().is_null(),
              "Invalid native cue published a recoverable checkpoint");
    }
    for (double time :
         {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
          -std::numeric_limits<double>::infinity()}) {
        Runtime runtime(root, scene.snapshot());
        auto config = runtime.scene.entity(zero).get<AudioCue>();
        config.time_seconds = time;
        runtime.scene.entity(zero).set(config);
        const auto recorder = std::make_shared<CueRecorder>();
        runtime.engine.services().publish_audio(recorder);
        for (unsigned tick = 0; tick < 3; ++tick)
            runtime.simulation.tick(.125f);
        check(recorder->calls == std::vector<EntityRef>{b} && diagnostics(runtime) == 1 &&
                  runtime.animation->checkpoint().is_null(),
              "Invalid native cue flooded diagnostics, emitted sound or entered recovery");
        config.time_seconds = .5;
        runtime.scene.entity(zero).set(config);
        runtime.simulation.tick(.25f);
        check(recorder->calls == std::vector<EntityRef>{b, a},
              "Correcting an invalid native cue did not restore future crossings");
    }
    {
        Runtime runtime(root, scene.snapshot());
        auto config = runtime.scene.entity(zero).get<AudioCue>();
        config.source = {};
        runtime.scene.entity(zero).set(config);
        runtime.simulation.tick(.125f);
        const auto saved = runtime.animation->checkpoint();
        config.source.scene = runtime.scene.asset_id(); // Partial native ref cannot mean null.
        runtime.scene.entity(zero).set(config);
        runtime.simulation.tick(.125f);
        const auto time = runtime.animation->playback(group)->time;
        rejects([&] { runtime.animation->restore(saved); });
        near(runtime.animation->playback(group)->time, time);
        check(runtime.animation->checkpoint().is_null(),
              "Partial native cue reference entered recovery");
    }
    {
        auto missing = scene.snapshot();
        for (auto& entity : missing["entities"])
            if (entity["id"] == zero)
                entity["components"]["forge.audio_cue"]["source"] = nullptr;
        Runtime runtime(root, missing);
        const auto recorder = std::make_shared<CueRecorder>();
        runtime.engine.services().publish_audio(recorder);
        runtime.simulation.tick(.25f);
        check(recorder->calls == std::vector<EntityRef>{b} && diagnostics(runtime) == 1,
              "Missing cue source stopped unrelated animation/cues or lost feedback");
        for (const auto& item : runtime.engine.services().diagnostics())
            if (item.at("category") == "animation.audio_cue")
                check(!item.at("context").contains("related_entity"),
                      "Missing cue source fabricated an invalid related EntityRef");
    }
    {
        Runtime runtime(root, scene.snapshot());
        Scene other(runtime.engine.world());
        const auto foreign =
            authoring_command(
                other, "entity.create",
                {{"recipe", "audio.source"}, {"component_values", {{"clip", AssetId::generate()}}}})
                .at("selected")
                .get<std::string>();
        auto config = runtime.scene.entity(zero).get<AudioCue>();
        config.source = {other.asset_id(), EntityId::parse(foreign)};
        runtime.scene.entity(zero).set(config);
        const auto recorder = std::make_shared<CueRecorder>();
        runtime.engine.services().publish_audio(recorder);
        runtime.simulation.tick(.25f);
        check(recorder->calls == std::vector<EntityRef>{b} && diagnostics(runtime) == 1,
              "Cross-Scene source binding emitted sound or stopped unrelated cues");
    }
    {
        auto maximum = scene.snapshot();
        const auto base = row(maximum, zero);
        auto& rows = maximum["entities"];
        for (unsigned i = 0; i < 253; ++i) {
            auto extra = base;
            extra["id"] = EntityId::generate();
            extra["components"]["forge.audio_cue"]["enabled"] = false;
            rows.push_back(std::move(extra));
        }
        Runtime bounded(root, maximum);
        check(bounded.animation->checkpoint().at("audio_cues").size() == 256,
              "Maximum cue configuration was not admitted for recovery");
        auto extra = base;
        const auto extra_id = EntityId::generate();
        extra["id"] = extra_id;
        extra["components"]["forge.audio_cue"]["enabled"] = false;
        rows.push_back(std::move(extra));
        Runtime overflow(root, maximum);
        const auto recorder = std::make_shared<CueRecorder>();
        overflow.engine.services().publish_audio(recorder);
        overflow.simulation.tick(.125f);
        overflow.simulation.tick(.125f);
        check(recorder->calls.empty() && diagnostics(overflow) == 1 &&
                  !overflow.animation->checkpoint_ready(),
              "Disabled cue bypassed total component budget or overflow flooded diagnostics");
        auto legacy = bounded.animation->checkpoint();
        legacy["version"] = 3;
        legacy.erase("audio_cues");
        legacy.erase("camera_shots");
        for (auto& entry : legacy["sequence_entries"])
            entry.erase("entry_pending");
        const auto time = overflow.animation->playback(group)->time;
        rejects([&] { overflow.animation->restore(legacy); });
        near(overflow.animation->playback(group)->time, time);
        overflow.scene.delete_subtree(extra_id.str());
        AnimationPlaybackAccess::control(*overflow.animation, group, {{"operation", "restart"}});
        overflow.simulation.tick(.125f);
        check(recorder->calls == std::vector<EntityRef>{a} &&
                  overflow.animation->checkpoint().at("audio_cues").size() == 256,
              "Cue collection did not recover after reducing its component count");
    }
    {
        Runtime runtime(root, loop);
        const auto recorder = std::make_shared<CueRecorder>();
        recorder->reject = true;
        runtime.engine.services().publish_audio(recorder);
        runtime.simulation.tick(2.25f);
        check(recorder->attempts == 8 && diagnostics(runtime) == 3,
              "Rejected cue requests lost bounded per-cue feedback");
        runtime.simulation.tick(1.f);
        check(recorder->attempts == 11 && diagnostics(runtime) == 3,
              "Cue failure retried indefinitely or repeated unchanged diagnostics");
        near(runtime.animation->playback(group)->time, .25);
    }
    for (auto& entity : loop["entities"]) {
        auto& components = entity["components"];
        if (components.contains("forge.animation_sequence"))
            components["forge.animation_sequence"]["duration"] = .000001;
        if (components.contains("forge.audio_cue"))
            components["forge.audio_cue"]["time_seconds"] = 0;
    }
    {
        Runtime runtime(root, loop);
        const auto recorder = std::make_shared<CueRecorder>();
        runtime.engine.services().publish_audio(recorder);
        runtime.simulation.tick(.25f);
        runtime.simulation.tick(.25f);
        check(recorder->calls.empty() && diagnostics(runtime) == 1,
              "Cue overflow partially dispatched, looped per wrap or flooded diagnostics");
    }
}
