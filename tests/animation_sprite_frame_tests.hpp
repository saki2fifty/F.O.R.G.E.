void animation_sprite_frame_tests(const std::filesystem::path& scratch) {
    constexpr auto frame = AnimationProperty::SpriteFrame;
    constexpr auto index = unsigned(frame);
    auto source = TransformAnimationSource::create(AssetId::generate());
    source.add_property(frame, 0);
    source.add_property(AnimationProperty::CameraHeight, 2);
    source.document["channels"] = Json::array();
    auto values = source.clip().keys.front().properties;
    values[index] = 3;
    values[unsigned(AnimationProperty::CameraHeight)] = 6;
    source.set_key(animation_timebase, {}, false, {}, {}, values);
    values[index] = 1;
    values[unsigned(AnimationProperty::CameraHeight)] = 10;
    source.set_key(2 * animation_timebase, {}, false, {}, {}, values);
    const auto first = source.clip().keys.front().id;
    source.set_property_curve(first, AnimationProperty::CameraHeight, {true, 0, 0});
    const auto original = source.document;
    const auto clip = decode_transform_animation(source.cook(), source.asset());
    for (const auto time :
         {-1., 0., 500000., 999999.999, 1000000., 1999999.999, 2000000., 3000000.}) {
        const auto sampled = clip.sample_properties(time);
        near(sampled[index], time < 1000000 ? 0 : time < 2000000 ? 3 : 1);
    }
    near(clip.sample_properties(500000)[unsigned(AnimationProperty::CameraHeight)], 4);
    for (const auto value : {-1., .5, 268435456., std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()})
        rejects([&] { validate_animation_property(frame, value); });
    rejects([&] { source.set_property_curve(first, frame, {}); });
    rejects([&] {
        source.set_property_tangent_preset(std::array{first}, frame,
                                           TransformAnimationTangentPreset::Flat);
    });
    auto fractional = values;
    fractional[index] = 1.5;
    rejects([&] { source.set_key(500000, {}, false, {}, {}, fractional); });
    require(source.document == original, "Rejected frame key/tangent changed source history");
    for (const auto* mode : {"linear", "cubic"}) {
        auto bad = source;
        bad.document["keys"][0]["curves"]["property_curves"]["forge.sprite_renderer.frame"] = {
            {"interpolation", mode}, {"incoming", 0}, {"outgoing", 0}};
        rejects([&] { bad.validate(); });
    }

    std::filesystem::create_directories(scratch / "Assets");
    auto lease = std::make_shared<ProjectLease>(scratch);
    auto document = TransformAnimationDocument::create(lease, "Assets/Frames.animation.json");
    const auto id = document->source().asset();
    source.document["asset_id"] = id;
    document->edit(document->revision(), "Sprite frames", [&](auto& j) { j = source.document; });
    document->save();
    AssetImportService service(lease, transform_animation_import_registry(),
                               {"windows", "d3d12", "desktop"});
    const auto receipt = publish(service, document->locator(), id);
    require(receipt.published && receipt.publication, "Sprite frame clip publication failed");
    const auto catalog = std::make_shared<const AssetCatalog>(receipt.publication->catalog);
    EngineContext authoring;
    Scene scene(authoring.world());
    const auto actor =
        authoring_command(
            scene, "entity.create",
            {{"recipe", "render.sprite"},
             {"component_values", {{"columns", 2}, {"rows", 2}, {"pivot_x", 0}, {"tint_a", .5}}}})
            .at("selected")
            .get<std::string>();
    scene.entity(actor).set<Camera>({}).set<TransformAnimator>({{id}});
    const auto saved = scene.document();
    auto preview = saved;
    require(apply_transform_animation_preview(preview, {scene.asset_id(), EntityId::parse(actor)},
                                              source.clip(), 1500000),
            "Sprite preview target missing");
    const auto& projected = preview["entities"][0]["components"]["forge.sprite_renderer"];
    require(projected["frame"].is_number_unsigned() && projected["frame"] == 3 &&
                projected["pivot_x"] == 0 && projected["tint_a"] == .5 && scene.document() == saved,
            "Sprite preview interpolated frame, changed unbound values or wrote authored state");
    auto invalid = saved;
    invalid["entities"][0]["components"]["forge.sprite_renderer"]["columns"] = 1;
    const auto invalid_before = invalid;
    rejects([&] {
        apply_transform_animation_preview(invalid, {scene.asset_id(), EntityId::parse(actor)},
                                          source.clip(), 0);
    });
    require(invalid == invalid_before, "Invalid future frame caused partial preview substitution");

    Fixture live(scratch);
    live.animation->catalog(catalog);
    live.load(saved);
    live.ready();
    live.simulation.reset_presentation();
    live.simulation.tick(.75f);
    require(live.scene.entity(actor).get<SpriteRenderer>().frame == 0,
            "Fixed sprite frame switched before its key");
    live.simulation.tick(.5f);
    require(live.scene.entity(actor).get<SpriteRenderer>().frame == 3,
            "Fixed sprite frame missed its key");
    const auto native = live.scene.entity(actor).get<SpriteRenderer>();
    for (const auto alpha : {0., .25, .5, 1.}) {
        const auto projected = live.simulation.presentation(alpha);
        const auto& value =
            projected["entities"][0]["components"]["forge.sprite_renderer"]["frame"];
        require(value.is_number_unsigned() && value == (alpha < .5 ? 0 : 3),
                "Presentation rounded a frame instead of sampling its exact key boundary");
    }
    require(live.scene.entity(actor).get<SpriteRenderer>() == native && scene.document() == saved,
            "Frame presentation wrote live or authored components");
    const auto checkpoint = live.animation->checkpoint();
    Fixture recovered(scratch);
    recovered.animation->catalog(catalog);
    recovered.load(saved);
    recovered.ready();
    recovered.animation->restore(checkpoint);
    require(recovered.scene.entity(actor).get<SpriteRenderer>().frame == 3 &&
                recovered.simulation.presentation(
                    0)["entities"][0]["components"]["forge.sprite_renderer"]["frame"] == 3,
            "Sprite frame recovery did not collapse presentation to restored time");
    Fixture invalid_target(scratch);
    invalid_target.animation->catalog(catalog);
    invalid_target.load(invalid_before);
    rejects([&] { invalid_target.ready(); });
    require(invalid_target.scene.entity(actor).get<SpriteRenderer>().frame == 0 &&
                invalid_target.scene.entity(actor).get<Camera>().orthographic_height == 10,
            "Invalid future frame partially wrote another animated property");
}
