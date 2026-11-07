#pragma once
// Reuse the sequence fixture's real import, Scene and runtime owners.
void sequence_collection_runtime_tests(const std::filesystem::path& root,
                                       const std::shared_ptr<ProjectLease>& lease,
                                       AssetImportService& imports) {
    std::array<AssetId, sequence_clip_limit> clips;
    for (std::size_t i = 0; i < clips.size(); ++i)
        clips[i] = clip(lease, imports, "Assets/Collection" + std::to_string(i) + ".animation.json",
                        false, false, animation_timebase, [i](auto& source) {
                            source.add_property(AnimationProperty::LightIntensity, 10 + i);
                            source.add_property(AnimationProperty::LightRed, 1);
                        });
    EngineContext author;
    Scene scene(author.world());
    const auto controller =
        authoring_command(scene, "entity.create", {{"recipe", "animation.sequence"}})
            .at("selected")
            .get<std::string>();
    const auto actor = authoring_command(scene, "entity.create").at("selected").get<std::string>();
    AnimationSequence sequence;
    sequence.duration = 10;
    scene.entity(controller).set(sequence);
    const EntityRef ref{scene.asset_id(), EntityId::parse(controller)};
    const EntityRef actor_ref{scene.asset_id(), EntityId::parse(actor)};
    TransformAnimator track{{clips[0]}};
    track.sequence = ref;
    track.sequence_next_clip = {clips[1]};
    track.sequence_next_start_seconds = .5;
    for (std::size_t i = 2; i < clips.size(); ++i)
        track.sequence_extra_clips.push_back({{clips[i]}, .5 * i});
    scene.entity(actor).set(track).set(Light{});
    const auto source = scene.snapshot();
    Runtime runtime(root, source);
    const auto position = [&](const Runtime& value) {
        return value.scene.entity(actor).get<LocalTranslation>().x;
    };
    const auto seek = [&](Runtime& value, double time) {
        AnimationPlaybackAccess::control(*value.animation, ref,
                                         {{"operation", "seek"}, {"position_seconds", time}});
    };
    // Independently known trajectory: each ramp is 0->4 in one second. Half-
    // second overlaps hold x=2; only the first/last half seconds move linearly.
    for (unsigned step = 0; step <= 80; ++step) {
        const double time = step / 16.;
        seek(runtime, time);
        const auto expected = time < .5   ? 4 * time
                              : time <= 4 ? 2.
                                          : std::min(4., 4 * (time - 3.5));
        near(position(runtime), expected);
        const auto primary = std::size_t(std::clamp(int(std::floor(time * 2)) - 1, 0, 6));
        const auto weight = std::clamp((time - .5 * (primary + 1)) * 2, 0., 1.);
        near(runtime.scene.entity(actor).get<Light>().intensity, 10 + primary + weight);
        const auto observed = runtime.animation->playback(actor_ref);
        check(observed && observed->clip == clips[primary] &&
                  observed->blend_clip == clips[primary + 1],
              "Collection public playback selected the wrong adjacent clip IDs");
        near(observed->blend_weight, weight);
        near(observed->duration, 4.5);
        const auto status = AnimationPlaybackAccess::status(*runtime.animation, actor_ref);
        const auto& collection = status.at("sequence_clips");
        check(collection.at("clips").size() == clips.size() &&
                  collection.at("active_primary") == primary &&
                  collection.at("active_next") == primary + 1,
              "Collection live status lost the complete list or active window");
    }
    seek(runtime, 2.75);
    const auto checkpoint = runtime.animation->checkpoint();
    check(checkpoint.at("version") == 12 && checkpoint.at("sequence_arrangement_profile") == 6 &&
              checkpoint.at("transform_entries")[0].at("sequence_extra_clips").size() == 6,
          "Collection recovery omitted its exact profile or admitted resources");
    Runtime recovered(root, source, false);
    recovered.animation->restore(checkpoint);
    near(position(recovered), 2);
    near(recovered.scene.entity(actor).get<Light>().intensity, 14.5);
    near(recovered.animation->playback(ref)->time, 2.75);
    for (const auto* field : {"clip", "start_seconds", "revision"}) {
        auto bad = checkpoint;
        bad["transform_entries"][0]["sequence_extra_clips"][4][field] =
            std::string_view(field) == "start_seconds" ? Json(3.125) : Json("invalid");
        rejects([&] { recovered.animation->restore(bad); });
        near(recovered.animation->playback(ref)->time, 2.75);
        near(recovered.scene.entity(actor).get<Light>().intensity, 14.5);
    }
    auto old = checkpoint;
    old["version"] = 6;
    old.erase("sequence_arrangement_profile");
    rejects([&] { recovered.animation->restore(old); });
    // A configuration with a future bad clip must reject before the currently
    // visible first clip can publish, and must retain the previous whole pose.
    const auto incompatible = clip(lease, imports, "Assets/CollectionBad.animation.json", false);
    runtime.animation->catalog(
        std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
    auto invalid = track;
    invalid.sequence_extra_clips.back().clip = {incompatible};
    runtime.scene.entity(actor).set(invalid);
    rejects([&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (!runtime.animation->prepare_initial_pose()) {
            near(position(runtime), 2);
            near(runtime.scene.entity(actor).get<Light>().intensity, 14.5);
            check(std::chrono::steady_clock::now() < deadline, "Collection admission timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    near(position(runtime), 2);
    near(runtime.scene.entity(actor).get<Light>().intensity, 14.5);
    runtime.scene.entity(actor).set(track);
    for (unsigned attempt = 0; !runtime.animation->prepare_initial_pose(); ++attempt) {
        check(attempt < 1000, "Corrected collection did not recover");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    seek(runtime, 4.25);
    near(position(runtime), 3);
    near(runtime.scene.entity(actor).get<Light>().intensity, 17);
    // A gap holds the previous endpoint, then cuts to the incoming clip's start.
    auto gap_track = track;
    gap_track.sequence_next_start_seconds = 1.25;
    for (std::size_t i = 0; i < gap_track.sequence_extra_clips.size(); ++i)
        gap_track.sequence_extra_clips[i].start_seconds = 2.25 + i;
    scene.entity(actor).set(gap_track);
    Runtime gaps(root, scene.snapshot());
    for (const auto [time, expected] : std::array<std::array<double, 2>, 6>{
             {{1.125, 4}, {1.25, 0}, {2.249, 3.996}, {2.25, 0}, {7.25, 0}, {8.5, 4}}}) {
        seek(gaps, time);
        near(position(gaps), expected);
    }
    // Fixed ticks and copied presentation must use the same last active window.
    Runtime stepped(root, source);
    for (unsigned tick = 0; tick < 17; ++tick)
        stepped.simulation.tick(.25);
    near(position(stepped), 3);
    near(row(stepped.simulation.presentation(.5), actor).at("world_affine")[3], 2.5);
    // Dense overlapping placements must sample all eight clips, rather than
    // treating the dominant/runner-up status projection as a pose recipe.
    auto layered = track;
    layered.sequence_multiway = true;
    layered.sequence_next_start_seconds = .125;
    for (std::size_t i = 0; i < layered.sequence_extra_clips.size(); ++i)
        layered.sequence_extra_clips[i].start_seconds = .125 * (i + 2);
    scene.entity(actor).set(layered);
    const auto layered_source = scene.snapshot();
    Runtime mixed(root, layered_source);
    double copied_from = 0, copied_to = 0, copied_light = 0;
    for (unsigned step = 0; step <= 128; ++step) {
        const double time = step / 64.;
        std::array<double, 8> weights{};
        double suffix = 1;
        for (std::size_t i = 7; i > 0; --i) {
            const double alpha = std::clamp((time - .125 * i) / .875, 0., 1.);
            weights[i] = alpha * suffix;
            suffix *= 1 - alpha;
        }
        weights[0] = suffix;
        double expected_position = 0, expected_light = 0;
        for (std::size_t i = 0; i < weights.size(); ++i) {
            expected_position += weights[i] * 4 * std::clamp(time - .125 * i, 0., 1.);
            expected_light += weights[i] * (10 + i);
        }
        if (step == 48)
            copied_from = expected_position;
        if (step == 64)
            copied_to = expected_position;
        if (step == 56)
            copied_light = expected_light;
        seek(mixed, time);
        near(position(mixed), expected_position);
        near(mixed.scene.entity(actor).get<Light>().intensity, expected_light);
        near(mixed.scene.entity(actor).get<Light>().color_r, 1);
        const auto status = AnimationPlaybackAccess::status(*mixed.animation, actor_ref);
        const auto& complete = status.at("sequence_clips");
        check(complete.at("multiway") == true && complete.at("clips").size() == 8,
              "Multiway watch omitted its complete contributor list");
        std::size_t active = 0;
        for (std::size_t i = 0; i < weights.size(); ++i) {
            near(complete.at("clips")[i].at("weight"), weights[i]);
            near(complete.at("clips")[i].at("time_seconds"), std::clamp(time - .125 * i, 0., 1.));
            active += weights[i] > 0;
        }
        check(complete.at("summary_only") == (active > 2),
              "Multiway watch misrepresented the two-slot summary");
    }
    seek(mixed, .75);
    const auto mixed_checkpoint = mixed.animation->checkpoint();
    check(mixed_checkpoint.at("version") == 15 &&
              mixed_checkpoint.at("sequence_mixing_profile") == 12 &&
              mixed_checkpoint.at("transform_entries")[0].at("sequence_multiway") == true,
          "Multiway recovery lost its profile or exact opt-in");
    Runtime mixed_cold(root, layered_source, false);
    mixed_cold.animation->restore(mixed_checkpoint);
    near(position(mixed_cold), position(mixed));
    check(mixed_cold.animation->checkpoint() == mixed_checkpoint,
          "Cold multiway recovery changed its selected resources/flags");
    for (const Json& invalid : {Json(), Json(false), Json(1), Json("true")}) {
        auto bad = mixed_checkpoint;
        bad["transform_entries"][0]["sequence_multiway"] = invalid;
        rejects([&] { mixed_cold.animation->restore(bad); });
        near(position(mixed_cold), position(mixed));
        check(mixed_cold.animation->checkpoint() == mixed_checkpoint,
              "Rejected mixing recovery changed the retained candidate");
    }
    auto missing = mixed_checkpoint;
    missing["transform_entries"][0].erase("sequence_multiway");
    rejects([&] { mixed_cold.animation->restore(missing); });
    auto downgraded = mixed_checkpoint;
    downgraded["version"] = downgraded.at("sequence_mixing_profile");
    downgraded.erase("sequence_mixing_profile");
    rejects([&] { mixed_cold.animation->restore(downgraded); });
    const auto retained_position = position(mixed);
    auto legacy = layered;
    legacy.sequence_multiway = false;
    mixed.scene.entity(actor).set(legacy);
    rejects([&] {
        for (unsigned attempt = 0; !mixed.animation->prepare_initial_pose(); ++attempt) {
            check(attempt < 1000, "Legacy overlap rejection stalled");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    near(position(mixed), retained_position);
    mixed.scene.entity(actor).set(layered);
    for (unsigned attempt = 0; !mixed.animation->prepare_initial_pose(); ++attempt) {
        check(attempt < 1000, "Multiway profile repair stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // Object transforms retain their existing completed-pose interpolation;
    // properties independently sample the complete set at presentation time.
    Runtime multiway_ticks(root, layered_source);
    for (unsigned tick = 0; tick < 4; ++tick)
        multiway_ticks.simulation.tick(.25);
    const auto copied = multiway_ticks.simulation.presentation(.5);
    near(row(copied, actor).at("world_affine")[3], (copied_from + copied_to) * .5);
    near(row(copied, actor).at("components").at("forge.light").at("intensity"), copied_light);
    std::cout
        << "Eight-clip transform collections: native resources, independent values, "
           "active IDs, recovery/rejection/repair, gaps/cuts and copied presentation passed\n";
}
