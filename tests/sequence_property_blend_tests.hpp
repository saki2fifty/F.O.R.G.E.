#pragma once
// Included in the existing sequence fixture; import and sample actual clip resources.
void sequence_property_blend_tests(const std::filesystem::path& root,
                                   const std::shared_ptr<ProjectLease>& lease,
                                   AssetImportService& imports) {
    const AnimationPropertyValues a{.6, 2, 1, .1, .2, .3, .1, 1, .5, .2, .3, .4, 0};
    const AnimationPropertyValues b{1, 6, 3, .3, .4, .5, .5, 2, 1.5, .4, .5, .6, 1};
    const AnimationPropertyValues c{1.4, 10, 7, .5, .6, .7, .7, 3, 2.5, .6, .7, .8, 2};
    const AnimationPropertyValues d{1.8, 14, 9, .7, .8, .9, .9, 4, 3.5, .8, .9, 1, 3};
    constexpr unsigned all_properties = (1u << animation_property_count) - 1;
    auto make = [&](const char* path, const auto& begin, const auto& end, bool transforms,
                    unsigned mask = (1u << animation_property_count) - 1, bool held = false) {
        return clip(lease, imports, path, false, false, animation_timebase, [&](auto& source) {
            for (const auto& field : animation_property_bindings())
                if (mask & animation_property_bit(field.property))
                    source.add_property(field.property, begin[unsigned(field.property)]);
            source.set_key(animation_timebase, {{4, 0, 0}, {}, {1, 1, 1}}, false, {}, {}, end);
            if (!transforms)
                source.document["channels"] = Json::array();
            if (held)
                source.document["keys"][0]["interpolation"] = "held";
        });
    };
    const auto primary = make("Assets/PropertyPairFirst.animation.json", a, b, true);
    const auto secondary = make("Assets/PropertyPairNext.animation.json", c, d, true);
    EngineContext author;
    Scene scene(author.world());
    const auto controller =
        authoring_command(scene, "entity.create", {{"recipe", "animation.sequence"}})
            .at("selected")
            .get<std::string>();
    const auto actor = authoring_command(scene, "entity.create").at("selected").get<std::string>();
    AnimationSequence sequence;
    sequence.duration = 2;
    scene.entity(controller).set(sequence);
    const EntityRef ref{scene.asset_id(), EntityId::parse(controller)};
    TransformAnimator track{{primary}};
    track.sequence = ref;
    track.sequence_next_clip = {secondary};
    track.sequence_next_start_seconds = .5;
    auto entity = scene.entity(actor);
    SpriteRenderer sprite;
    sprite.columns = 4;
    entity.set(track)
        .set(Camera{})
        .set(Light{})
        .set(AudioSource{})
        .set(ParticleEmitter{})
        .set(sprite);
    auto source = scene.snapshot();
    for (auto& item : source["entities"])
        if (item["id"] == actor)
            item["components"]["future.sequence_properties"] = {{"keep", {1, "opaque", nullptr}}};
    scene.restore_snapshot(source);
    const auto authored = scene.document();
    Runtime runtime(root, source);
    auto seek = [&](Runtime& value, double seconds) {
        AnimationPlaybackAccess::control(*value.animation, ref,
                                         {{"operation", "seek"}, {"position_seconds", seconds}});
    };
    auto verify = [&](const Json& snapshot, double first_time, double next_time, double weight,
                      bool reversed = false) {
        const auto& begin = reversed ? c : a;
        const auto& end = reversed ? d : b;
        const auto& incoming_begin = reversed ? a : c;
        const auto& incoming_end = reversed ? b : d;
        const auto& components = row(snapshot, actor).at("components");
        for (const auto& field : animation_property_bindings()) {
            const auto i = unsigned(field.property);
            const double expected =
                field.property == AnimationProperty::SpriteFrame
                    ? (weight < .5 ? (first_time < 1 ? begin[i] : end[i])
                                   : (next_time < 1 ? incoming_begin[i] : incoming_end[i]))
                    : (1 - weight) * (begin[i] + first_time * (end[i] - begin[i])) +
                          weight * (incoming_begin[i] +
                                    next_time * (incoming_end[i] - incoming_begin[i]));
            near(components.at(field.component).at(field.field).template get<double>(), expected);
        }
        check(components.at("future.sequence_properties") == Json{{"keep", {1, "opaque", nullptr}}},
              "Sequence property sampling changed opaque component data");
    };
    // Explicit sequence/local times and weights, independent of the runtime span helper.
    for (const auto& value : std::array<std::array<double, 4>, 8>{{{0, 0, 0, 0},
                                                                   {.25, .25, 0, 0},
                                                                   {.5, .5, 0, 0},
                                                                   {.75, .75, .25, .5},
                                                                   {1, 1, .5, 1},
                                                                   {1.25, 1, .75, 1},
                                                                   {1.5, 1, 1, 1},
                                                                   {2, 1, 1, 1}}}) {
        seek(runtime, value[0]);
        verify(runtime.scene.snapshot(), value[1], value[2], value[3]);
    }
    seek(runtime, .749999);
    check(runtime.scene.entity(actor).get<SpriteRenderer>().frame == 0,
          "Discrete sequence property switched before its midpoint");
    seek(runtime, .75);
    verify(runtime.scene.snapshot(), .75, .25, .5);
    near(runtime.scene.entity(actor).get<LocalTranslation>().x, 2);
    const auto checkpoint = runtime.animation->checkpoint();
    Runtime cold(root, source, false);
    cold.animation->restore(checkpoint);
    verify(cold.scene.snapshot(), .75, .25, .5);
    near(cold.animation->playback(ref)->time, .75);

    // Both clips must be admitted against the target before even the first pose.
    // At this time the secondary end frame is not sampled, but must still reject.
    const auto valid_sprite = runtime.scene.entity(actor).get<SpriteRenderer>();
    auto smaller = valid_sprite;
    smaller.columns = 2;
    smaller.frame = 0;
    runtime.scene.entity(actor).set(smaller);
    rejects([&] { runtime.animation->prepare_initial_pose(); });
    near(runtime.scene.entity(actor).get<Light>().intensity, 5);
    runtime.scene.entity(actor).set(valid_sprite);
    for (unsigned attempt = 0; !runtime.animation->prepare_initial_pose(); ++attempt) {
        check(attempt < 1000, "Corrected property target did not recover");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    near(runtime.animation->playback(ref)->time, .75);
    verify(runtime.scene.snapshot(), .75, .25, .5);
    // Authoring is isolated from every runtime sample and rejected target edit.
    check(scene.document() == authored, "Property crossfade changed authored Scene content");

    const auto property_primary = make("Assets/PropertyOnlyFirst.animation.json", a, b, false);
    const auto property_next = make("Assets/PropertyOnlyNext.animation.json", c, d, false);
    auto property_source = source;
    for (auto& item : property_source["entities"])
        if (item["id"] == actor) {
            item["components"]["forge.transform_animator"]["clip"] = property_primary;
            item["components"]["forge.transform_animator"]["sequence_next_clip"] = property_next;
        }
    Runtime properties(root, property_source);
    seek(properties, .75);
    verify(properties.scene.snapshot(), .75, .25, .5);
    near(properties.scene.entity(actor).get<LocalTranslation>().x, 0);

    const auto collection_source = [&](Json document, AssetId first, AssetId next) {
        for (auto& item : document["entities"])
            if (item["id"] == controller)
                item["components"]["forge.animation_sequence"]["duration"] = 5;
            else if (item["id"] == actor) {
                auto& track = item["components"]["forge.transform_animator"];
                track["sequence_extra_clips"] = Json::array();
                for (std::size_t i = 2; i < sequence_clip_limit; ++i)
                    track["sequence_extra_clips"].push_back(
                        {{"clip", i % 2 ? next : first}, {"start_seconds", .5 * i}});
            }
        return document;
    };
    const auto full_collection = collection_source(source, primary, secondary);
    for (const auto& document :
         {full_collection, collection_source(property_source, property_primary, property_next)}) {
        Runtime many(root, document);
        for (const double time : {.75, 1.25, 1.75, 2.25, 2.75, 3.25, 3.749999, 3.75, 4.25, 4.5}) {
            seek(many, time);
            const auto active = std::clamp(int(std::floor(time * 2)) - 1, 0, 6);
            const auto first_time = std::clamp(time - .5 * active, 0., 1.);
            const auto next_time = std::clamp(time - .5 * (active + 1), 0., 1.);
            const auto weight = std::clamp((time - .5 * (active + 1)) * 2, 0., 1.);
            verify(many.scene.snapshot(), first_time, next_time, weight, active % 2);
        }
    }
    const auto held_last =
        make("Assets/PropertyCollectionHeld.animation.json", c, d, true, all_properties, true);
    auto held_collection = full_collection;
    for (auto& item : held_collection["entities"])
        if (item["id"] == actor)
            item["components"]["forge.transform_animator"]["sequence_extra_clips"].back()["clip"] =
                held_last;
    Runtime held_many(root, held_collection);
    seek(held_many, 3.75);
    verify(held_many.scene.snapshot(), .75, 0, .5);
    seek(held_many, 0);
    for (unsigned tick = 0; tick < 18; ++tick)
        held_many.simulation.tick(.25);
    verify(held_many.scene.snapshot(), 1, 1, 1);
    verify(held_many.simulation.presentation(.5), 1, 1, 1);
    near(row(held_many.simulation.presentation(.5), actor).at("world_affine")[3], 4);

    Runtime fixed(root, source);
    for (unsigned i = 0; i < 3; ++i)
        fixed.simulation.tick(.25);
    verify(fixed.scene.snapshot(), .75, .25, .5);
    fixed.simulation.tick(.25);
    verify(fixed.scene.snapshot(), 1, .5, 1);
    verify(fixed.simulation.presentation(.5), .875, .375, .75);
    verify(fixed.scene.snapshot(), 1, .5, 1);

    auto gap_source = source;
    for (auto& item : gap_source["entities"])
        if (item["id"] == actor)
            item["components"]["forge.transform_animator"]["sequence_next_start_seconds"] = 1.25;
    Runtime gap(root, gap_source);
    seek(gap, 1.125);
    verify(gap.scene.snapshot(), 1, 0, 0);
    seek(gap, 1.25);
    verify(gap.scene.snapshot(), 1, 0, 1);
    Runtime gap_fixed(root, gap_source);
    for (unsigned i = 0; i < 5; ++i)
        gap_fixed.simulation.tick(.25);
    verify(gap_fixed.simulation.presentation(.5), 1, 0, 1);

    auto loop_source = source;
    for (auto& item : loop_source["entities"])
        if (item["id"] == controller)
            item["components"]["forge.animation_sequence"]["loop"] = true;
    Runtime looping(root, loop_source);
    for (unsigned i = 0; i < 8; ++i)
        looping.simulation.tick(.25);
    verify(looping.scene.snapshot(), 0, 0, 0);
    verify(looping.simulation.presentation(.5), 0, 0, 0);

    const auto held_next =
        make("Assets/PropertyHeldNext.animation.json", c, d, true, all_properties, true);
    auto held_source = source;
    for (auto& item : held_source["entities"])
        if (item["id"] == actor)
            item["components"]["forge.transform_animator"]["sequence_next_clip"] = held_next;
    Runtime held(root, held_source);
    seek(held, .75);
    verify(held.scene.snapshot(), .75, 0, .5);
    Runtime held_fixed(root, held_source);
    for (unsigned i = 0; i < 6; ++i)
        held_fixed.simulation.tick(.25);
    verify(held_fixed.simulation.presentation(.5), 1, 1, 1);

    const auto parent = EntityId::generate();
    auto nested_source = source;
    auto parent_row = row(source, controller);
    parent_row["id"] = parent;
    parent_row["name"] = "Property sequence parent";
    parent_row["components"]["forge.animation_sequence"]["duration"] = 4;
    for (auto& item : nested_source["entities"])
        if (item["id"] == controller) {
            auto& config = item["components"]["forge.animation_sequence"];
            config["sequence"] = EntityRef{scene.asset_id(), parent};
            config["sequence_start_seconds"] = 1;
        }
    nested_source["entities"].push_back(std::move(parent_row));
    Runtime nested(root, nested_source);
    const EntityRef parent_ref{scene.asset_id(), parent};
    AnimationPlaybackAccess::control(*nested.animation, parent_ref,
                                     {{"operation", "seek"}, {"position_seconds", 1.75}});
    verify(nested.scene.snapshot(), .75, .25, .5);
    near(nested.animation->playback(ref)->time, .75);
    const auto nested_checkpoint = nested.animation->checkpoint();
    Runtime nested_cold(root, nested_source, false);
    nested_cold.animation->restore(nested_checkpoint);
    verify(nested_cold.scene.snapshot(), .75, .25, .5);
    near(nested_cold.animation->playback(parent_ref)->time, 1.75);

    const auto intensity = animation_property_bit(AnimationProperty::LightIntensity);
    const auto subset_first =
        make("Assets/PropertySubsetFirst.animation.json", a, b, false, intensity);
    const auto subset_next =
        make("Assets/PropertySubsetNext.animation.json", c, d, false, intensity);
    auto subset_source = property_source;
    for (auto& item : subset_source["entities"])
        if (item["id"] == actor) {
            item["components"]["forge.transform_animator"]["clip"] = subset_first;
            item["components"]["forge.transform_animator"]["sequence_next_clip"] = subset_next;
        }
    Runtime subset(root, subset_source);
    seek(subset, .75);
    near(subset.scene.entity(actor).get<Light>().intensity, 5);
    const auto subset_sample = subset.scene.snapshot();
    for (const auto& field : animation_property_bindings())
        if (field.property != AnimationProperty::LightIntensity)
            check(row(subset_sample, actor).at("components").at(field.component).at(field.field) ==
                      row(source, actor).at("components").at(field.component).at(field.field),
                  "Property subset changed an unanimated native field");
    auto mismatch = property_source;
    for (auto& item : mismatch["entities"])
        if (item["id"] == actor)
            item["components"]["forge.transform_animator"]["sequence_next_clip"] = subset_next;
    bool mask_rejected = false;
    try {
        Runtime invalid(root, mismatch);
    } catch (const std::exception& error) {
        mask_rejected = std::string_view(error.what()).find("matching transform channels") !=
                        std::string_view::npos;
    }
    check(mask_rejected, "Mismatched property masks did not report pair admission failure");

    TransformAnimationDocument next_document(lease, "Assets/PropertyPairNext.animation.json");
    auto publish_next_frame = [&](unsigned frame) {
        next_document.edit_source(
            next_document.revision(), "Next frame revision", [&](TransformAnimationSource& value) {
                auto end = d;
                end[unsigned(AnimationProperty::SpriteFrame)] = frame;
                value.set_key(animation_timebase, {{4, 0, 0}, {}, {1, 1, 1}}, false, {}, {}, end);
            });
        next_document.save();
        imports.submit(
            imports.prepare(next_document.locator(), {}, secondary),
            [](auto& candidate, const auto& plan, const auto&) {
                prepare_transform_animation_publication(candidate, plan);
            },
            [](const auto&, const auto&) {});
        check(imports.wait_idle(std::chrono::seconds(20)), "Next revision publication timed out");
        const auto outcomes = imports.poll();
        check(outcomes.size() == 1 && outcomes.front().published,
              "Next revision was not published through the asset owner");
        runtime.animation->catalog(
            std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
    };
    publish_next_frame(4);
    bool revision_rejected = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!revision_rejected) {
        try {
            check(!runtime.animation->prepare_initial_pose(),
                  "Invalid secondary revision was admitted");
        } catch (const std::exception& error) {
            revision_rejected = std::string_view(error.what()).find("target Sprite frame count") !=
                                std::string_view::npos;
            if (!revision_rejected)
                throw;
        }
        near(runtime.scene.entity(actor).get<Light>().intensity, 5);
        check(std::chrono::steady_clock::now() < deadline, "Next revision admission timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    publish_next_frame(3);
    const auto recovery_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    bool revision_ready = false;
    while (!revision_ready) {
        try {
            revision_ready = runtime.animation->prepare_initial_pose();
        } catch (const std::exception& error) {
            // The existing owner retains its last diagnostic while a new
            // selection loads. Only that known failure is permitted here.
            if (std::string_view(error.what()).find("target Sprite frame count") ==
                std::string_view::npos)
                throw;
        }
        near(runtime.scene.entity(actor).get<Light>().intensity, 5);
        check(std::chrono::steady_clock::now() < recovery_deadline,
              "Valid next revision did not recover");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    verify(runtime.scene.snapshot(), .75, .25, .5);
    near(runtime.animation->playback(ref)->time, .75);
    std::cout << "Sequence matching property pairs: all scalar controls, held frames, "
                 "fixed/presentation/seek/gap/loop, property-only/combined sampling, "
                 "nested/subset/held curves, target/revision admission and cold recovery passed\n";
}
