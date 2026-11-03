#pragma once
// Uses the existing real Scene, imported clip and runtime fixtures.
void nested_sequence_tests(const std::filesystem::path& root, AssetId motion, AssetId pulse) {
    EngineContext author;
    Scene scene(author.world());
    auto create = [&](const char* recipe, const char* name) {
        return authoring_command(scene, "entity.create", {{"recipe", recipe}, {"name", name}})
            .at("selected")
            .get<std::string>();
    };
    const auto parent = create("animation.sequence", "Parent");
    const auto child = create("animation.sequence", "Child");
    const auto grand = create("animation.sequence", "Grandchild");
    const auto actor = create("primitive.0", "Nested cube");
    const auto lamp = create("primitive.0", "Nested property");
    const auto fallback = create("render.camera", "Fallback");
    const auto shot_camera = create("render.camera", "Nested view");
    const auto shot = create("animation.camera_shot", "Nested shot");
    auto ref = [&](const std::string& id) {
        return EntityRef{scene.asset_id(), EntityId::parse(id)};
    };
    AnimationSequence outer;
    outer.duration = 4;
    scene.entity(parent).set(outer);
    AnimationSequence inner;
    inner.duration = 2;
    inner.sequence = ref(parent);
    inner.sequence_start_seconds = .5;
    inner.loop = true;
    inner.playback_speed = 4;
    inner.play_on_start = false;
    scene.entity(child).set(inner);
    auto leaf = inner;
    leaf.duration = 1;
    leaf.sequence = ref(child);
    scene.entity(grand).set(leaf);
    TransformAnimator track{{motion}};
    track.sequence = ref(grand);
    scene.entity(actor).set(track).set<LocalTranslation>({99, 0, 0});
    track.clip = {pulse};
    scene.entity(lamp).set(track).set(Light{});
    auto camera = scene.entity(shot_camera).get<Camera>();
    camera.enabled = false;
    scene.entity(shot_camera).set(camera);
    scene.entity(shot).set(CameraShot{ref(grand), ref(shot_camera), 0, 1, true});
    const auto source = scene.snapshot();
    Runtime runtime(root, source);
    auto seek = [&](Runtime& world, double time) {
        AnimationPlaybackAccess::control(*world.animation, ref(parent),
                                         {{"operation", "seek"}, {"position_seconds", time}});
    };
    auto position = [&](const Runtime& world) {
        return world.scene.entity(actor).get<LocalTranslation>().x;
    };
    auto selected_camera = [&](Runtime& world, double alpha = 1) {
        const auto render = extract_render_scene(world.simulation.presentation(alpha));
        check(render.cameras.size() == 1, "Nested camera composition is ambiguous");
        return render.cameras.front().entity;
    };
    for (const auto [time, expected] : std::array<std::array<double, 2>, 8>{
             {{0, 0}, {.75, 0}, {1, 0}, {1.25, 1}, {1.75, 3}, {2, 4}, {2.5, 4}, {4, 4}}}) {
        seek(runtime, time);
        near(position(runtime), expected);
        check(selected_camera(runtime) ==
                  EntityId::parse(time >= 1 && time <= 2 ? shot_camera : fallback),
              "Nested camera ignored its placement boundary");
    }
    seek(runtime, 1.75);
    near(runtime.animation->playback(ref(child))->time, 1.25);
    near(runtime.animation->playback(ref(grand))->time, .75);
    near(runtime.animation->playback(ref(grand))->speed, 1);
    check(runtime.animation->playback(ref(grand))->playing &&
              !runtime.animation->playback(ref(grand))->loop &&
              !runtime.animation->control(ref(child), AnimationControl::Pause, 100) &&
              !runtime.animation->seek(ref(grand), .25, 100),
          "Bound controller acquired standalone controls/rate/loop intent");
    const auto checkpoint = runtime.animation->checkpoint();
    check(checkpoint.at("version") == 7, "Nested recovery omitted its exact profile");
    Runtime cold(root, source, false);
    cold.animation->restore(checkpoint);
    near(position(cold), 3);
    near(cold.animation->playback(ref(child))->time, 1.25);
    for (const auto field : {"sequence_start_seconds", "time_us", "playing"}) {
        auto bad = checkpoint;
        for (auto& entry : bad["sequence_entries"])
            if (entry.at("entity") == Json(ref(grand))) {
                if (std::string_view(field) == "playing")
                    entry[field] = true;
                else
                    entry[field] = entry.at(field).get<double>() + .1;
            }
        rejects([&] { cold.animation->restore(bad); });
        near(position(cold), 3);
        near(cold.animation->playback(ref(parent))->time, 1.75);
    }
    auto old = checkpoint;
    old["version"] = 6;
    rejects([&] { cold.animation->restore(old); });
    seek(runtime, .75);
    runtime.simulation.tick(.5);
    near(row(runtime.simulation.presentation(.25), lamp)
             .at("components")
             .at("forge.light")
             .at("intensity"),
         1);
    near(row(runtime.simulation.presentation(.75), lamp)
             .at("components")
             .at("forge.light")
             .at("intensity"),
         1.25);
    check(selected_camera(runtime, .25) == EntityId::parse(fallback) &&
              selected_camera(runtime, .75) == EntityId::parse(shot_camera),
          "Nested presentation clamped before sampling the root interval");
    auto changed = [&](const std::string& entity, const Json& values) {
        auto result = source;
        for (auto& entry : result["entities"])
            if (entry.at("id") == entity)
                entry["components"]["forge.animation_sequence"].update(values);
        return result;
    };
    for (const auto& bad : std::vector<Json>{
             changed(child, {{"sequence", ref(child)}}),
             changed(parent, {{"sequence", ref(grand)}}),
             changed(child, {{"sequence", ref(actor)}}),
             changed(child, {{"sequence", EntityRef{scene.asset_id(), EntityId::generate()}}}),
             changed(child,
                     {{"sequence", EntityRef{AssetId::generate(), EntityId::parse(parent)}}}),
             changed(child, {{"sequence_start_seconds", 4.1}})}) {
        Runtime rejected(root, bad, false);
        rejects([&] { rejected.animation->prepare_initial_pose(); });
        near(position(rejected), 99);
    }
    Runtime disabled(root, changed(parent, {{"enabled", false}}));
    check(!disabled.animation->playback(ref(child)) && !disabled.animation->playback(ref(grand)),
          "Disabled parent retained derived clocks");
    near(position(disabled), 99);
    for (const auto malformed :
         {EntityRef{scene.asset_id(), {}}, EntityRef{{}, EntityId::parse(parent)}}) {
        Runtime rejected(root, source, false);
        auto config = rejected.scene.entity(child).get<AnimationSequence>();
        config.sequence = malformed;
        rejected.scene.entity(child).set(config);
        rejects([&] { rejected.animation->prepare_initial_pose(); });
        near(position(rejected), 99);
    }
    check(selected_camera(disabled) == EntityId::parse(fallback),
          "Disabled parent retained its nested camera director");
    auto extended = changed(grand, {{"duration", 4}});
    Runtime truncated(root, extended);
    seek(truncated, 3.5);
    near(truncated.animation->playback(ref(grand))->time, 1.5);
    near(position(truncated), 4);
    auto invalid_live = runtime.scene.entity(child).get<AnimationSequence>();
    invalid_live.sequence = ref(grand);
    const auto held = position(runtime);
    runtime.scene.entity(child).set(invalid_live);
    rejects([&] { runtime.animation->prepare_initial_pose(); });
    near(position(runtime), held);
    auto paired = source;
    for (auto& entry : paired["entities"])
        if (entry.at("id") == actor) {
            auto& values = entry["components"]["forge.transform_animator"];
            values["sequence_next_clip"] = motion;
            values["sequence_next_start_seconds"] = .5;
        }
    Runtime pair(root, paired);
    seek(pair, 1.75);
    near(position(pair), 2);
    const auto pair_checkpoint = pair.animation->checkpoint();
    check(pair_checkpoint.at("version") == 7, "Nested pair lost its exact recovery profile");
    Runtime pair_cold(root, paired, false);
    pair_cold.animation->restore(pair_checkpoint);
    near(position(pair_cold), 2);
    check(pair_cold.animation->checkpoint() == pair_checkpoint,
          "Nested pair cold recovery changed selected clips/derived clocks");

    // Reference adapters belong to the same Scene/Prefab owner as other native links.
    EngineContext persisted_engine;
    Scene persisted(persisted_engine.world());
    persisted.reset(source);
    persisted.reparent_entity(child, parent, ReparentMode::KeepLocal);
    persisted.reparent_entity(grand, child, ReparentMode::KeepLocal);
    for (const auto& id : {actor, lamp, shot_camera, shot})
        persisted.reparent_entity(id, grand, ReparentMode::KeepLocal);
    const auto copy = persisted.duplicate_subtree(parent);
    const auto copied_child = named(persisted, "Child", copy);
    const auto copied_grand = named(persisted, "Grandchild", copied_child);
    check(persisted.entity(copied_child).get<AnimationSequence>().sequence ==
                  EntityRef{persisted.asset_id(), EntityId::parse(copy)} &&
              persisted.entity(copied_grand).get<AnimationSequence>().sequence ==
                  EntityRef{persisted.asset_id(), EntityId::parse(copied_child)},
          "Nested subtree duplicate did not remap controller parents");
    const auto detached = persisted.duplicate_subtree(grand);
    check(persisted.entity(detached).get<AnimationSequence>().sequence == ref(child),
          "Child-only duplicate retargeted its external parent");
    PrefabLibrary library(root);
    const auto definition = create_prefab_source(persisted, parent);
    check(definition.source.at("version") == 3, "Nested Prefab omitted portable member links");
    const auto prefab = library.create(persisted, definition, "NestedSequence.prefab.json");
    const auto verify = [&](const Scene& target, const std::string& instance) {
        const auto inner_id = named(target, "Child", instance);
        const auto leaf_id = named(target, "Grandchild", inner_id);
        check(target.entity(inner_id).get<AnimationSequence>().sequence ==
                      EntityRef{target.asset_id(), EntityId::parse(instance)} &&
                  target.entity(leaf_id).get<AnimationSequence>().sequence ==
                      EntityRef{target.asset_id(), EntityId::parse(inner_id)} &&
                  target.entity(named(target, "Nested cube", leaf_id))
                          .get<TransformAnimator>()
                          .sequence == EntityRef{target.asset_id(), EntityId::parse(leaf_id)},
              "Nested Prefab reference crossed instances or lost an internal controller");
    };
    const auto first_instance = instantiate_prefab(persisted, prefab);
    const auto second_instance = instantiate_prefab(persisted, prefab);
    verify(persisted, first_instance);
    verify(persisted, second_instance);
    persisted.save(root / "NestedSequence.scene.json");
    EngineContext reopened_engine;
    Scene reopened(reopened_engine.world());
    library.load_scene(reopened, read_scene_file(root / "NestedSequence.scene.json"));
    verify(reopened, first_instance);
    verify(reopened, second_instance);

    EngineContext budget_engine;
    Scene budget(budget_engine.world());
    EntityRef previous;
    for (unsigned depth = 0; depth < 65; ++depth) {
        const auto id =
            authoring_command(budget, "entity.create", {{"recipe", "animation.sequence"}})
                .at("selected")
                .get<std::string>();
        AnimationSequence config;
        config.sequence = previous;
        budget.entity(id).set(config);
        previous = {budget.asset_id(), EntityId::parse(id)};
        if (depth == 63) {
            Runtime at_limit(root, budget.snapshot());
            near(at_limit.animation->playback(previous)->time, 0);
        }
    }
    Runtime over_limit(root, budget.snapshot(), false);
    rejects([&] { over_limit.animation->prepare_initial_pose(); });
    std::cout
        << "Nested sequences: derived clocks, controls, cold recovery, camera/scalar boundaries, "
           "invalid graphs, disabled chains and ancestor truncation passed\n";
}
