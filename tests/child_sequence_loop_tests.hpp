#pragma once
// Real Scene, source authoring, imported clips and AnimationRuntime fixtures.
void child_sequence_loop_tests(const std::filesystem::path& root, AssetId motion, AssetId pulse) {
    EngineContext author;
    Scene scene(author.world());
    const auto create = [&](const char* recipe, const char* name) {
        return authoring_command(scene, "entity.create", {{"recipe", recipe}, {"name", name}})
            .at("selected")
            .get<std::string>();
    };
    const auto parent = create("animation.sequence", "Loop owner");
    const auto child = create("animation.sequence", "Repeating child");
    const auto grand = create("animation.sequence", "Finite descendant");
    const auto actor = create("primitive.0", "Looped cube");
    const auto lamp = create("primitive.0", "Looped property");
    const auto fallback = create("render.camera", "Default view");
    const auto shot_camera = create("render.camera", "Repeated view");
    const auto shot = create("animation.camera_shot", "Repeated shot");
    const auto ref = [&](const std::string& id) {
        return EntityRef{scene.asset_id(), EntityId::parse(id)};
    };
    AnimationSequence outer;
    outer.duration = 4.75;
    // Ignored on a root; standalone loop still controls root playback.
    outer.sequence_loop = true;
    scene.entity(parent).set(outer);
    AnimationSequence inner;
    inner.duration = 1;
    inner.sequence = ref(parent);
    inner.sequence_start_seconds = .5;
    inner.sequence_rate = 2;
    inner.sequence_loop = true;
    inner.loop = false;
    inner.playback_speed = 0;
    inner.play_on_start = false;
    scene.entity(child).set(inner);
    AnimationSequence leaf;
    leaf.duration = .5;
    leaf.sequence = ref(child);
    leaf.sequence_start_seconds = .25;
    scene.entity(grand).set(leaf);
    TransformAnimator track{{motion}};
    track.sequence = ref(grand);
    scene.entity(actor).set(track).set<LocalTranslation>({99, 0, 0});
    track.clip = {pulse};
    track.sequence = ref(child);
    scene.entity(lamp).set(track).set(Light{});
    auto camera = scene.entity(shot_camera).get<Camera>();
    camera.enabled = false;
    scene.entity(shot_camera).set(camera);
    scene.entity(shot).set(CameraShot{ref(grand), ref(shot_camera), 0, .5, true});
    const auto source = scene.snapshot();
    Runtime runtime(root, source);
    const auto seek = [&](Runtime& world, double time) {
        AnimationPlaybackAccess::control(*world.animation, ref(parent),
                                         {{"operation", "seek"}, {"position_seconds", time}});
    };
    const auto position = [&](const Runtime& world) {
        return world.scene.entity(actor).get<LocalTranslation>().x;
    };
    const auto selected_camera = [&](Runtime& world, double alpha = 1) {
        const auto render = extract_render_scene(world.simulation.presentation(alpha));
        check(render.cameras.size() == 1, "Looped camera composition is ambiguous");
        return render.cameras.front().entity;
    };
    for (const double time :
         {0., .4, .5, .6, .625, .75, .875, .99, 1., 1.1, 1.25, 1.4, 1.5, 2.75, 3.99, 4.5, 4.75}) {
        seek(runtime, time);
        const double phase = time < .5 ? 0 : std::fmod((time - .5) * 2, 1.);
        const double local = std::clamp(phase - .25, 0., .5);
        near(runtime.animation->playback(ref(child))->time, phase);
        near(runtime.animation->playback(ref(grand))->time, local);
        near(position(runtime), 4 * local);
        near(runtime.scene.entity(lamp).get<Light>().intensity, 1 + 2 * phase);
        check(selected_camera(runtime) == EntityId::parse(time >= .5 && phase >= .25 && phase <= .75
                                                              ? shot_camera
                                                              : fallback),
              "Looped shot ignored a descendant's entry gap or active span");
    }
    seek(runtime, 1.4);
    check(runtime.animation->playback(ref(child))->loop &&
              !runtime.animation->playback(ref(grand))->loop &&
              !runtime.animation->control(ref(child), AnimationControl::Pause, 10) &&
              !runtime.animation->seek(ref(child), .2, 10),
          "Bound looping replaced root control ownership or standalone intent");
    // A tick crosses an ancestor reset and finishes beyond the descendant entry.
    // Copied presentation must use the new cycle rather than interpolate backwards.
    runtime.simulation.tick(.25);
    const double current_phase = .3;
    near(position(runtime), .2);
    for (const double alpha : {0., .25, .75, 1.}) {
        const auto presentation = runtime.simulation.presentation(alpha);
        near(row(presentation, actor).at("components").at("forge.local_translation").at("x"), .2);
        near(row(presentation, lamp).at("components").at("forge.light").at("intensity"),
             1 + 2 * current_phase);
        check(selected_camera(runtime, alpha) == EntityId::parse(shot_camera),
              "Camera and object presentation selected different child cycles");
    }
    const auto checkpoint = runtime.animation->checkpoint();
    check(checkpoint.at("version") == 13 && checkpoint.at("sequence_loop_profile") == 11 &&
              checkpoint.at("sequence_rate_profile") == 7,
          "Child loops did not wrap the exact nested/rate recovery profile");
    Runtime cold(root, source, false);
    cold.animation->restore(checkpoint);
    near(position(cold), .2);
    check(cold.animation->checkpoint() == checkpoint,
          "Fresh child-loop recovery changed derived clocks or intent");
    for (const bool strip_wrapper : {false, true}) {
        auto bad = checkpoint;
        if (strip_wrapper) {
            bad["version"] = bad.at("sequence_loop_profile");
            bad.erase("sequence_loop_profile");
        } else
            for (auto& entry : bad["sequence_entries"])
                if (entry.at("entity") == Json(ref(child)))
                    entry["sequence_loop"] = false;
        rejects([&] { cold.animation->restore(bad); });
        near(position(cold), .2);
        check(cold.animation->checkpoint() == checkpoint,
              "Rejected child-loop recovery partially adopted state");
    }
    // Invalid live geometry must not replace the last admitted complete pose.
    auto finite = inner;
    finite.sequence_loop = false;
    runtime.scene.entity(child).set(finite);
    check(runtime.animation->prepare_initial_pose(), "Finite placement edit did not admit");
    near(position(runtime), 2);
    runtime.scene.entity(child).set(inner);
    check(runtime.animation->prepare_initial_pose(), "Loop placement edit did not admit");
    near(position(runtime), .2);
    auto longer = outer;
    longer.duration = 8;
    runtime.scene.entity(parent).set(longer);
    check(runtime.animation->prepare_initial_pose(), "Parent duration edit did not admit");
    seek(runtime, 6.75);
    near(position(runtime), 1);
    runtime.scene.entity(parent).set(outer);
    check(runtime.animation->prepare_initial_pose(), "Parent duration repair did not admit");
    near(runtime.animation->playback(ref(parent))->time, 4.75);
    near(position(runtime), 1);
    seek(runtime, 1.65);
    near(position(runtime), .2);
    auto excessive = runtime.scene.entity(child).get<AnimationSequence>();
    excessive.duration = 1e-6;
    runtime.scene.entity(child).set(excessive);
    rejects([&] { runtime.animation->prepare_initial_pose(); });
    near(position(runtime), .2);
    runtime.scene.entity(child).set(inner);
    check(runtime.animation->prepare_initial_pose(), "Valid child-loop repair did not recover");
    near(position(runtime), .2);

    const auto before = scene.document();
    authoring_command(scene, "property.set_many",
                      {{"entity", child},
                       {"component", "forge.animation_sequence"},
                       {"values", {{"sequence_loop", false}, {"sequence_rate", 1.5}}}});
    check(!scene.entity(child).get<AnimationSequence>().sequence_loop,
          "Generic authoring did not update the typed placement loop");
    const auto edited = scene.document();
    check(scene.undo() && scene.document() == before && scene.redo() && scene.document() == edited,
          "Placement loop/rate edit did not share one history operation");
    auto malformed = source;
    for (auto& entry : malformed["entities"])
        if (entry.at("id") == child)
            entry["components"]["forge.animation_sequence"]["sequence_loop"] = 1;
    rejects([&] { scene.restore_snapshot(malformed); });
    check(scene.document() == edited, "Malformed loop intent partially changed the Scene");
}

void child_sequence_loop_audio_tests(const std::filesystem::path& root) {
    EngineContext author;
    Scene scene(author.world());
    const auto create = [&](const char* name, const char* recipe, Json values) {
        return authoring_command(scene, "entity.create",
                                 {{"name", name}, {"recipe", recipe}, {"component_values", values}})
            .at("selected")
            .get<std::string>();
    };
    const auto ref = [&](const std::string& id) {
        return EntityRef{scene.asset_id(), EntityId::parse(id)};
    };
    const auto parent = create("Root", "animation.sequence", {{"duration", 2.5}, {"loop", true}});
    const auto child = create("Looping child", "animation.sequence",
                              {{"duration", 1},
                               {"sequence", ref(parent)},
                               {"sequence_start_seconds", .25},
                               {"sequence_rate", 2},
                               {"sequence_loop", true},
                               {"loop", false}});
    const auto grand =
        create("Finite child section", "animation.sequence",
               {{"duration", .25}, {"sequence", ref(child)}, {"sequence_start_seconds", .25}});
    struct Point {
        EntityRef cue, source;
    };
    const auto cue = [&](const char* name, const std::string& clock, double time) {
        const auto speaker = ref(create(name, "audio.source", {{"clip", AssetId::generate()}}));
        return Point{
            ref(create(name, "animation.audio_cue",
                       {{"sequence", ref(clock)}, {"source", speaker}, {"time_seconds", time}})),
            speaker};
    };
    const auto a = cue("Child entry", child, 0);
    const auto b = cue("Child end", child, 1);
    const auto c = cue("Grandchild entry", grand, 0);
    const auto d = cue("Grandchild end", grand, .25);
    const auto e = cue("Root entry", parent, 0);
    struct Expected {
        double time;
        Point point;
    };
    const auto expected = [&](double begin, double end, bool entry, double placement_rate = 2,
                              bool root_loop = true) {
        std::vector<Expected> points;
        const auto add = [&](double time, Point point) {
            if ((time > begin || (entry && time == 0)) && time <= end)
                points.push_back({time, point});
        };
        // Source-local quarter/half/end points invert analytically through the
        // placement rate, independently of the provider's derived span storage.
        for (unsigned cycle = 0; cycle <= (root_loop ? unsigned(end / 2.5) : 0u); ++cycle) {
            const auto origin = cycle * 2.5;
            add(origin, e);
            const auto clipped = [&](double local, Point point) {
                const auto time = origin + .25 + local / placement_rate;
                if (time <= origin + 2.5)
                    add(time, point);
            };
            for (unsigned i = 0; i < unsigned(std::ceil(2.25 * placement_rate)); ++i) {
                clipped(i, a);
                clipped(i + 1, b);
                clipped(i + .25, c);
                clipped(i + .5, d);
            }
        }
        std::sort(points.begin(), points.end(), [](const auto& left, const auto& right) {
            return left.time != right.time ? left.time < right.time
                                           : left.point.cue < right.point.cue;
        });
        std::vector<EntityRef> sources;
        for (const auto& point : points)
            sources.push_back(point.point.source);
        return sources;
    };
    const auto source = scene.snapshot();
    Runtime segmented(root, source);
    const auto calls = std::make_shared<CueRecorder>();
    segmented.engine.services().publish_audio(calls);
    double time = 0;
    for (const double step : {.25, .125, .125, .25, .75, 1.}) {
        segmented.simulation.tick(step);
        time += step;
        check(calls->calls == expected(0, time, true),
              "Looped child cues lost entry/end, ancestor reset, tie ordering or root wrap");
    }
    AnimationPlaybackAccess::control(*segmented.animation, ref(parent), {{"operation", "pause"}});
    segmented.simulation.tick(.5);
    check(calls->calls == expected(0, 2.5, true), "Paused loop placement emitted sound points");
    Runtime whole(root, source);
    const auto many = std::make_shared<CueRecorder>();
    whole.engine.services().publish_audio(many);
    whole.simulation.tick(7.75);
    check(many->calls == expected(0, 7.75, true),
          "Multiple child/root cycles in one tick lost or duplicated sound points");
    const auto checkpoint = whole.animation->checkpoint();
    Runtime cold(root, source, false);
    const auto after = std::make_shared<CueRecorder>();
    cold.engine.services().publish_audio(after);
    cold.animation->restore(checkpoint);
    check(after->calls.empty(), "Child-loop recovery replayed sound entries");
    const auto count = many->calls.size();
    for (auto* world : {&whole, &cold})
        world->simulation.tick(.25);
    check(after->calls == expected(7.75, 8, false) &&
              std::vector<EntityRef>(many->calls.begin() + count, many->calls.end()) ==
                  after->calls,
          "Recovered child-loop cue interval differs from uninterrupted playback");
    Runtime overflow(root, source);
    const auto none = std::make_shared<CueRecorder>();
    overflow.engine.services().publish_audio(none);
    overflow.simulation.tick(20);
    check(none->calls.empty(), "Excessive child-loop cue batch was partially queued");
    const auto diagnostics = overflow.engine.services().diagnostics();
    check(std::any_of(diagnostics.begin(), diagnostics.end(),
                      [](const auto& item) {
                          return item.at("text").template get<std::string>().find(
                                     "128 occurrences") != std::string::npos;
                      }),
          "Excessive child-loop cue batch omitted its bounded diagnostic");
    auto fractional_source = source;
    for (auto& row : fractional_source["entities"]) {
        if (row.at("id") == parent)
            row["components"]["forge.animation_sequence"]["loop"] = false;
        if (row.at("id") == child)
            row["components"]["forge.animation_sequence"]["sequence_rate"] = 1.3;
    }
    Runtime fractional(root, fractional_source);
    const auto fractional_calls = std::make_shared<CueRecorder>();
    fractional.engine.services().publish_audio(fractional_calls);
    fractional.simulation.tick(2.5);
    check(fractional_calls->calls == expected(0, 2.5, true, 1.3, false),
          "Fractional placement rate separated coincident child entry/end cue ordering");
}
