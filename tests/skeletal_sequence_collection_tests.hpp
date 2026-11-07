#pragma once
#include "skeletal_sequence_tests.hpp"
// Reuse the original locomotion model and native Scene/animation fixture.
void skeletal_sequence_collection_tests(const std::filesystem::path& project, const Json& source,
                                        EntityId actor, AssetId forward, AssetId strafe) {
    EngineContext author;
    Scene scene(author.world());
    scene.restore_snapshot(source);
    const auto controller_id = configure_skeletal_sequence(scene, actor, forward, strafe);
    const auto controller = scene.reference(controller_id);
    const auto actor_ref = scene.reference(actor.str());
    auto sequence = scene.entity(controller_id).get<AnimationSequence>();
    sequence.duration = 5;
    scene.entity(controller_id).set(sequence);
    auto config = scene.entity(actor.str()).get<Animator>();
    for (std::size_t i = 2; i < sequence_clip_limit; ++i)
        config.sequence_extra_clips.push_back({{i % 2 ? strafe : forward}, .5 * i});
    scene.entity(actor.str()).set(config);
    const auto authored = scene.snapshot();
    Fixture runtime(project, authored, actor);
    const auto seek = [&](Fixture& value, double seconds) {
        AnimationPlaybackAccess::control(*value.animation, controller,
                                         {{"operation", "seek"}, {"position_seconds", seconds}});
    };
    for (unsigned step = 0; step <= 80; ++step) {
        const double time = step / 16.;
        seek(runtime, time);
        const auto index = std::size_t(std::clamp(int(std::floor(time * 2)) - 1, 0, 6));
        const auto weight = std::clamp((time - .5 * (index + 1)) * 2, 0., 1.);
        const auto a = 2 * std::clamp(time - .5 * index, 0., 1.);
        const auto b = 2 * std::clamp(time - .5 * (index + 1), 0., 1.);
        const auto x = index % 2 ? a * (1 - weight) : b * weight;
        const auto z = index % 2 ? b * weight : a * (1 - weight);
        const auto pose = runtime.pose();
        near(pose.at("local")[0].at("translation")[0], x);
        near(pose.at("local")[0].at("translation")[2], z);
        const auto playback = runtime.animation->playback(actor_ref);
        require(playback && playback->clip == (index % 2 ? strafe : forward) &&
                    playback->blend_clip == (index % 2 ? forward : strafe),
                "Skeletal collection active clip identities differ");
        near(playback->blend_weight, weight);
        near(playback->duration, 4.5);
        const auto status = AnimationPlaybackAccess::status(*runtime.animation, actor_ref);
        require(status.at("sequence_clips").at("clips").size() == 8 &&
                    status.at("sequence_clips").at("active_primary") == index,
                "Skeletal collection status omitted its list or active window");
    }
    seek(runtime, 2.75);
    const auto checkpoint = runtime.animation->checkpoint();
    require(checkpoint.at("version") == 12 && checkpoint.at("sequence_arrangement_profile") == 10 &&
                checkpoint.at("entries")[0].at("sequence").at("extra_clips").size() == 6,
            "Skeletal collection checkpoint omitted profile or resource revisions");
    Fixture restored(project, authored, actor, false);
    restored.animation->restore(checkpoint);
    near(restored.animation->playback(controller)->time, 2.75);
    near(restored.pose().at("local")[0].at("translation")[0], .25);
    near(restored.pose().at("local")[0].at("translation")[2], .75);
    for (unsigned variant = 0; variant < 4; ++variant) {
        auto bad = checkpoint;
        if (variant == 0) {
            bad["version"] = bad.at("sequence_arrangement_profile");
            bad.erase("sequence_arrangement_profile");
        } else {
            auto& extra = bad["entries"][0]["sequence"]["extra_clips"][4];
            if (variant == 1)
                extra["revision"] = "invalid";
            else if (variant == 2)
                extra["clip"] = AssetId::generate();
            else
                extra["start_seconds"] = 3.125;
        }
        const auto before = restored.scene.snapshot();
        bool rejected = false;
        try {
            restored.animation->restore(bad);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected && restored.scene.snapshot() == before,
                "Rejected skeletal collection recovery changed the Scene");
        near(restored.animation->playback(controller)->time, 2.75);
    }
    // Every half-second overlap integrates the mean of the adjacent velocities.
    // Four forward/strafe placements give 4.5 units along each axis per cycle.
    config.root_motion = 1;
    scene.entity(actor.str()).set(config);
    Fixture moving(project, scene.snapshot(), actor);
    for (unsigned step = 0; step < 20; ++step)
        moving.simulation.tick(.25f);
    near(moving.local().translation.x, 4.5);
    near(moving.local().translation.z, 4.5);
    near(moving.pose().at("local")[0].at("translation")[0], 0);
    near(moving.pose().at("local")[0].at("translation")[2], 0);
    Fixture whole(project, scene.snapshot(), actor);
    whole.simulation.tick(5);
    near(whole.local().translation.x, 4.5);
    near(whole.local().translation.z, 4.5);
    sequence.loop = true;
    scene.entity(controller_id).set(sequence);
    Fixture looping(project, scene.snapshot(), actor);
    looping.simulation.tick(10.25f);
    near(looping.local().translation.x, 9);
    near(looping.local().translation.z, 9.5);
    // Nested placement rates remap every clip boundary into the root interval.
    sequence.loop = false;
    sequence.duration = 3;
    scene.entity(controller_id).set(sequence);
    const auto child_id =
        authoring_command(scene, "entity.create", {{"recipe", "animation.sequence"}})
            .at("selected")
            .get<std::string>();
    AnimationSequence child;
    child.duration = 5;
    child.sequence = controller;
    child.sequence_start_seconds = .25;
    child.sequence_rate = 2;
    scene.entity(child_id).set(child);
    config.sequence = scene.reference(child_id);
    scene.entity(actor.str()).set(config);
    Fixture nested(project, scene.snapshot(), actor);
    nested.simulation.tick(3);
    near(nested.local().translation.x, 4.5);
    near(nested.local().translation.z, 4.5);
    require(nested.animation->checkpoint().at("sequence_arrangement_profile") == 11,
            "Collection recovery lost the nested placement-rate profile");
    // A five-local-second child at 2x repeats three full cycles and a half
    // local second inside this eight-second root. Held clip endpoints and clock
    // resets contribute no motion between the outgoing and incoming poses.
    sequence.duration = 8;
    scene.entity(controller_id).set(sequence);
    child.sequence_loop = true;
    scene.entity(child_id).set(child);
    // Compression error in the original imported clips is checked against the
    // source-space oracle above. Repetitions must compose those same admitted
    // native deltas, rather than multiply one fixed absolute error tolerance.
    const auto cycle_motion = whole.local().translation;
    Fixture prefix_reference(project, whole.scene.snapshot(), actor);
    const auto initial_prefix = prefix_reference.local().translation;
    prefix_reference.simulation.tick(.5f);
    const auto partial_pose = prefix_reference.local().translation;
    const LocalTranslation partial_motion{partial_pose.x - initial_prefix.x,
                                          partial_pose.y - initial_prefix.y,
                                          partial_pose.z - initial_prefix.z};
    near(partial_motion.x, 0);
    near(partial_motion.z, 1);
    prefix_reference.simulation.tick(.5f);
    const auto prefix_pose = prefix_reference.local().translation;
    const LocalTranslation prefix_motion{prefix_pose.x - initial_prefix.x,
                                         prefix_pose.y - initial_prefix.y,
                                         prefix_pose.z - initial_prefix.z};
    near(prefix_motion.x, .5);
    near(prefix_motion.z, 1.5);
    std::cout << "Admitted native cycle/prefix deltas: " << cycle_motion.x << "," << cycle_motion.z
              << " / " << prefix_motion.x << "," << prefix_motion.z << '\n';
    for (const bool segmented : {false, true}) {
        Fixture repeated(project, scene.snapshot(), actor);
        for (unsigned i = 0; i < (segmented ? 64u : 1u); ++i)
            repeated.simulation.tick(segmented ? .125f : 8.f);
        const auto expected_x = 3 * cycle_motion.x + partial_motion.x;
        const auto expected_z = 3 * cycle_motion.z + partial_motion.z;
        near(repeated.local().translation.x, expected_x);
        near(repeated.local().translation.z, expected_z);
        const auto checkpoint = repeated.animation->checkpoint();
        require(checkpoint.at("version") == 13 && checkpoint.at("sequence_loop_profile") == 12 &&
                    checkpoint.at("sequence_arrangement_profile") == 11,
                "Skeletal looping collection lost its composed recovery profile");
        Fixture cold(project, repeated.scene.snapshot(), actor, false);
        cold.animation->restore(checkpoint);
        near(cold.local().translation.x, expected_x);
        near(cold.local().translation.z, expected_z);
    }
    sequence.loop = true;
    scene.entity(controller_id).set(sequence);
    Fixture repeated_roots(project, scene.snapshot(), actor);
    repeated_roots.simulation.tick(16.5f);
    near(repeated_roots.local().translation.x, 6 * cycle_motion.x + 3 * partial_motion.x);
    near(repeated_roots.local().translation.z, 6 * cycle_motion.z + 3 * partial_motion.z);
    sequence.loop = false;
    scene.entity(controller_id).set(sequence);
    Fixture placement_reference(project, whole.scene.snapshot(), actor);
    const auto origin = placement_reference.local().translation;
    placement_reference.simulation.tick(2.5f);
    const auto midpoint = placement_reference.local().translation;
    const LocalTranslation midpoint_motion{midpoint.x - origin.x, midpoint.y - origin.y,
                                           midpoint.z - origin.z};
    near(midpoint_motion.x, 2);
    near(midpoint_motion.z, 3);
    child.sequence_extra_placements = {{3, .5, false}};
    scene.entity(child_id).set(child);
    const LocalTranslation placed_motion{cycle_motion.x + partial_motion.x + midpoint_motion.x, 0,
                                         cycle_motion.z + partial_motion.z + midpoint_motion.z};
    for (const bool segmented : {false, true}) {
        Fixture placed(project, scene.snapshot(), actor);
        for (unsigned i = 0; i < (segmented ? 64u : 1u); ++i)
            placed.simulation.tick(segmented ? .125f : 8.f);
        near(placed.local().translation.x, placed_motion.x);
        near(placed.local().translation.z, placed_motion.z);
        const auto saved = placed.animation->checkpoint();
        require(saved.at("version") == 14 && saved.at("sequence_placement_profile") == 13 &&
                    saved.at("sequence_loop_profile") == 12,
                "Repeated skeletal placements lost the existing loop/collection recovery chain");
        Fixture resumed(project, placed.scene.snapshot(), actor, false);
        resumed.animation->restore(saved);
        require(resumed.animation->checkpoint() == saved,
                "Repeated skeletal placement recovery changed clocks or configuration");
        near(resumed.local().translation.x, placed_motion.x);
        near(resumed.local().translation.z, placed_motion.z);
    }
    sequence.loop = true;
    scene.entity(controller_id).set(sequence);
    Fixture placed_roots(project, scene.snapshot(), actor);
    placed_roots.simulation.tick(16.5f);
    near(placed_roots.local().translation.x, 2 * placed_motion.x + partial_motion.x);
    near(placed_roots.local().translation.z, 2 * placed_motion.z + partial_motion.z);
    sequence.loop = false;
    scene.entity(controller_id).set(sequence);
    child.sequence_extra_placements.clear();
    scene.entity(child_id).set(child);
    const auto grand_id =
        authoring_command(scene, "entity.create", {{"recipe", "animation.sequence"}})
            .at("selected")
            .get<std::string>();
    AnimationSequence grand;
    grand.duration = 1;
    grand.sequence = scene.reference(child_id);
    grand.sequence_start_seconds = 1;
    scene.entity(grand_id).set(grand);
    config.sequence = scene.reference(grand_id);
    // This short descendant admits the original pair; later collection starts
    // would be outside its duration and must remain rejected by admission.
    config.sequence_extra_clips.clear();
    scene.entity(actor.str()).set(config);
    Fixture finite_descendant(project, scene.snapshot(), actor);
    finite_descendant.simulation.tick(8);
    near(finite_descendant.local().translation.x, 3 * prefix_motion.x);
    near(finite_descendant.local().translation.z, 3 * prefix_motion.z);
    grand.sequence_loop = true;
    scene.entity(grand_id).set(grand);
    Fixture repeated_descendant(project, scene.snapshot(), actor);
    repeated_descendant.simulation.tick(8);
    near(repeated_descendant.local().translation.x, 12 * prefix_motion.x);
    near(repeated_descendant.local().translation.z, 12 * prefix_motion.z);
    std::cout
        << "Eight-clip skeletal collections: repeated resources, independent poses, "
           "active IDs, cold recovery and root-motion boundary/rate/loop integration passed\n";
}
