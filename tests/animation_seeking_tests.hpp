#pragma once
#include "../src/animation_playback_access.hpp"
#include <limits>
void animation_seeking_tests(const std::filesystem::path& project, const Json& source,
                             EntityId actor) {
    Fixture f(project, source, actor);
    const auto ref = f.scene.reference(actor.str());
    f.simulation.tick(.25f);
    const auto before = f.scene.snapshot();
    require(f.animation->control(ref, AnimationControl::Pause, 2), "Seek pause failed");
    require(f.animation->seek(ref, .75, 2), "Paused seek failed");
    require(f.scene.snapshot() == before && !f.motion(),
            "Seek wrote a pose or retained stale motion");
    f.simulation.tick(.25f);
    near(f.animation->playback(ref)->time, .75);
    near(f.local().translation.z, .5);
    near(f.motion()->translation[2], 0);
    require(!f.animation->seek(ref, .5, 2), "Late seek accepted");
    bool foreign = true;
    std::thread thread([&] { foreign = f.animation->seek(ref, .5, 3); });
    thread.join();
    require(!foreign, "Foreign seek accepted");
    for (const auto invalid : {-1., 1.01, std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN()}) {
        bool rejected = false;
        try {
            (void)f.animation->seek(ref, invalid, 3);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "Invalid seek accepted");
        near(f.animation->playback(ref)->time, .75);
    }
    require(!f.animation->seek({ref.scene, EntityId::generate()}, .5, 3), "Missing seek accepted");
    require(f.animation->control(ref, AnimationControl::Play, 3) && f.animation->seek(ref, .9, 3),
            "Playing seek failed");
    const auto checkpoint = f.animation->checkpoint();
    Fixture restored(project, f.scene.snapshot(), actor);
    restored.animation->restore(checkpoint);
    f.simulation.tick(.25f);
    restored.simulation.tick(.25f);
    near(f.local().translation.z, 1);
    near(restored.local().translation.z, 1);
    near(f.animation->playback(ref)->time, .15);
    require(f.animation->seek(ref, 1., 4), "Loop end seek rejected");
    near(f.animation->playback(ref)->time, 0);
    f.simulation.tick(.25f);
    near(f.local().translation.z, 1.5);
    auto config = f.entity().get<Animator>();
    config.loop = false;
    f.entity().set(config);
    f.animation->synchronize();
    require(f.animation->seek(ref, 1., 5), "Nonloop end seek rejected");
    f.simulation.tick(.25f);
    near(f.local().translation.z, 1.5);
    require(f.animation->playback(ref)->finished, "Nonloop seek did not finish");
    require(f.animation->seek(ref, .25, 6), "Completed seek rejected");
    f.simulation.tick(.25f);
    near(f.animation->playback(ref)->time, .25);
    require(f.animation->control(ref, AnimationControl::Play, 7), "Completed seek cannot resume");
    f.simulation.tick(.25f);
    near(f.local().translation.z, 2);
    config.playback_speed = 0;
    f.entity().set(config);
    f.animation->synchronize();
    require(f.animation->seek(ref, .8, 8), "Zero speed seek rejected");
    f.simulation.tick(.25f);
    near(f.local().translation.z, 2);
    near(f.animation->playback(ref)->time, .8);
    // Private editor boundary updates a paused pose without advancing the clock.
    unsigned extra_ticks = 0;
    auto observer = f.engine.world()
                        .world()
                        .system("seek-clock-observer")
                        .kind(f.engine.world().world().entity("forge.runtime.PostAnimation"))
                        .immediate()
                        .run([&](flecs::iter&) { ++extra_ticks; });
    AnimationPlaybackAccess::control(*f.animation, ref,
                                     {{"operation", "seek"}, {"position_seconds", .4}});
    near(AnimationPlaybackAccess::status(*f.animation, ref).at("time"), .4);
    require(extra_ticks == 0, "Private seek advanced simulation");
    near(f.local().translation.z, 2);
    observer.destruct();
    // A private pose rejection restores the previous clock and joint pose.
    Fixture off(project, source, actor);
    off.mode(0);
    off.simulation.tick(.25f);
    const auto original = off.scene.snapshot();
    const auto reference = off.scene.reference(actor.str());
    off.animation->pose_validator([&](const auto& nodes) {
        for (const auto& [id, node] : nodes) {
            (void)id;
            if (node.local.translation.z > 1)
                throw std::runtime_error("Seek pose rejected by existing owner");
        }
    });
    bool rejected = false;
    try {
        AnimationPlaybackAccess::control(*off.animation, reference,
                                         {{"operation", "seek"}, {"position_seconds", .8}});
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && off.scene.snapshot() == original, "Rejected private seek changed pose");
    near(off.animation->playback(reference)->time, .25);
}
void animation_state_seeking_tests(const std::filesystem::path& project, const Json& source,
                                   EntityId actor) {
    Fixture f(project, source, actor);
    const auto ref = f.scene.reference(actor.str());
    require(f.animation->seek(ref, .6, 1), "Named seek failed");
    require(f.entity().get<Animator>().state == 1, "Seek itself triggered an exit");
    f.simulation.tick(.1f);
    require(f.entity().get<Animator>().state == 2 && f.animation->events().empty(),
            "Seek replayed skipped markers or lost the next advancing exit policy");
    near(f.local().translation.z, .2);
    f.simulation.tick(.125f);
    const auto fading = f.animation->playback(ref);
    require(fading->transitioning && f.animation->seek(ref, .8, 3), "Fading seek failed");
    near(f.animation->playback(ref)->transition_weight, fading->transition_weight);
    const auto saved = f.animation->checkpoint();
    Fixture recovered(project, f.scene.snapshot(), actor);
    recovered.animation->restore(saved);
    f.simulation.tick(.125f);
    recovered.simulation.tick(.125f);
    near(f.motion()->translation[0], .09375);
    near(f.motion()->translation[2], .15625);
    near(f.local().translation.x, recovered.local().translation.x);
    near(f.local().translation.z, recovered.local().translation.z);
    auto marker_source = source;
    for (auto& row : marker_source.at("entities"))
        if (row.at("id") == actor.str())
            row["components"]["forge.animator"]["state"] = 2;
    Fixture markers(project, marker_source, actor);
    const auto marker_ref = markers.scene.reference(actor.str());
    require(markers.animation->seek(marker_ref, .6, 1), "Marker seek failed");
    markers.simulation.tick(.1f);
    require(markers.animation->events().empty(), "Seek replayed a skipped marker");
    markers.simulation.tick(.1f);
    require(markers.animation->events().size() == 1 && markers.animation->events()[0].phase == .75,
            "Seek lost the actual subsequent marker");
}
