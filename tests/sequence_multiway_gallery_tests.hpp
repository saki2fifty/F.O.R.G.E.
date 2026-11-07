#pragma once
// Compare every layered result with independently sampled standalone resources.
Json exercise_multiway_gallery(const std::filesystem::path& root, AssetId id) {
    const auto source = load_game_scene(root, {id});
    std::string controller;
    std::vector<std::string> actors;
    for (const auto& row : source.at("entities")) {
        if (row.at("components").contains("forge.animation_sequence"))
            controller = row.at("id");
        if (row.at("components").contains("forge.animator"))
            actors.push_back(row.at("id"));
    }
    check(!controller.empty() && actors.size() == 3,
          "Multiway Gallery needs three original model tracks");
    World live(root, source), first(root, reference_document(source, false)),
        next(root, reference_document(source, true));
    Json observations = Json::array();
    bool different_morphs = false, mapped_motion = false, eight_contributors = false;
    check(live.scene.entity(controller).get<AnimationSequence>().duration == 4 &&
              live.scene.entity(controller).get<AnimationSequence>().loop,
          "Multiway Gallery must retain its authored four-second loop");
    for (const double requested : {0., .25, .5, .75, .9, 1., 1.25, 1.8, 2., 2.75, 3.75, 4.}) {
        // An explicit seek to a looping root's endpoint selects the next cycle's
        // zero, as defined by the existing sequence control owner.
        const double time = requested == 4 ? 0 : requested;
        live.seek(controller, requested);
        Json frame = Json::array();
        for (const auto& actor : actors) {
            const auto ref = live.scene.reference(actor);
            const auto config = live.scene.entity(actor).get<Animator>();
            check(config.sequence_multiway && config.sequence_extra_clips.size() == 6,
                  "Multiway Gallery lost its native opt-in or eight placements");
            const auto status = AnimationPlaybackAccess::status(*live.animation, ref);
            const auto actual = live.pose(actor);
            const double a_duration = first.animation->playback(ref)->duration;
            const double b_duration = next.animation->playback(ref)->duration;
            std::array<double, 8> starts{}, durations{}, weights{};
            std::array<Json, 8> references;
            starts[1] = config.sequence_next_start_seconds;
            for (std::size_t i = 2; i < starts.size(); ++i)
                starts[i] = config.sequence_extra_clips[i - 2].start_seconds;
            for (std::size_t i = 0; i < durations.size(); ++i) {
                durations[i] = i % 2 ? b_duration : a_duration;
                auto& reference = i % 2 ? next : first;
                reference.seek(actor, std::clamp(time - starts[i], 0., durations[i]));
                references[i] = reference.pose(actor);
            }
            // Closed-form ordered overlays; do not invoke the runtime's timing helper.
            double suffix = 1;
            for (std::size_t i = 7; i > 0; --i) {
                const double end = starts[i - 1] + durations[i - 1];
                const double alpha =
                    starts[i] >= end ? double(time >= starts[i])
                                     : std::clamp((time - starts[i]) / (end - starts[i]), 0., 1.);
                weights[i] = alpha * suffix;
                suffix *= 1 - alpha;
            }
            weights[0] = suffix;
            unsigned active = 0;
            for (std::size_t i = 0; i < weights.size(); ++i) {
                active += weights[i] > 0;
                const auto& layer = status.at("sequence_clips").at("clips")[i];
                if (!(std::abs(layer.at("weight").get<double>() - weights[i]) < 1e-9 &&
                      std::abs(layer.at("time_seconds").get<double>() -
                               std::clamp(time - starts[i], 0., durations[i])) < 1e-9))
                    throw std::runtime_error("Multiway Gallery watch mismatch: " +
                                             Json{{"actor", actor},
                                                  {"time", time},
                                                  {"layer", i},
                                                  {"durations", durations},
                                                  {"starts", starts},
                                                  {"expected_weight", weights[i]},
                                                  {"actual", layer}}
                                                 .dump());
            }
            eight_contributors |= active == 8;
            check(status.at("sequence_clips").at("multiway") == true &&
                      status.at("sequence_clips").at("summary_only") == (active > 2),
                  "Multiway Gallery mislabeled its two-slot summary");
            for (std::size_t joint = 0; joint < actual.at("local").size(); ++joint) {
                for (const char* field : {"translation", "scale"})
                    for (unsigned lane = 0; lane < 3; ++lane) {
                        double expected = 0;
                        for (std::size_t i = 0; i < weights.size(); ++i)
                            expected +=
                                weights[i] *
                                references[i].at("local")[joint].at(field)[lane].get<double>();
                        near(actual.at("local")[joint].at(field)[lane], expected);
                    }
                std::size_t anchor = 0;
                while (anchor + 1 < weights.size() && weights[anchor] == 0)
                    ++anchor;
                std::array<double, 4> expected{};
                for (std::size_t i = 0; i < weights.size(); ++i) {
                    double dot = 0;
                    for (unsigned lane = 0; lane < 4; ++lane)
                        dot += references[anchor]
                                   .at("local")[joint]
                                   .at("rotation")[lane]
                                   .get<double>() *
                               references[i].at("local")[joint].at("rotation")[lane].get<double>();
                    for (unsigned lane = 0; lane < 4; ++lane)
                        expected[lane] +=
                            weights[i] * (dot < 0 ? -1 : 1) *
                            references[i].at("local")[joint].at("rotation")[lane].get<double>();
                }
                double length_squared = 0, agreement = 0;
                for (const double lane : expected)
                    length_squared += lane * lane;
                check(length_squared > 0, "Multiway reference rotation is degenerate");
                for (unsigned lane = 0; lane < 4; ++lane)
                    agreement += expected[lane] / std::sqrt(length_squared) *
                                 actual.at("local")[joint].at("rotation")[lane].get<double>();
                near(std::abs(agreement), 1);
            }
            for (std::size_t node = 0; node < actual.at("morphs").size(); ++node)
                for (std::size_t lane = 0; lane < actual.at("morphs")[node].at("weights").size();
                     ++lane) {
                    double expected = 0;
                    for (std::size_t i = 0; i < weights.size(); ++i) {
                        check(references[i].at("morphs")[node].at("node") ==
                                  actual.at("morphs")[node].at("node"),
                              "Layered morph sampling changed node identity/defaults");
                        expected +=
                            weights[i] *
                            references[i].at("morphs")[node].at("weights")[lane].get<double>();
                    }
                    near(actual.at("morphs")[node].at("weights")[lane], expected);
                    different_morphs |=
                        active > 2 &&
                        std::abs(
                            references[0].at("morphs")[node].at("weights")[lane].get<double>() -
                            references[1].at("morphs")[node].at("weights")[lane].get<double>()) >
                            .01;
                }
            const auto authored = live.scene.document();
            const auto named =
                std::find_if(authored.at("entities").begin(), authored.at("entities").end(),
                             [&](const auto& row) { return row.at("id") == actor; });
            if (named->at("name").get<std::string>().starts_with("Right") && active > 2)
                mapped_motion |= references[0].at("local") != references[1].at("local");
            frame.push_back({{"pose", actual}, {"playback", status}});
        }
        const auto checkpoint = live.animation->checkpoint();
        check(checkpoint.at("version") == 15 && checkpoint.at("sequence_mixing_profile") == 12,
              "Multiway Gallery lost its exact recovery profiles");
        World cold(root, source);
        cold.animation->restore(checkpoint);
        for (const auto& actor : actors)
            check(cold.pose(actor) == live.pose(actor),
                  "Multiway Gallery recovery changed pose/morphs");
        observations.push_back(std::move(frame));
    }
    check(
        different_morphs && mapped_motion && eight_contributors,
        "Multiway Gallery did not exercise real morphs, mapped motion and all eight contributors");
    return observations;
}
