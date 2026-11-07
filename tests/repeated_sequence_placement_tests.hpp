#pragma once
void repeated_sequence_placement_tests(const std::filesystem::path& root, AssetId motion,
                                       AssetId pulse) {
    EngineContext author;
    Scene scene(author.world());
    const auto create = [&](const char* recipe, const char* name, Json values = Json::object()) {
        Json arguments{{"recipe", recipe}, {"name", name}};
        if (!values.empty())
            arguments["component_values"] = std::move(values);
        return authoring_command(scene, "entity.create", arguments)
            .at("selected")
            .get<std::string>();
    };
    const auto ref = [&](const std::string& id) {
        return EntityRef{scene.asset_id(), EntityId::parse(id)};
    };
    const auto parent = create("animation.sequence", "Placement owner", {{"duration", 4}});
    const Json placements = Json::array({{{"start_seconds", 2.}, {"rate", .5}, {"loop", true}},
                                         {{"start_seconds", 3.}, {"rate", 1.}, {"loop", false}}});
    const auto child = create("animation.sequence", "Repeated controller",
                              {{"duration", 1.},
                               {"sequence", ref(parent)},
                               {"sequence_start_seconds", .5},
                               {"sequence_rate", 2.},
                               {"sequence_extra_placements", placements}});
    const Json leaf_placements =
        Json::array({{{"start_seconds", .8}, {"rate", 2.}, {"loop", false}}});
    const auto grand = create("animation.sequence", "Repeated descendant",
                              {{"duration", .5},
                               {"sequence", ref(child)},
                               {"sequence_start_seconds", .25},
                               {"sequence_extra_placements", leaf_placements}});
    const auto actor = create("primitive.0", "Repeated cube");
    const auto lamp = create("render.light", "Repeated property");
    const auto fallback = create("render.camera", "Fallback camera");
    const auto camera = create("render.camera", "Placed camera", {{"enabled", false}});
    create("animation.camera_shot", "Repeated shot",
           {{"sequence", ref(grand)},
            {"camera", ref(camera)},
            {"start_seconds", 0.},
            {"end_seconds", .5}});
    TransformAnimator track{{motion}};
    track.sequence = ref(grand);
    scene.entity(actor).set(track);
    track.clip = {pulse};
    track.sequence = ref(child);
    scene.entity(lamp).set(track);
    const auto cue = [&](const char* name, const std::string& clock, double time) {
        const auto speaker = ref(create("audio.source", name, {{"clip", AssetId::generate()}}));
        create("animation.audio_cue", name,
               {{"sequence", ref(clock)}, {"source", speaker}, {"time_seconds", time}});
        return speaker;
    };
    const auto child_entry = cue("Repeated entry", child, 0);
    const auto child_end = cue("Repeated end", child, 1);
    const auto leaf_entry = cue("Descendant entry", grand, 0);
    const auto leaf_end = cue("Descendant end", grand, .5);
    const auto source = scene.snapshot();
    const auto child_config = scene.entity(child).get<AnimationSequence>();
    check(child_config.sequence_extra_placements.size() == 2,
          "Authored placements did not reach the native value");
    Runtime runtime(root, source);
    const auto seek = [&](Runtime& target, double time) {
        AnimationPlaybackAccess::control(*target.animation, ref(parent),
                                         {{"operation", "seek"}, {"position_seconds", time}});
    };
    const auto local = [](double time) {
        if (time < .5)
            return 0.;
        if (time < 2)
            return std::min(1., (time - .5) * 2);
        if (time < 3)
            return (time - 2) * .5;
        return std::min(1., time - 3);
    };
    const auto leaf = [](double time) {
        if (time < .625)
            return 0.;
        if (time < .9)
            return std::min(.5, (time - .625) * 2);
        if (time < 2)
            return std::min(.4, (time - .9) * 4);
        if (time < 2.5)
            return 0.;
        if (time < 3)
            return (time - 2.5) * .5;
        if (time < 3.25)
            return 0.;
        if (time < 3.8)
            return std::min(.5, time - 3.25);
        return std::min(.4, (time - 3.8) * 2);
    };
    for (const double time : {0., .4, .5, .65, .9, .95, 1.2, 1.99, 2., 2.25, 2.6, 2.99, 3., 3.1,
                              3.4, 3.79, 3.8, 3.9, 4.}) {
        seek(runtime, time);
        const double phase = local(time), descendant = leaf(time);
        const auto status = runtime.animation->playback(ref(child));
        near(status->time, phase);
        near(runtime.animation->playback(ref(grand))->time, descendant);
        near(runtime.scene.entity(actor).get<LocalTranslation>().x, 4 * descendant);
        near(runtime.scene.entity(lamp).get<Light>().intensity, 1 + 2 * phase);
        near(status->speed, time < 2 ? 2 : time < 3 ? .5 : 1);
        const auto placement_status =
            AnimationPlaybackAccess::status(*runtime.animation, ref(child))
                .at("sequence_placements");
        check(placement_status.at("active_placement") == (time < 2   ? 0
                                                          : time < 3 ? 1
                                                                     : 2) &&
                  placement_status.at("placements").size() == 3 &&
                  placement_status.at("placements")[1].at("rate") == .5,
              "Live controller status lost the admitted list or selected placement");
        check(status->loop == (time >= 2 && time < 3),
              "Observed loop did not follow the selected placement");
        check(status->finished == (time == 4),
              "A held early placement incorrectly finished the repeated controller");
        if (time == 4)
            check(!runtime.animation->playback(ref(grand))->finished,
                  "A parent-clipped descendant falsely completed its local duration");
        const auto render = extract_render_scene(runtime.simulation.presentation(1));
        const bool shot_active = (time >= .625 && time <= .875) || (time >= .9 && time <= 1) ||
                                 (time >= 2.5 && time < 3) || (time >= 3.25 && time <= 3.75) ||
                                 (time >= 3.8 && time <= 4);
        check(render.cameras.size() == 1 &&
                  render.cameras.front().entity == EntityId::parse(shot_active ? camera : fallback),
              "Repeated camera windows disagree with the independent schedule");
    }
    seek(runtime, 2.9);
    runtime.simulation.tick(.2); // Cross an explicit placement reset.
    for (const double alpha : {0., .25, .75, 1.}) {
        const auto frame = runtime.simulation.presentation(alpha);
        near(row(frame, lamp).at("components").at("forge.light").at("intensity"), 1.2);
        near(row(frame, actor).at("components").at("forge.local_translation").at("x"), 0);
    }
    const auto checkpoint = runtime.animation->checkpoint();
    check(checkpoint.at("version") == 14 && checkpoint.at("sequence_placement_profile") == 11 &&
              checkpoint.at("sequence_rate_profile") == 7,
          "Repeated placements did not wrap the existing recovery route");
    Runtime cold(root, source, false);
    cold.animation->restore(checkpoint);
    check(cold.animation->checkpoint() == checkpoint,
          "Fresh repeated-placement recovery changed exact clocks/configuration");
    near(cold.scene.entity(lamp).get<Light>().intensity, 1.2);
    const auto unchanged = cold.animation->checkpoint();
    auto malformed = checkpoint;
    malformed["sequence_placement_profile"] = 14;
    rejects([&] { cold.animation->restore(malformed); });
    check(cold.animation->checkpoint() == unchanged, "Malformed placement wrapper partly adopted");
    malformed = checkpoint;
    for (auto& entry : malformed["sequence_entries"])
        if (entry.at("entity") == Json(ref(child)))
            entry["sequence_extra_placements"][0]["rate"] = 1.;
    rejects([&] { cold.animation->restore(malformed); });
    check(cold.animation->checkpoint() == unchanged, "Changed placement recovery partly adopted");
    malformed = checkpoint;
    malformed["version"] = malformed["sequence_placement_profile"];
    malformed.erase("sequence_placement_profile");
    rejects([&] { cold.animation->restore(malformed); });
    check(cold.animation->checkpoint() == unchanged, "Missing placement profile partly adopted");
    auto overlap = child_config;
    overlap.sequence_extra_placements[0].start_seconds = .75;
    runtime.scene.entity(child).set(overlap);
    rejects([&] { runtime.animation->prepare_initial_pose(); });
    near(runtime.scene.entity(lamp).get<Light>().intensity, 1.2);
    runtime.scene.entity(child).set(child_config);
    check(runtime.animation->prepare_initial_pose(), "Valid repeated-placement repair failed");
    near(runtime.scene.entity(lamp).get<Light>().intensity, 1.2);
    const auto before = scene.document();
    auto changed = placements;
    changed[1]["start_seconds"] = 3.25;
    authoring_command(scene, "property.set_many",
                      {{"entity", child},
                       {"component", "forge.animation_sequence"},
                       {"values", {{"sequence_extra_placements", changed}}}});
    const auto after = scene.document();
    check(scene.undo() && scene.document() == before && scene.redo() && scene.document() == after,
          "Repeated placement edits did not share the existing Scene history");
    for (const Json& invalid :
         {Json(1), Json::object(),
          Json::array({{{"start_seconds", 2.}, {"rate", 1.}, {"loop", 1}}}),
          Json::array({{{"start_seconds", .75}, {"rate", 1.}, {"loop", false}}})}) {
        rejects([&] {
            authoring_command(scene, "property.set_many",
                              {{"entity", child},
                               {"component", "forge.animation_sequence"},
                               {"values", {{"sequence_extra_placements", invalid}}}});
        });
        check(scene.document() == after, "Invalid authored placement partially changed the Scene");
    }
    const std::array<std::pair<double, EntityRef>, 12> points{{{.5, child_entry},
                                                               {.625, leaf_entry},
                                                               {.875, leaf_end},
                                                               {.9, leaf_entry},
                                                               {1, child_end},
                                                               {2, child_entry},
                                                               {2.5, leaf_entry},
                                                               {3, child_entry},
                                                               {3.25, leaf_entry},
                                                               {3.75, leaf_end},
                                                               {3.8, leaf_entry},
                                                               {4, child_end}}};
    const auto expected = [&](double end, bool looping) {
        std::vector<EntityRef> result;
        for (unsigned cycle = 0; cycle <= (looping ? unsigned(end / 4) : 0u); ++cycle)
            for (const auto& [time, speaker] : points)
                if (cycle * 4 + time <= end)
                    result.push_back(speaker);
        return result;
    };
    Runtime segmented(root, source);
    const auto calls = std::make_shared<CueRecorder>();
    segmented.engine.services().publish_audio(calls);
    double end = 0;
    for (const double step : {.25, .125, .125, .5, 1., 1., 1.}) {
        segmented.simulation.tick(step);
        end += step;
        check(calls->calls == expected(end, false),
              "Repeated cue inverse lost finite endpoints, clipped spans, gaps or rate changes");
    }
    auto loop_source = source;
    for (auto& entry : loop_source["entities"])
        if (entry.at("id") == parent)
            entry["components"]["forge.animation_sequence"]["loop"] = true;
    Runtime whole(root, loop_source);
    const auto many = std::make_shared<CueRecorder>();
    whole.engine.services().publish_audio(many);
    whole.simulation.tick(8.25);
    check(many->calls == expected(8.25, true),
          "Repeated placements lost cues across multiple root cycles");
    const auto audible = whole.animation->checkpoint();
    Runtime recovered(root, loop_source, false);
    const auto after_recovery = std::make_shared<CueRecorder>();
    recovered.engine.services().publish_audio(after_recovery);
    recovered.animation->restore(audible);
    check(after_recovery->calls.empty(), "Repeated-placement recovery replayed audio");
    recovered.simulation.tick(.75);
    auto continuation = expected(9, true);
    continuation.erase(continuation.begin(), continuation.begin() + many->calls.size());
    check(after_recovery->calls == continuation,
          "Repeated-placement recovered cue continuation differs");
    Runtime overflow(root, loop_source);
    const auto bounded = std::make_shared<CueRecorder>();
    overflow.engine.services().publish_audio(bounded);
    overflow.simulation.tick(64);
    check(bounded->calls.empty(), "Repeated cue overflow submitted a partial batch");
    std::cout << "Repeated native/source placements, descendant phases, camera/status, copied "
                 "reset, exact fresh recovery, atomic repair and history passed\n";
}
