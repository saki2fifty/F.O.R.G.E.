#pragma once
// Exercise the admitted locomotion resources with independently derived weights.
void skeletal_sequence_multiway_tests(const std::filesystem::path& project, const Json& source,
                                      EntityId actor, AssetId forward, AssetId strafe) {
    for (const std::size_t count : std::array<std::size_t, 2>{3, sequence_clip_limit}) {
        EngineContext author;
        Scene scene(author.world());
        scene.restore_snapshot(source);
        const auto controller_id = configure_skeletal_sequence(scene, actor, forward, strafe);
        const auto controller = scene.reference(controller_id);
        const auto actor_ref = scene.reference(actor.str());
        const double step = count == 3 ? .25 : .125;
        auto sequence = scene.entity(controller_id).get<AnimationSequence>();
        sequence.duration = 2;
        scene.entity(controller_id).set(sequence);
        auto config = scene.entity(actor.str()).get<Animator>();
        config.sequence_multiway = true;
        config.sequence_next_start_seconds = step;
        for (std::size_t i = 2; i < count; ++i)
            config.sequence_extra_clips.push_back({{i % 2 ? strafe : forward}, step * i});
        scene.entity(actor.str()).set(config);
        const auto authored = scene.snapshot();
        const auto weights_at = [&](double time) {
            std::array<double, sequence_clip_limit> weights{};
            double suffix = 1;
            for (std::size_t i = count - 1; i > 0; --i) {
                const double fade = std::clamp((time - step * i) / (1 - step), 0., 1.);
                weights[i] = fade * suffix;
                suffix *= 1 - fade;
            }
            weights[0] = suffix;
            return weights;
        };
        const auto pose_at = [&](double time) {
            const auto weights = weights_at(time);
            std::array<double, 2> position{};
            for (std::size_t i = 0; i < count; ++i)
                position[i % 2 ? 0 : 1] += weights[i] * 2 * std::clamp(time - step * i, 0., 1.);
            return position;
        };
        Fixture sampled(project, authored, actor);
        for (unsigned tick = 0; tick <= 128; ++tick) {
            const double time = tick / 64.;
            AnimationPlaybackAccess::control(*sampled.animation, controller,
                                             {{"operation", "seek"}, {"position_seconds", time}});
            const auto pose = sampled.pose();
            const auto expected = pose_at(time);
            near(pose.at("local")[0].at("translation")[0], expected[0]);
            near(pose.at("local")[0].at("translation")[2], expected[1]);
            const auto observed = AnimationPlaybackAccess::status(*sampled.animation, actor_ref);
            const auto& clips = observed.at("sequence_clips");
            const auto weights = weights_at(time);
            require(clips.at("multiway") == true && clips.at("clips").size() == count,
                    "Skeletal multiway watch omitted contributors");
            unsigned active = 0;
            for (std::size_t i = 0; i < count; ++i) {
                near(clips.at("clips")[i].at("weight"), weights[i], 1e-9);
                near(clips.at("clips")[i].at("time_seconds"), std::clamp(time - step * i, 0., 1.),
                     1e-9);
                active += weights[i] > 0;
            }
            require(clips.at("summary_only") == (active > 2),
                    "Skeletal watch disguised a multiway pose as a two-layer recipe");
        }
        Fixture copied(project, authored, actor);
        for (unsigned tick = 0; tick < 4; ++tick)
            copied.simulation.tick(.25f);
        const auto copied_pose = copied.pose(.5);
        const auto copied_expected = pose_at(.875);
        near(copied_pose.at("local")[0].at("translation")[0], copied_expected[0]);
        near(copied_pose.at("local")[0].at("translation")[2], copied_expected[1]);
        AnimationPlaybackAccess::control(*sampled.animation, controller,
                                         {{"operation", "seek"}, {"position_seconds", .875}});
        const auto checkpoint = sampled.animation->checkpoint();
        require(checkpoint.at("version") == 15 && checkpoint.at("sequence_mixing_profile") == 12 &&
                    checkpoint.at("entries")[0].at("sequence_multiway") == true,
                "Skeletal multiway checkpoint lost its exact profile/opt-in");
        Fixture cold(project, authored, actor, false);
        cold.animation->restore(checkpoint);
        require(cold.animation->checkpoint() == checkpoint,
                "Skeletal multiway cold recovery changed intent");
        for (unsigned variant = 0; variant < 5; ++variant) {
            auto invalid = checkpoint;
            if (variant == 0)
                invalid["entries"][0]["sequence_multiway"] = false;
            else if (variant == 1)
                invalid["entries"][0].erase("sequence_multiway");
            else if (variant == 2)
                invalid["entries"][0]["sequence_multiway"] = 1;
            else if (variant == 3)
                invalid["entries"][0]["sequence"]["extra_clips"][0]["revision"] = "invalid";
            else {
                invalid["version"] = invalid.at("sequence_mixing_profile");
                invalid.erase("sequence_mixing_profile");
            }
            bool rejected = false;
            const auto before = cold.scene.snapshot();
            try {
                cold.animation->restore(invalid);
            } catch (const std::exception&) {
                rejected = true;
            }
            require(rejected && cold.scene.snapshot() == before &&
                        cold.animation->checkpoint() == checkpoint,
                    "Rejected skeletal multiway recovery changed the retained candidate");
        }
        // Independent midpoint quadrature of the original constant velocities;
        // the runtime instead integrates bounded fade polynomials per segment.
        const auto motion_at = [&](double end) {
            std::array<double, 2> result{};
            constexpr unsigned slices = 16384;
            const double width = end / slices;
            for (unsigned slice = 0; slice < slices; ++slice) {
                const double time = (slice + .5) * width;
                const auto weights = weights_at(time);
                for (std::size_t i = 0; i < count; ++i)
                    if (time >= step * i && time < step * i + 1)
                        result[i % 2 ? 0 : 1] += 2 * weights[i] * width;
            }
            return result;
        };
        const auto expected_motion = motion_at(2);
        if (count == 3) {
            near(expected_motion[0], 31. / 54, 1e-7);
            near(expected_motion[1], 131. / 54, 1e-7);
        }
        config.root_motion = 1;
        scene.entity(actor.str()).set(config);
        const auto moving_source = scene.snapshot();
        Fixture whole(project, moving_source, actor);
        whole.simulation.tick(2.f);
        near(whole.local().translation.x, expected_motion[0]);
        near(whole.local().translation.z, expected_motion[1]);
        near(whole.pose().at("local")[0].at("translation")[0], 0);
        near(whole.pose().at("local")[0].at("translation")[2], 0);
        Fixture segmented(project, moving_source, actor);
        for (unsigned tick = 0; tick < 128; ++tick)
            segmented.simulation.tick(1.f / 64);
        near(segmented.local().translation.x, whole.local().translation.x);
        near(segmented.local().translation.z, whole.local().translation.z);
        Fixture prefix(project, moving_source, actor);
        prefix.simulation.tick(.5f);
        const auto expected_prefix = motion_at(.5);
        near(prefix.local().translation.x, expected_prefix[0]);
        near(prefix.local().translation.z, expected_prefix[1]);
        sequence.loop = true;
        scene.entity(controller_id).set(sequence);
        Fixture looped(project, scene.snapshot(), actor);
        looped.simulation.tick(4.5f);
        near(looped.local().translation.x,
             2 * whole.local().translation.x + prefix.local().translation.x);
        near(looped.local().translation.z,
             2 * whole.local().translation.z + prefix.local().translation.z);
        const auto before_copy = looped.scene.snapshot();
        (void)looped.pose(.25);
        (void)looped.pose(.75);
        require(looped.scene.snapshot() == before_copy,
                "Copied multiway poses extracted native root motion");
    }
    std::cout << "Three/eight-clip native poses, full watch, recovery rollback, copied poses and "
                 "analytic root motion passed\n";
}
