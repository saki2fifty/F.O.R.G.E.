#pragma once
void nested_audio_cue_tests(const std::filesystem::path& root) {
    EngineContext author;
    Scene scene(author.world());
    auto create = [&](const char* name, const char* recipe, Json values = Json::object()) {
        return authoring_command(scene, "entity.create",
                                 {{"name", name}, {"recipe", recipe}, {"component_values", values}})
            .at("selected")
            .get<std::string>();
    };
    auto ref = [&](const std::string& id) {
        return EntityRef{scene.asset_id(), EntityId::parse(id)};
    };
    const auto parent =
        create("Root cue clock", "animation.sequence", {{"duration", 2}, {"loop", true}});
    const auto child = create("Child cues", "animation.sequence",
                              {{"duration", 1},
                               {"sequence", ref(parent)},
                               {"sequence_start_seconds", .5},
                               {"play_on_start", false},
                               {"loop", true},
                               {"playback_speed", 0}});
    const auto grand =
        create("Grandchild cues", "animation.sequence",
               {{"duration", .5}, {"sequence", ref(child)}, {"sequence_start_seconds", .25}});
    auto speaker = [&](const char* name) {
        return ref(create(name, "audio.source", {{"clip", AssetId::generate()}}));
    };
    const auto a = speaker("Child entry");
    const auto b = speaker("Child end");
    const auto c = speaker("Grandchild entry");
    const auto d = speaker("Grandchild end");
    const auto e = speaker("Root entry");
    const auto cue = [&](const char* name, const std::string& controller, EntityRef source,
                         double time) {
        return create(name, "animation.audio_cue",
                      {{"sequence", ref(controller)}, {"source", source}, {"time_seconds", time}});
    };
    cue("A", child, a, 0);
    const auto end_child = cue("B", child, b, 1);
    cue("C", grand, c, 0);
    const auto end_grand = cue("D", grand, d, .5);
    cue("E", parent, e, 0);
    const auto source = scene.snapshot();
    Runtime runtime(root, source);
    auto recorder = std::make_shared<CueRecorder>();
    runtime.engine.services().publish_audio(recorder);
    runtime.simulation.tick(.25);
    check(recorder->calls == std::vector<EntityRef>{e}, "Nested cue fired before its placement");
    runtime.simulation.tick(.25);
    check(recorder->calls == std::vector<EntityRef>{e, a},
          "Nested zero-time cue missed exact entry");
    runtime.simulation.tick(.25);
    check(recorder->calls == std::vector<EntityRef>{e, a, c},
          "Grandchild entry lost its accumulated offset");
    runtime.simulation.tick(.5);
    check(recorder->calls == std::vector<EntityRef>{e, a, c, d},
          "Grandchild final cue missed its clipped interval");
    runtime.simulation.tick(.25);
    runtime.simulation.tick(.5);
    check(recorder->calls == std::vector<EntityRef>{e, a, c, d, b, e},
          "Nested final cue or root wrap entry differs");
    AnimationPlaybackAccess::control(*runtime.animation, ref(parent), {{"operation", "pause"}});
    runtime.simulation.tick(.5);
    check(recorder->calls.size() == 6, "Paused parent emitted nested cues");
    AnimationPlaybackAccess::control(*runtime.animation, ref(parent),
                                     {{"operation", "seek"}, {"position_seconds", 1.25}});
    check(recorder->calls.size() == 6, "Nested seek replayed crossed cues");
    const auto checkpoint = runtime.animation->checkpoint();
    Runtime recovered(root, source, false);
    const auto after = std::make_shared<CueRecorder>();
    recovered.engine.services().publish_audio(after);
    recovered.animation->restore(checkpoint);
    check(after->calls.empty(), "Nested recovery replayed cue entry");
    for (auto* world : {&runtime, &recovered}) {
        AnimationPlaybackAccess::control(*world->animation, ref(parent), {{"operation", "play"}});
        world->simulation.tick(.25);
    }
    check(after->calls == std::vector<EntityRef>{b} && recorder->calls.back() == b,
          "Recovered nested end cue differs from uninterrupted playback");
    AnimationPlaybackAccess::control(*recovered.animation, ref(parent), {{"operation", "restart"}});
    recovered.simulation.tick(.25);
    check(after->calls == std::vector<EntityRef>{b, e},
          "Root restart lost or duplicated nested entry intent");
    recovered.simulation.tick(.25);
    check(after->calls == std::vector<EntityRef>{b, e, a},
          "Restarted child entry did not follow root time");
    auto repeated = source;
    for (auto& row : repeated["entities"]) {
        const auto id = row.at("id").get<std::string>();
        auto& components = row["components"];
        if (id == parent)
            components["forge.animation_sequence"]["duration"] = .25;
        if (id == child) {
            components["forge.animation_sequence"]["duration"] = .1;
            components["forge.animation_sequence"]["sequence_start_seconds"] = .05;
        }
        if (id == grand) {
            components["forge.animation_sequence"]["duration"] = .05;
            components["forge.animation_sequence"]["sequence_start_seconds"] = .025;
        }
        if (id == end_child)
            components["forge.audio_cue"]["time_seconds"] = .1;
        if (id == end_grand)
            components["forge.audio_cue"]["time_seconds"] = .05;
    }
    Runtime multiple(root, repeated);
    const auto many = std::make_shared<CueRecorder>();
    multiple.engine.services().publish_audio(many);
    multiple.simulation.tick(.75);
    check(many->calls == std::vector<EntityRef>{e, a, c, d, b, e, a, c, d, b, e, a, c, d, b, e},
          "Nested cue delivery lost multiple root periods within one tick");
    auto clipped = source;
    for (auto& row : clipped["entities"])
        if (row.at("id") == parent) {
            row["components"]["forge.animation_sequence"]["duration"] = 1;
            row["components"]["forge.animation_sequence"]["loop"] = false;
        }
    Runtime shortened(root, clipped);
    const auto clipped_calls = std::make_shared<CueRecorder>();
    shortened.engine.services().publish_audio(clipped_calls);
    shortened.simulation.tick(2);
    check(clipped_calls->calls == std::vector<EntityRef>{e, a, c},
          "Nested cues after an ancestor end escaped truncation");
    std::cout << "Nested cues: exact offset entry/end, root wrap/multiple periods, pause/seek, "
                 "cold recovery and restart passed\n";
}
