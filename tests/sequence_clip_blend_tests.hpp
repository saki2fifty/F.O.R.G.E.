#pragma once
// Included inside the existing sequence fixture namespace; use its real owners.
void sequence_clip_blend_tests(const std::filesystem::path& root,
                               const std::shared_ptr<ProjectLease>& lease,
                               AssetImportService& imports, AssetId motion, AssetId pulse,
                               AssetId held) {
    auto reject = [&](const std::string& label, auto operation) {
        bool failed = false;
        try {
            operation();
        } catch (const std::exception& error) {
            if (std::string_view(error.what()).find("timed out") != std::string_view::npos)
                throw std::runtime_error("Sequence pair " + label + ": " + error.what());
            failed = true;
        }
        if (!failed)
            throw std::runtime_error("Sequence pair " + label + " accepted invalid state");
    };
    const auto next = clip(lease, imports, "Assets/SequenceNext.animation.json", false);
    const auto short_clip = clip(lease, imports, "Assets/SequenceShort.animation.json", false,
                                 false, animation_timebase / 4);
    const auto channels = clip(
        lease, imports, "Assets/SequenceChannels.animation.json", false, false, animation_timebase,
        [](auto& value) { value.document["channels"] = {"forge.local_translation"}; });
    const auto properties = clip(lease, imports, "Assets/SequenceProperties.animation.json", false,
                                 false, animation_timebase, [](auto& value) {
                                     value.add_property(AnimationProperty::LightIntensity, 1);
                                 });
    EngineContext author;
    Scene scene(author.world());
    const auto controller =
        authoring_command(scene, "entity.create", {{"recipe", "animation.sequence"}})
            .at("selected")
            .get<std::string>();
    const auto actor = authoring_command(scene, "entity.create", {{"recipe", "primitive.0"}})
                           .at("selected")
                           .get<std::string>();
    AnimationSequence sequence;
    sequence.duration = 2;
    scene.entity(controller).set(sequence);
    const EntityRef reference{scene.asset_id(), EntityId::parse(controller)};
    TransformAnimator track{{motion}};
    track.sequence = reference;
    track.sequence_next_clip = {next};
    track.sequence_next_start_seconds = .5;
    scene.entity(actor).set(track);
    const auto source = scene.snapshot();
    auto seek = [&](Runtime& runtime, double seconds) {
        AnimationPlaybackAccess::control(*runtime.animation, reference,
                                         {{"operation", "seek"}, {"position_seconds", seconds}});
    };
    auto position = [&](const Runtime& runtime) {
        return runtime.scene.entity(actor).get<LocalTranslation>().x;
    };
    Runtime runtime(root, source);
    // Two distinct 0->4, one-second clips, offset by half a second. Their
    // crossfade holds x=2 during overlap, then continues along the second clip.
    for (const auto [time, expected] : std::array<std::array<double, 2>, 8>{
             {{0, 0}, {.25, 1}, {.5, 2}, {.75, 2}, {1, 2}, {1.25, 3}, {1.5, 4}, {2, 4}}}) {
        seek(runtime, time);
        near(position(runtime), expected);
    }
    const EntityRef actor_ref{scene.asset_id(), EntityId::parse(actor)};
    near(runtime.animation->playback(actor_ref)->duration, 1.5);
    const auto pair_status = AnimationPlaybackAccess::status(*runtime.animation, actor_ref);
    near(pair_status.at("sequence_clips").at("duration_seconds"), 1);
    near(pair_status.at("sequence_clips").at("next").at("duration_seconds"), 1);
    check(!runtime.animation->seek(actor_ref, .25, 100),
          "Bound pair acquired an independent clock");
    seek(runtime, .75);
    check(runtime.animation->playback(actor_ref)->blend_clip == next,
          "Public paired playback omitted its secondary clip identity");
    near(runtime.animation->playback(actor_ref)->blend_weight, .5);
    near(AnimationPlaybackAccess::status(*runtime.animation, actor_ref)
             .at("sequence_clips")
             .at("next")
             .at("weight"),
         .5);
    const auto checkpoint = runtime.animation->checkpoint();
    check(checkpoint.at("version") == 6 && checkpoint.at("transform_entries").size() == 1 &&
              checkpoint.at("transform_entries")[0].at("sequence_next_clip") == Json(next),
          "Two-clip recovery did not require its exact pair profile");
    Runtime recovered(root, source);
    recovered.animation->restore(checkpoint);
    near(position(recovered), 2);
    near(recovered.animation->playback(reference)->time, .75);
    Runtime cold(root, source, false);
    cold.animation->restore(checkpoint);
    near(position(cold), 2);
    near(cold.animation->playback(reference)->time, .75);
    for (const auto& field :
         {"sequence_next_clip", "sequence_next_revision", "sequence_next_start_seconds"}) {
        auto bad = checkpoint;
        auto& entry = bad["transform_entries"][0];
        entry[field] = std::string_view(field) == "sequence_next_start_seconds"
                           ? Json(.25)
                           : Json(AssetId::generate());
        reject(std::string("recovery ") + field, [&] { recovered.animation->restore(bad); });
        near(position(recovered), 2);
        near(recovered.animation->playback(reference)->time, .75);
    }
    auto old = checkpoint;
    old["version"] = 5;
    reject("old recovery profile", [&] { recovered.animation->restore(old); });
    auto mutable_source = [&](const std::function<void(Json&)>& edit) {
        auto value = source;
        for (auto& item : value["entities"])
            if (item["id"] == actor)
                edit(item["components"]["forge.transform_animator"]);
        return value;
    };
    for (const auto& invalid : std::vector<Json>{
             {{"sequence_next_clip", pulse}},
             {{"sequence_next_clip", channels}},
             {{"sequence_next_clip", properties}},
             {{"sequence_next_clip", short_clip}},
             {{"sequence_next_clip", AssetId::generate()}},
             {{"clip", nullptr}},
             {{"sequence", nullptr}},
             {{"sequence_next_start_seconds", -.1}},
             {{"sequence_start_seconds", .5}, {"sequence_next_start_seconds", .25}}}) {
        const auto bad = mutable_source([&](Json& config) { config.update(invalid); });
        reject("invalid configuration " + invalid.dump(), [&] { Runtime rejected(root, bad); });
    }
    // A bad live selection must not partially publish a new pose or clock.
    auto bad_track = runtime.scene.entity(actor).get<TransformAnimator>();
    bad_track.sequence_next_clip = {pulse};
    runtime.scene.entity(actor).set(bad_track);
    reject("live selection", [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (!runtime.animation->prepare_initial_pose()) {
            near(position(runtime), 2);
            check(std::chrono::steady_clock::now() < deadline,
                  "Invalid replacement admission timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    near(position(runtime), 2);
    // More than the pool's 64-asset budget can be selected serially. Retired
    // requests must leave the existing resource owner rather than accumulate.
    Runtime turnover(root, source);
    for (unsigned index = 0; index < 67; ++index) {
        const auto replacement =
            clip(lease, imports,
                 "Assets/SequenceTurnover" + std::to_string(index) + ".animation.json", false);
        turnover.animation->catalog(
            std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
        auto selection = track;
        selection.sequence_next_clip = {replacement};
        turnover.scene.entity(actor).set(selection);
        check(!turnover.animation->prepare_initial_pose(),
              "New pair resource did not use asynchronous admission");
        near(position(turnover), 0);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (!turnover.animation->prepare_initial_pose()) {
            near(position(turnover), 0);
            check(std::chrono::steady_clock::now() < deadline,
                  "Pair resource retirement timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(turnover.animation->checkpoint().at("transform_entries")[0].at(
                  "sequence_next_clip") == Json(replacement),
              "Pair resource selection kept a retired request");
    }
    runtime.scene.entity(actor).set(track);
    for (unsigned tries = 0; !runtime.animation->prepare_initial_pose(); ++tries) {
        check(tries < 1000, "Corrected sequence pair did not recover");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    near(position(runtime), 2);
    const auto gap_source =
        mutable_source([](Json& value) { value["sequence_next_start_seconds"] = 1.25; });
    Runtime gap(root, gap_source);
    seek(gap, 1.125);
    near(position(gap), 4);
    seek(gap, 1.25);
    near(position(gap), 0);
    seek(gap, 1.5);
    near(position(gap), 1);
    Runtime gap_advance(root, gap_source);
    for (unsigned tick = 0; tick < 5; ++tick)
        gap_advance.simulation.tick(.25);
    near(position(gap_advance), 0);
    near(row(gap_advance.simulation.presentation(.5), actor)
             .at("components")
             .at("forge.local_translation")
             .at("x"),
         0);
    const auto held_source =
        mutable_source([&](Json& value) { value["sequence_next_clip"] = held; });
    Runtime held_runtime(root, held_source);
    for (unsigned tick = 0; tick < 4; ++tick)
        held_runtime.simulation.tick(.25);
    near(position(held_runtime), 2);
    near(row(held_runtime.simulation.presentation(.5), actor)
             .at("components")
             .at("forge.local_translation")
             .at("x"),
         2);
    auto loop_source = source;
    for (auto& item : loop_source["entities"])
        if (item["id"] == controller)
            item["components"]["forge.animation_sequence"]["loop"] = true;
    Runtime looping(root, loop_source);
    for (unsigned tick = 0; tick < 8; ++tick)
        looping.simulation.tick(.25);
    near(position(looping), 0);
    near(row(looping.simulation.presentation(.5), actor)
             .at("components")
             .at("forge.local_translation")
             .at("x"),
         0);
    // Known geometric midpoint, including opposite quaternion representations.
    LocalTransform a{{-2, 0, 0}, rotation_about_axis({0, 1, 0}, 45), {-1, 2, 1}};
    LocalTransform z{{4, 2, 0}, rotation_about_axis({0, 1, 0}, 135), {1, 4, 1}};
    z.rotation = {-z.rotation.x, -z.rotation.y, -z.rotation.z, -z.rotation.w};
    const auto middle = interpolate_animation_poses(a, z, .5);
    near(middle.translation.x, 1);
    near(middle.translation.y, 1);
    near(middle.scale.x, 0);
    check(equivalent(middle.rotation, rotation_about_axis({0, 1, 0}, 90)),
          "Sequence pose interpolation lost the shortest rotation arc");
    scene.reparent_entity(actor, controller, ReparentMode::KeepLocal);
    const auto definition = create_prefab_source(scene, controller);
    PrefabLibrary prefabs(root);
    const auto prefab = prefabs.create(scene, definition, "SequencePair.prefab.json");
    const auto instance = instantiate_prefab(scene, prefab);
    const auto child = named(scene, row(source, actor).at("name"), instance);
    const auto copied = scene.entity(child).get<TransformAnimator>();
    check(copied.clip.id == motion && copied.sequence_next_clip.id == next &&
              copied.sequence.entity == EntityId::parse(instance),
          "Portable pair Prefab lost clip identities or its member controller");
    near(copied.sequence_next_start_seconds, .5);
    // Re-publication of the same source keeps identity but changes its exact
    // resource revision. Recovery cannot silently substitute that new motion.
    Runtime revised(root, source);
    seek(revised, .75);
    const auto old_revision = revised.animation->checkpoint();
    TransformAnimationDocument document(lease, "Assets/SequenceNext.animation.json");
    document.edit(document.revision(), "Changed motion endpoint", [](Json& json) {
        TransformAnimationSource source{json};
        source.set_key(animation_timebase, {{8, 0, 0}, {}, {1, 1, 1}});
        json = source.document;
    });
    document.save();
    imports.submit(
        imports.prepare(document.locator(), {}, next),
        [](auto& candidate, const auto& plan, const auto&) {
            prepare_transform_animation_publication(candidate, plan);
        },
        [](const auto&, const auto&) {});
    check(imports.wait_idle(std::chrono::seconds(20)), "Pair revision cook timed out");
    const auto outcomes = imports.poll();
    check(outcomes.size() == 1 && outcomes[0].published, "Pair revision did not publish");
    revised.animation->catalog(
        std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!revised.animation->prepare_initial_pose()) {
        near(position(revised), 2);
        check(std::chrono::steady_clock::now() < deadline, "Pair revision admission timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    near(position(revised), 2.5);
    near(revised.animation->playback(reference)->time, .75);
    reject("replaced source revision", [&] { revised.animation->restore(old_revision); });
    near(position(revised), 2.5);
    std::cout << "Sequence pair:overlap/gap/hold/seek/wrap/held/recovery/atomic rejection passed\n";
}
