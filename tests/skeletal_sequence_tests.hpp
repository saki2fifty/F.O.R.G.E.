#pragma once
// Uses the original model publication, Scene and native playback fixture.
std::string configure_skeletal_sequence(Scene& scene, EntityId actor, AssetId forward,
                                        AssetId strafe) {
    const auto controller_id =
        authoring_command(scene, "entity.create", {{"recipe", "animation.sequence"}})
            .at("selected")
            .get<std::string>();
    AnimationSequence sequence;
    sequence.duration = 2;
    scene.entity(controller_id).set(sequence);
    const auto controller = scene.reference(controller_id);
    auto animator = scene.entity(actor.str()).get<Animator>();
    animator.root_motion = 0;
    animator.clip.id = forward;
    animator.sequence = controller;
    animator.sequence_next_clip.id = strafe;
    animator.sequence_next_start_seconds = .5;
    // These retained standalone values must not affect a bound track.
    animator.loop = true;
    animator.playback_speed = 0;
    animator.play_on_start = false;
    scene.entity(actor.str()).set(animator);
    return controller_id;
}
void skeletal_sequence_tests(const std::filesystem::path& project, const Json& source,
                             EntityId actor, AssetId forward, AssetId strafe) {
    EngineContext author;
    Scene scene(author.world());
    scene.restore_snapshot(source);
    const auto controller_id = configure_skeletal_sequence(scene, actor, forward, strafe);
    auto sequence = scene.entity(controller_id).get<AnimationSequence>();
    const auto controller = scene.reference(controller_id);
    auto animator = scene.entity(actor.str()).get<Animator>();
    const auto authored = scene.snapshot();
    Fixture f(project, authored, actor);
    const auto actor_ref = f.scene.reference(actor.str());
    auto seek = [&](double seconds) {
        AnimationPlaybackAccess::control(*f.animation, controller,
                                         {{"operation", "seek"}, {"position_seconds", seconds}});
    };
    for (const auto& sample : std::array<std::array<double, 3>, 8>{{{0, 0, 0},
                                                                    {.25, 0, .5},
                                                                    {.5, 0, 1},
                                                                    {.75, .25, .75},
                                                                    {1, 1, 0},
                                                                    {1.25, 1.5, 0},
                                                                    {1.5, 2, 0},
                                                                    {2, 2, 0}}}) {
        seek(sample[0]);
        const auto pose = f.pose();
        near(pose.at("local")[0].at("translation")[0], sample[1]);
        near(pose.at("local")[0].at("translation")[2], sample[2]);
        near(f.animation->playback(controller)->time, sample[0]);
        near(f.animation->playback(actor_ref)->time, std::min(sample[0], 1.5));
    }
    require(!f.animation->control(actor_ref, AnimationControl::Restart, 1) &&
                !f.animation->seek(actor_ref, .25, 1),
            "Skeletal sequence track acquired an independent clock");
    require(scene.snapshot() == authored, "Skeletal sequence preview changed authored content");
    // Native group seek preflight must retain both clocks and the visible pose.
    seek(.75);
    const auto checkpoint = f.animation->checkpoint();
    require(checkpoint.at("version") == 10 && checkpoint.contains("skeletal_sequence_profile"),
            "Skeletal sequence checkpoint omitted its exact profile");
    Fixture restored(project, authored, actor);
    restored.animation->restore(checkpoint);
    near(restored.animation->playback(controller)->time, .75);
    near(restored.pose().at("local")[0].at("translation")[0], .25);
    near(restored.pose().at("local")[0].at("translation")[2], .75);
    for (unsigned variant = 0; variant < 5; ++variant) {
        auto invalid = checkpoint;
        if (variant == 0) {
            invalid["version"] = invalid.at("skeletal_sequence_profile");
            invalid.erase("skeletal_sequence_profile");
        } else if (variant == 1)
            invalid["entries"][0]["sequence"]["next_revision"] = "different";
        else if (variant == 2)
            invalid["entries"][0]["sequence"]["next_start_seconds"] = .6;
        else if (variant == 3)
            invalid["entries"][0]["time"] = .25;
        else
            invalid["sequence_entries"][0]["time_us"] = 250000;
        const auto retained = restored.scene.snapshot();
        bool rejected = false;
        try {
            restored.animation->restore(invalid);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected && restored.scene.snapshot() == retained,
                "Invalid skeletal sequence recovery changed native content");
        near(restored.animation->playback(controller)->time, .75);
    }
    const auto before = f.scene.snapshot();
    const auto before_pose = f.pose();
    f.animation->pose_validator([](const auto&) {
        throw std::runtime_error("Rejected complete skeletal sequence candidate");
    });
    bool rejected = false;
    try {
        seek(1.25);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && f.scene.snapshot() == before && f.pose() == before_pose,
            "Failed skeletal sequence seek partially published a pose");
    near(f.animation->playback(controller)->time, .75);
    near(f.animation->playback(actor_ref)->time, .75);
    f.animation->pose_validator({});
    seek(1.25);
    near(f.pose().at("local")[0].at("translation")[0], 1.5);
    seek(.25);
    const auto good_native = f.scene.snapshot();
    f.animation->pose_validator([](const auto&) {
        throw std::runtime_error("Rejected fixed skeletal sequence candidate");
    });
    f.simulation.tick(.25f);
    require(f.scene.snapshot() == good_native && f.pose().is_null() &&
                !f.animation->checkpoint_ready(),
            "Failed fixed skeletal group published values or a fresh presentation");
    f.animation->pose_validator({});
    f.simulation.tick(.25f);
    for (const double alpha : {0., .5, 1.}) {
        near(f.pose(alpha).at("local")[0].at("translation")[0], .25);
        near(f.pose(alpha).at("local")[0].at("translation")[2], .75);
    }

    animator.root_motion = 1;
    scene.entity(actor.str()).set(animator);
    Fixture moving(project, scene.snapshot(), actor);
    moving.simulation.tick(.25f);
    near(moving.local().translation.z, .5);
    near(moving.motion()->translation[2], .5);
    moving.simulation.tick(.25f);
    moving.simulation.tick(.25f);
    near(moving.local().translation.x, .125);
    near(moving.local().translation.z, 1.375);
    near(moving.motion()->translation[0], .125);
    near(moving.motion()->translation[2], .375);
    const auto live_checkpoint = moving.animation->checkpoint();
    Fixture live_restored(project, moving.scene.snapshot(), actor);
    live_restored.animation->restore(live_checkpoint);
    moving.simulation.tick(.25f);
    live_restored.simulation.tick(.25f);
    near(moving.local().translation.x, live_restored.local().translation.x);
    near(moving.local().translation.z, live_restored.local().translation.z);
    for (unsigned i = 0; i < 4; ++i)
        moving.simulation.tick(.25f);
    near(moving.local().translation.x, 1.5);
    near(moving.local().translation.z, 1.5);
    near(moving.motion()->translation[0], 0);
    near(moving.motion()->translation[2], 0);
    near(moving.pose().at("local")[0].at("translation")[0], 0);
    near(moving.pose().at("local")[0].at("translation")[2], 0);

    sequence.loop = true;
    scene.entity(controller_id).set(sequence);
    Fixture looping(project, scene.snapshot(), actor);
    looping.simulation.tick(2.5f);
    near(looping.local().translation.x, 1.5);
    near(looping.local().translation.z, 2.5);
    near(looping.animation->playback(controller)->time, .5);
    const auto position = looping.local();
    for (const double alpha : {0., .25, .75, 1.})
        (void)looping.pose(alpha);
    require(looping.local() == position, "Skeletal sequence presentation extracted root motion");

    sequence.loop = false;
    sequence.duration = 3;
    scene.entity(controller_id).set(sequence);
    animator.root_motion = 0;
    animator.sequence_next_start_seconds = 1.5;
    scene.entity(actor.str()).set(animator);
    Fixture gap(project, scene.snapshot(), actor);
    AnimationPlaybackAccess::control(*gap.animation, controller,
                                     {{"operation", "seek"}, {"position_seconds", 1.25}});
    near(gap.pose().at("local")[0].at("translation")[2], 2);
    AnimationPlaybackAccess::control(*gap.animation, controller,
                                     {{"operation", "seek"}, {"position_seconds", 1.5}});
    near(gap.pose().at("local")[0].at("translation")[0], 0);
    near(gap.pose().at("local")[0].at("translation")[2], 0);

    animator.sequence_next_clip = {};
    animator.sequence_start_seconds = .5;
    scene.entity(actor.str()).set(animator);
    Fixture single(project, scene.snapshot(), actor);
    for (const auto& sample : std::array<std::array<double, 2>, 3>{{{.25, 0}, {1, 1}, {2, 2}}}) {
        AnimationPlaybackAccess::control(*single.animation, controller,
                                         {{"operation", "seek"}, {"position_seconds", sample[0]}});
        near(single.pose().at("local")[0].at("translation")[2], sample[1]);
    }
    const auto nested_id =
        authoring_command(scene, "entity.create", {{"recipe", "animation.sequence"}})
            .at("selected")
            .get<std::string>();
    AnimationSequence child;
    child.duration = 1.25;
    child.sequence = controller;
    child.sequence_start_seconds = .5;
    child.playback_speed = 0;
    child.loop = true;
    scene.entity(nested_id).set(child);
    const auto nested_ref = scene.reference(nested_id);
    animator.sequence = nested_ref;
    animator.sequence_start_seconds = .25;
    animator.sequence_next_clip.id = strafe;
    animator.sequence_next_start_seconds = .75;
    scene.entity(actor.str()).set(animator);
    Fixture nested(project, scene.snapshot(), actor);
    AnimationPlaybackAccess::control(*nested.animation, controller,
                                     {{"operation", "seek"}, {"position_seconds", 1.5}});
    near(nested.pose().at("local")[0].at("translation")[0], .25);
    near(nested.pose().at("local")[0].at("translation")[2], .75);
    require(!nested.animation->seek(nested_ref, .5, 1),
            "Nested skeletal sequence acquired an independent clock");
    const auto nested_checkpoint = nested.animation->checkpoint();
    require(nested_checkpoint.at("version") == 10 &&
                nested_checkpoint.at("skeletal_sequence_profile") == 7,
            "Skeletal sequence recovery discarded its nested profile");
    Fixture nested_restored(project, scene.snapshot(), actor);
    nested_restored.animation->restore(nested_checkpoint);
    near(nested_restored.pose().at("local")[0].at("translation")[0], .25);
    near(nested_restored.pose().at("local")[0].at("translation")[2], .75);
    animator.root_motion = 1;
    scene.entity(actor.str()).set(animator);
    Fixture nested_motion(project, scene.snapshot(), actor);
    nested_motion.simulation.tick(2.5f);
    near(nested_motion.local().translation.x, .5);
    near(nested_motion.local().translation.z, 1.5);
    auto disabled = nested_motion.scene.entity(controller_id).get<AnimationSequence>();
    disabled.enabled = false;
    nested_motion.scene.entity(controller_id).set(disabled);
    nested_motion.animation->synchronize();
    require(!nested_motion.animation->playback(nested_motion.scene.reference(actor.str())),
            "Disabled sequence retained a standalone skeletal player");
}
void skeletal_sequence_mixed_tests(const std::filesystem::path& project, const Json& source,
                                   EntityId actor, AssetId forward, AssetId strafe,
                                   const std::shared_ptr<ProjectLease>& lease) {
    AssetImportService imports(lease, transform_animation_import_registry(),
                               {"linux", "none", "cpu"}, 1);
    const auto light_clip = [&](const std::filesystem::path& path, double first, double last) {
        auto document = TransformAnimationDocument::create(lease, path);
        document->edit(document->revision(), "Mixed skeletal/light fixture", [&](Json& value) {
            TransformAnimationSource clip{value};
            clip.retime_duration(2 * animation_timebase);
            clip.add_property(AnimationProperty::LightIntensity, first);
            clip.document["channels"] = Json::array();
            auto properties = clip.clip().keys.front().properties;
            properties[unsigned(AnimationProperty::LightIntensity)] = last;
            clip.set_key(2 * animation_timebase, {}, false, {}, {}, properties);
            value = std::move(clip.document);
        });
        document->save();
        const auto id = document->source().asset();
        imports.submit(
            imports.prepare(path, {}, id),
            [](auto& candidate, const auto& draft, const auto&) {
                prepare_transform_animation_publication(candidate, draft);
            },
            [](const auto&, const auto&) {});
        require(imports.wait_idle(20s), "Mixed Light source publication timed out");
        const auto outcomes = imports.poll();
        require(outcomes.size() == 1 && outcomes[0].published,
                outcomes.empty() ? "Missing mixed Light publication" : outcomes[0].diagnostic);
        return id;
    };
    const auto first = light_clip("Assets/MixedLightFirst.animation.json", 1, 3);
    const auto next = light_clip("Assets/MixedLightNext.animation.json", 5, 7);
    EngineContext author;
    Scene scene(author.world());
    scene.restore_snapshot(source);
    const auto controller_id = configure_skeletal_sequence(scene, actor, forward, strafe);
    const auto controller = scene.reference(controller_id);
    scene.entity(actor.str()).set(Light{});
    TransformAnimator properties{{first}};
    properties.sequence = controller;
    properties.sequence_next_clip = {next};
    properties.sequence_next_start_seconds = 1;
    scene.entity(actor.str()).set(properties);
    Fixture f(project, scene.snapshot(), actor);
    AnimationPlaybackAccess::control(*f.animation, controller,
                                     {{"operation", "seek"}, {"position_seconds", .75}});
    const auto reference = f.scene.reference(actor.str());
    using Kind = AnimationPlaybackAccess::PlayerKind;
    const auto automatic = AnimationPlaybackAccess::status(*f.animation, reference);
    const auto skeletal = AnimationPlaybackAccess::status(*f.animation, reference, Kind::Skeletal);
    const auto transform =
        AnimationPlaybackAccess::status(*f.animation, reference, Kind::Transform);
    require(f.animation->playback(reference)->clip == first &&
                automatic.at("component") == "forge.transform_animator",
            "Mixed actor changed the public entity playback priority");
    require(automatic.at("players").at("forge.animator") == skeletal &&
                automatic.at("players").at("forge.transform_animator") == transform,
            "Mixed observations did not expose the existing typed owners");
    near(skeletal.at("duration"), 1.5);
    near(skeletal.at("blend_weight"), .5);
    near(skeletal.at("sequence_clips").at("duration_seconds"), 1);
    require(skeletal.at("sequence_clips").at("clip") == Json(forward) &&
                skeletal.at("sequence_clips").at("next").at("clip") == Json(strafe),
            "Skeletal observation borrowed the property clip identities");
    near(transform.at("duration"), 3);
    near(transform.at("blend_weight"), 0);
    near(transform.at("sequence_clips").at("duration_seconds"), 2);
    require(transform.at("sequence_clips").at("clip") == Json(first) &&
                automatic.at("sequence_clips") == transform.at("sequence_clips"),
            "Property observation borrowed the skeletal clip identities");
    near(f.entity().get<Light>().intensity, 1.75);
    near(f.pose().at("local")[0].at("translation")[0], .25);
    near(f.pose().at("local")[0].at("translation")[2], .75);
}
void skeletal_sequence_reimport_tests(const std::filesystem::path& project, const Json& source,
                                      EntityId actor, AssetId model, AssetId forward,
                                      AssetId strafe, AssetId child_asset,
                                      AssetImportService& imports) {
    EngineContext author;
    Scene scene(author.world());
    scene.restore_snapshot(source);
    const auto controller_id = configure_skeletal_sequence(scene, actor, forward, strafe);
    const auto controller = scene.reference(controller_id);
    Fixture f(project, scene.snapshot(), actor);
    AnimationPlaybackAccess::control(*f.animation, controller, {{"operation", "pause"}});
    AnimationPlaybackAccess::control(*f.animation, controller,
                                     {{"operation", "seek"}, {"position_seconds", .25}});
    flecs::entity_t child = 0;
    f.engine.world().world().each([&](flecs::entity entity, const ModelSource& value) {
        if (value.node.id == child_asset && f.engine.world().reference(entity.id()))
            child = entity.id();
    });
    require(child != 0, "Missing reimport fixture child joint");
    const auto old_revision = f.pose().at("model_revision").get<std::string>();
    const auto path = project / "Assets/LocomotionColumn.gltf";
    const auto bytes = read_bytes(path, 64 * 1024 * 1024);
    const std::string original(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    auto changed = Json::parse(original);
    bool edited = false;
    for (auto& channel : changed.at("animations")[0].at("channels"))
        if (channel.at("target").at("node") == 0 &&
            channel.at("target").at("path") == "translation") {
            channel["target"]["node"] = 1;
            edited = true;
        }
    require(edited, "Missing original Forward root channel");
    const auto reimport = [&](const std::string& text) {
        atomic_write(path, text);
        imports.submit(
            imports.prepare("Assets/LocomotionColumn.gltf"),
            [](auto& candidate, const auto& draft, const auto& catalog) {
                prepare_model_publication(candidate, draft, catalog);
            },
            [](const auto&, const auto&) {});
        require(imports.wait_idle(60s), "Skeletal reimport timed out");
        const auto outcomes = imports.poll();
        require(outcomes.size() == 1 && outcomes[0].published,
                outcomes.empty() ? "Missing skeletal reimport" : outcomes[0].diagnostic);
        auto catalog = AssetCatalog::open_project(project);
        const auto selection = load_model_selection(project, catalog, model);
        require(selection.bindings.at("/animations/0") == forward &&
                    selection.bindings.at("/animations/1") == strafe &&
                    selection.bindings.at("/nodes/1") == child_asset,
                "Skeletal source edit replaced stable imported member identities");
        return std::make_shared<const AssetCatalog>(std::move(catalog));
    };
    const auto catalog = reimport(changed.dump());
    f.animation->pose_validator([child](const auto& candidate) {
        if (candidate.at(child).local.translation.z > .01)
            throw std::runtime_error("Reject reimported child");
    });
    f.animation->catalog(catalog);
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    for (;;) {
        f.simulation.tick(.01f);
        const auto diagnostics = f.engine.services().diagnostics();
        if (std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& value) {
                return value.at("category") == "animation.reload" &&
                       value.at("text") == "Reject reimported child";
            }))
            break;
        require(std::chrono::steady_clock::now() < deadline,
                "Skeletal reload did not reach its prepared rejection");
        std::this_thread::sleep_for(1ms);
    }
    require(f.pose().at("model_revision") == old_revision,
            "Rejected reimport replaced last-good skeletal resources");
    near(f.animation->playback(controller)->time, .25);
    f.animation->pose_validator([child](const auto& candidate) {
        if (candidate.at(child).local.translation.z > .75)
            throw std::runtime_error("Reject future child");
    });
    AnimationPlaybackAccess::control(*f.animation, controller, {{"operation", "play"}});
    f.animation->catalog(catalog);
    f.simulation.tick(.25f);
    require(!f.pose().is_null() && f.pose().at("model_revision") == old_revision,
            "Skeletal reload adopted resources before the complete future candidate passed");
    near(f.animation->playback(controller)->time, .5);
    near(f.pose().at("local")[0].at("translation")[2], 1);
    f.animation->pose_validator({});
    f.animation->catalog(catalog);
    f.simulation.tick(.25f);
    require(!f.pose().is_null() && f.pose().at("model_revision") != old_revision,
            "Compatible skeletal reimport did not adopt after repair");
    near(f.pose().at("local")[1].at("translation")[2], .75);
    (void)reimport(original);
}
