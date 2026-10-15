void animation_property_tests(const std::filesystem::path& scratch) {
    auto source = TransformAnimationSource::create(AssetId::generate());
    const auto first = source.clip().keys.front().id;
    source.document["keys"][0]["extension"] = "retain";
    source.add_property(AnimationProperty::CameraFov, std::numbers::pi / 6);
    source.add_property(AnimationProperty::CameraHeight, 4);
    source.add_property(AnimationProperty::LightIntensity, 2);
    source.add_property(AnimationProperty::LightRed, .1);
    source.add_property(AnimationProperty::LightGreen, .2);
    source.add_property(AnimationProperty::LightBlue, .3);
    auto end = source.clip().keys.front().properties;
    end = {std::numbers::pi / 2, 12, 10, .9, .6, .5};
    const auto middle = source.set_key(animation_timebase, {}, false, {}, {}, end);
    const auto initial_values = source.clip().keys.front().properties;
    source.set_key(2 * animation_timebase, {}, false, {}, {}, initial_values);
    source.document["channels"] = Json::array();
    source.validate();
    const auto admitted = source.document;
    const auto clip = source.clip();
    require(clip.channels == 0 && clip.properties == 63 && source.document["version"] == 3,
            "Property-only clip mask/version differs");
    const auto half = clip.sample_properties(500000);
    for (unsigned i = 0; i < animation_property_count; ++i)
        near(half[i], (initial_values[i] + end[i]) / 2);
    near(clip.sample_properties(-1)[2], 2);
    near(clip.sample_properties(3000000)[2], 2);
    rejects([&] { clip.sample_properties(std::numeric_limits<double>::quiet_NaN()); });
    auto held = source;
    held.document["keys"][0]["interpolation"] = "held";
    near(held.clip().sample_properties(999999)[2], 2);
    near(held.clip().sample_properties(1000000)[2], 10);
    auto mixed = source;
    mixed.document["channels"] = {"forge.local_translation"};
    mixed.set_key(0, {}, false, first, TransformAnimationCurves{true});
    require(mixed.document["version"] == 3 && mixed.clip().properties == 63,
            "Vector curve edit downgraded properties");
    mixed.set_tangent_preset(std::array{first, middle}, TransformAnimationTangentPreset::Flat);
    require(mixed.document["version"] == 3, "Tangent edit downgraded source3");
    mixed.retime_duration(4000000);
    require(mixed.document["keys"][0]["extension"] == "retain" &&
                mixed.clip().keys[1].properties == end && mixed.clip().keys[1].id == middle,
            "Retiming erased property values, identities or extension");
    const auto copies = mixed.duplicate_keys(std::array{middle}, 1000000);
    require(mixed.clip().keys[2].id == copies[0] && mixed.clip().keys[2].properties == end,
            "Duplicated key lost scalar values");
    auto removed = source;
    for (const auto& binding : animation_property_bindings()) {
        if (!(removed.clip().properties & animation_property_bit(binding.property)))
            continue;
        if (binding.property == AnimationProperty::LightBlue) {
            const auto before = removed.document;
            rejects([&] { removed.erase_property(binding.property); });
            require(removed.document == before, "Removing final channel changed source");
        } else
            removed.erase_property(binding.property);
    }
    for (unsigned kind = 0; kind < 9; ++kind) {
        auto bad = source;
        switch (kind) {
        case 0:
            bad.document["version"] = 2;
            break;
        case 1:
            bad.document["properties"].push_back("forge.light.intensity");
            break;
        case 2:
            bad.document["properties"][0] = "forge.camera.order";
            break;
        case 3:
            bad.document["keys"][0]["values"].erase("forge.light.intensity");
            break;
        case 4:
            bad.document["keys"][0]["values"]["extra"] = 4;
            break;
        case 5:
            bad.document["keys"][0]["values"]["forge.camera.vertical_fov"] = 0;
            break;
        case 6:
            bad.document["keys"][0]["values"]["forge.light.color_r"] = 1.001;
            break;
        case 7:
            bad.document["keys"][0]["values"]["forge.light.intensity"] = true;
            break;
        case 8:
            bad.document["keys"][0]["values"]["forge.light.intensity"] = 1000000001.;
            break;
        }
        rejects([&] { bad.validate(); });
    }
    auto bad_values = end;
    bad_values[0] = std::numbers::pi;
    rejects([&] { source.set_key(500000, {}, false, {}, {}, bad_values); });
    require(source.document == admitted, "Failed key write changed source");
    for (unsigned version : {1, 2}) {
        auto legacy = TransformAnimationSource::create(AssetId::generate());
        auto document = legacy.document;
        document["format"] = "forge.transform-clip";
        document["version"] = version;
        const auto text = document.dump();
        const auto old_clip =
            decode_transform_animation(std::as_bytes(std::span(text)), legacy.asset());
        require(old_clip.properties == 0 && old_clip.channels == 7, "Old clip changed channels");
    }
    require(decode_transform_animation(source.cook(), source.asset()).properties == 63,
            "Cooked version3 lost bindings");

    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root / "Assets");
    auto lease = std::make_shared<ProjectLease>(root);
    auto document = TransformAnimationDocument::create(lease, "Assets/Properties.animation.json");
    const auto id = document->source().asset();
    source.document["asset_id"] = id;
    const auto original = document->source().document;
    document->edit(document->revision(), "Animate properties",
                   [&](auto& j) { j = source.document; });
    document->undo();
    require(document->source().document == original, "Property source Undo failed");
    document->redo();
    document->save();
    AssetImportService service(lease, transform_animation_import_registry(),
                               {"windows", "d3d12", "desktop"});
    const auto receipt = publish(service, document->locator(), id);
    require(receipt.published && receipt.publication, "Property clip publication failed");
    const auto catalog = std::make_shared<const AssetCatalog>(receipt.publication->catalog);
    require(catalog->records().at(id).metadata["forge.import"]["output_version"] == 5,
            "Property clip selected wrong cook version");
    EngineContext authored;
    Scene scene(authored.world());
    const auto actor = authoring_command(scene, "entity.create",
                                         {{"name", "Animated camera and light"}})["selected"]
                           .get<std::string>();
    for (const char* component :
         {"forge.camera", "forge.light", "forge.transform_animator", "forge.physics_body"})
        authoring_command(scene, "component.add", {{"entity", actor}, {"component", component}});
    authoring_command(scene, "property.set",
                      {{"entity", actor},
                       {"component", "forge.transform_animator"},
                       {"field", "clip"},
                       {"value", id}});
    const auto saved = scene.document();
    auto preview = scene.effective_document();
    require(apply_transform_animation_preview(preview, {scene.asset_id(), EntityId::parse(actor)},
                                              source.clip(), 500000),
            "Property preview missing target");
    near(preview["entities"][0]["components"]["forge.light"]["intensity"].get<double>(), 6);
    require(scene.document() == saved, "Property preview mutated authored Scene");
    auto missing = saved;
    missing["entities"][0]["components"].erase("forge.light");
    const auto missing_before = missing;
    rejects([&] {
        apply_transform_animation_preview(missing, {scene.asset_id(), EntityId::parse(actor)},
                                          source.clip(), 500000);
    });
    require(missing == missing_before, "Missing preview component caused partial substitution");

    Fixture live(root);
    live.animation->catalog(catalog);
    live.load(saved);
    // Property-only playback coexists with a dynamic body's transform ownership.
    auto body = live.scene.entity(actor).get<PhysicsBody>();
    body.motion = 2;
    live.scene.entity(actor).set<PhysicsBody>(body);
    live.ready();
    live.simulation.reset_presentation();
    near(live.scene.entity(actor).get<Light>().intensity, 2);
    live.simulation.tick(.5f);
    near(live.scene.entity(actor).get<Light>().intensity, 6);
    const auto before_presentation = live.scene.entity(actor).get<Light>();
    const auto presented = live.simulation.presentation(.5);
    near(presented["entities"][0]["components"]["forge.light"]["intensity"].get<double>(), 4);
    near(presented["entities"][0]["components"]["forge.camera"]["vertical_fov"].get<double>(),
         std::numbers::pi / 4);
    require(live.scene.entity(actor).get<Light>() == before_presentation,
            "Presentation changed live component");
    const auto checkpoint = live.animation->checkpoint();
    Fixture recovered(root);
    recovered.animation->catalog(catalog);
    recovered.load(saved);
    recovered.ready();
    recovered.animation->restore(checkpoint);
    near(recovered.scene.entity(actor).get<Light>().intensity, 6);
    near(recovered.simulation
             .presentation(0)["entities"][0]["components"]["forge.light"]["intensity"]
             .get<double>(),
         6);
    recovered.simulation.tick(.5f);
    near(recovered.scene.entity(actor).get<Light>().intensity, 10);
    live.simulation.reset_presentation();
    near(live.simulation.presentation(0)["entities"][0]["components"]["forge.light"]["intensity"]
             .get<double>(),
         6);
    auto config = live.scene.entity(actor).get<TransformAnimator>();
    config.enabled = false;
    live.scene.entity(actor).set<TransformAnimator>(config);
    live.animation->synchronize();
    require(live.animation->presentation_properties(live.scene.entity(actor).id(), .5).is_null(),
            "Disabled property animator remained active");
    near(live.scene.entity(actor).get<Light>().intensity, 6);
    auto two_players = saved;
    auto second_row = two_players["entities"][0];
    const auto second_id = EntityId::generate().str();
    second_row["id"] = second_id;
    second_row["name"] = "Independent property player";
    second_row["components"]["forge.transform_animator"]["play_on_start"] = false;
    two_players["entities"].push_back(second_row);
    auto missing_native = two_players;
    missing_native["entities"][1]["components"].erase("forge.light");
    Fixture invalid(root);
    invalid.animation->catalog(catalog);
    invalid.load(missing_native);
    rejects([&] { invalid.ready(); });
    near(invalid.scene.entity(actor).get<Camera>().vertical_fov, std::numbers::pi / 3);
    near(invalid.scene.entity(actor).get<Light>().intensity, 1);
    Fixture independent(root);
    independent.animation->catalog(catalog);
    independent.load(two_players);
    independent.ready();
    independent.simulation.tick(.5f);
    near(independent.scene.entity(actor).get<Light>().intensity, 6);
    near(independent.scene.entity(second_id).get<Light>().intensity, 2);
    // Same-endpoint loop crosses the seam continuously on the owner's semantic clock.
    independent.simulation.tick(1.25f);
    independent.simulation.tick(.5f);
    near(independent.simulation
             .presentation(.5)["entities"][0]["components"]["forge.light"]["intensity"]
             .get<double>(),
         2);

    auto discontinuous = source;
    discontinuous.document["keys"][0]["interpolation"] = "held";
    discontinuous.set_key(2 * animation_timebase, {}, false, {}, {}, end);
    document->edit(document->revision(), "Held property boundary",
                   [&](auto& j) { j = discontinuous.document; });
    document->save();
    const auto newer = publish(service, document->locator(), id);
    require(newer.published, "Held property replacement publication failed");
    Fixture snapped(root);
    snapped.animation->catalog(std::make_shared<const AssetCatalog>(newer.publication->catalog));
    snapped.load(saved);
    snapped.ready();
    snapped.simulation.tick(.75f);
    near(snapped.scene.entity(actor).get<Light>().intensity, 2);
    snapped.scene.entity(actor).set<LocalTranslation>({8, 0, 0});
    snapped.simulation.tick(.5f);
    near(snapped.simulation.presentation(0)["entities"][0]["components"]["forge.light"]["intensity"]
             .get<double>(),
         10);
    near(snapped.simulation.presentation(.5)["entities"][0]["world_affine"][3].get<double>(), 4);
    snapped.simulation.tick(.75f);
    near(
        snapped.simulation.presentation(.5)["entities"][0]["components"]["forge.light"]["intensity"]
            .get<double>(),
        2);
    std::cout << "Typed property source, sampling, preview, fixed runtime/presentation and "
                 "recovery PASS\n";
}
