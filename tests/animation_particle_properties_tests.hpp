void animation_particle_property_tests(const std::filesystem::path& scratch) {
    static_assert(unsigned(AnimationProperty::AudioPitch) == 7);
    static_assert(unsigned(AnimationProperty::ParticleSize) == 8);
    auto source = TransformAnimationSource::create(AssetId::generate());
    source.add_property(AnimationProperty::ParticleSize, .5);
    source.add_property(AnimationProperty::ParticleRed, 1);
    source.add_property(AnimationProperty::ParticleGreen, .25);
    source.add_property(AnimationProperty::ParticleBlue, 0);
    source.document["channels"] = Json::array();
    auto end = source.clip().keys.front().properties;
    end[unsigned(AnimationProperty::ParticleSize)] = 3;
    end[unsigned(AnimationProperty::ParticleRed)] = 0;
    end[unsigned(AnimationProperty::ParticleGreen)] = .75;
    end[unsigned(AnimationProperty::ParticleBlue)] = 1;
    source.set_key(animation_timebase, {}, false, {}, {}, end);
    source.set_property_curve(source.clip().keys.front().id, AnimationProperty::ParticleSize,
                              {true, 0, 0});
    source.validate();
    const auto clip = decode_transform_animation(source.cook(), source.asset());
    require(clip.channels == 0 && clip.properties == AnimationPropertyState::particle_mask,
            "Particle property-only cook lost named bindings");
    const auto half = clip.sample_properties(500000);
    near(half[8], 1.75);
    near(half[9], .5);
    near(half[10], .5);
    near(half[11], .5);
    auto held = source;
    held.document["keys"][0]["interpolation"] = "held";
    near(held.clip().sample_properties(999999)[8], .5);
    near(held.clip().sample_properties(1000000)[8], 3);
    for (double bad : {0., .009, 16.01, std::numeric_limits<double>::infinity()})
        rejects([&] { validate_animation_property(AnimationProperty::ParticleSize, bad); });
    for (double bad : {-0.01, 1.01, std::numeric_limits<double>::quiet_NaN()})
        rejects([&] { validate_animation_property(AnimationProperty::ParticleBlue, bad); });

    std::filesystem::create_directories(scratch / "Assets");
    auto lease = std::make_shared<ProjectLease>(scratch);
    auto document = TransformAnimationDocument::create(lease, "Assets/Appearance.animation.json");
    const auto id = document->source().asset();
    source.document["asset_id"] = id;
    document->edit(document->revision(), "Animate particle appearance",
                   [&](auto& j) { j = source.document; });
    document->save();
    AssetImportService service(lease, transform_animation_import_registry(),
                               {"windows", "d3d12", "desktop"});
    const auto receipt = publish(service, document->locator(), id);
    require(receipt.published && receipt.publication, "Particle property clip publication failed");
    const auto catalog = std::make_shared<const AssetCatalog>(receipt.publication->catalog);
    require(catalog->records().at(id).metadata.at("forge.import").at("importer_revision") ==
                "forge.animation.transform.v8",
            "Expanded bindings reused an old recipe identity");
    EngineContext authored;
    Scene scene(authored.world());
    const auto actor = authoring_command(scene, "entity.create", {{"name", "Keyframed emitter"}})
                           .at("selected")
                           .get<std::string>();
    ParticleEmitter emitter;
    emitter.effect = {AssetId::generate()};
    emitter.seed = 124;
    emitter.layers = 13;
    emitter.playback_speed = .75;
    emitter.visible = false;
    scene.entity(actor).set(emitter).set<TransformAnimator>({{id}});
    const auto saved = scene.document();
    auto preview = saved;
    require(apply_transform_animation_preview(preview, {scene.asset_id(), EntityId::parse(actor)},
                                              source.clip(), 500000),
            "Particle preview target missing");
    auto expected = saved;
    auto& fields = expected["entities"][0]["components"]["forge.particle_emitter"];
    fields["size_multiplier"] = 1.75;
    fields["tint_r"] = fields["tint_g"] = fields["tint_b"] = .5;
    require(preview == expected && scene.document() == saved,
            "Particle preview changed unbound fields or authored history");
    auto missing = saved;
    missing["entities"][0]["components"].erase("forge.particle_emitter");
    const auto before = missing;
    rejects([&] {
        apply_transform_animation_preview(missing, {scene.asset_id(), EntityId::parse(actor)},
                                          source.clip(), 500000);
    });
    require(missing == before, "Missing particle preview partially changed its input");

    Fixture live(scratch);
    live.animation->catalog(catalog);
    live.load(saved);
    live.ready();
    live.simulation.reset_presentation();
    near(live.scene.entity(actor).get<ParticleEmitter>().size_multiplier, .5);
    live.simulation.tick(.5f);
    auto actual = live.scene.entity(actor).get<ParticleEmitter>();
    near(actual.size_multiplier, 1.75);
    near(actual.tint_r, .5);
    near(actual.tint_g, .5);
    near(actual.tint_b, .5);
    require(actual.effect == emitter.effect && actual.seed == emitter.seed &&
                actual.playback_speed == emitter.playback_speed &&
                actual.layers == emitter.layers && actual.visible == emitter.visible,
            "Particle animation changed emission policy");
    const auto copied = live.simulation.presentation(.5);
    near(copied["entities"][0]["components"]["forge.particle_emitter"]["size_multiplier"], .890625);
    require(live.scene.entity(actor).get<ParticleEmitter>() == actual,
            "Particle presentation interpolation wrote into live ECS");
    const auto checkpoint = live.animation->checkpoint();
    Fixture recovered(scratch);
    recovered.animation->catalog(catalog);
    recovered.load(saved);
    recovered.ready();
    recovered.animation->restore(checkpoint);
    near(recovered.scene.entity(actor).get<ParticleEmitter>().size_multiplier, 1.75);
    near(recovered.simulation.presentation(
             0)["entities"][0]["components"]["forge.particle_emitter"]["size_multiplier"],
         1.75);
    auto invalid = actual;
    invalid.playback_speed = std::numeric_limits<double>::quiet_NaN();
    live.scene.entity(actor).set(invalid);
    rejects([&] { AnimationPropertyState::from(live.scene.entity(actor), clip.properties); });
    require(live.scene.entity(actor).get<ParticleEmitter>().size_multiplier ==
                actual.size_multiplier,
            "Invalid complete native candidate partially wrote animated appearance");
    const auto absent = authoring_command(scene, "entity.create", {{"name", "Missing emitter"}})
                            .at("selected")
                            .get<std::string>();
    scene.entity(absent).set<TransformAnimator>({{id}});
    Fixture rejected(scratch);
    rejected.animation->catalog(catalog);
    rejected.load(scene.document());
    rejects([&] { rejected.ready(); });
    require(rejected.scene.entity(actor).get<ParticleEmitter>() == emitter &&
                !rejected.scene.entity(absent).has<ParticleEmitter>(),
            "Missing target admitted partial initial animation or created a component");
}
