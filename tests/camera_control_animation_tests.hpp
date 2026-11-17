// Uses the component animation owners and fixtures in animation_component_tests.cpp.
void camera_control_playback(const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    EngineContext engine;
    Scene scene(engine.world());
    const auto create = [&](const char* name, const char* recipe) {
        return authoring_command(scene, "entity.create", {{"name", name}, {"recipe", recipe}})
            .at("selected")
            .get<std::string>();
    };
    const auto target = create("Camera target", "empty");
    const auto actor = create("Animated camera controls", "render.camera");
    const EntityRef reference{scene.asset_id(), EntityId::parse(target)};
    scene.entity(target).set<LocalTranslation>({0, 1.5, 0});
    scene.entity(actor)
        .set<LocalTranslation>({0, 3, -8})
        .set<CameraAim>({true, reference, 0, .5, 0, 0})
        .set<CameraFollow>({true, reference, -3, 2, -6, 0, 0});
    const auto schema = scene.schema();
    const auto declaration = [&](const char* name) -> const Json& {
        const auto& types = schema.at("components");
        const auto at = std::find_if(types.begin(), types.end(),
                                     [&](const auto& type) { return type.at("id") == name; });
        check(at != types.end(), "Camera control declaration missing");
        return *at;
    };
    const auto binding = [&](const char* component, const char* field) {
        const auto& type = declaration(component);
        check(type.at("admission").at("module") == "forge.core",
              "Camera component admission has the wrong owner");
        const auto fields = animation_component_fields(type);
        const auto at = std::find_if(fields.begin(), fields.end(),
                                     [&](const auto& item) { return item.field == field; });
        check(at != fields.end(), "Camera control field is unavailable");
        check(std::none_of(fields.begin(), fields.end(),
                           [](const auto& item) { return item.field == "target"; }),
              "Camera Target reference became an animation leaf");
        return *at;
    };
    const auto follow = binding("forge.camera_follow", "weight");
    const auto aim = binding("forge.camera_aim", "weight");
    const auto offset = binding("forge.camera_follow", "offset_x");
    const auto enabled = binding("forge.camera_aim", "enabled");
    const auto space = binding("forge.camera_follow", "offset_space");
    check(enabled.value_type == AnimationComponentValueType::Boolean &&
              space.value_type == AnimationComponentValueType::UnsignedInteger,
          "Camera discrete tracks lost their exact native domains");
    auto lease = std::make_shared<ProjectLease>(root);
    auto document = TransformAnimationDocument::create(lease, "Camera.animation.json");
    auto source = document->source();
    source.add_component_field(follow, 0.);
    source.add_component_field(aim, 0.);
    source.add_component_field(offset, -3.);
    source.add_component_field(enabled, true);
    source.add_component_field(space, 0u);
    source.document["channels"] = Json::array();
    const auto last = source.set_key(2 * animation_timebase, {});
    source.set_component_value(last, follow, 1.);
    source.set_component_value(last, aim, 1.);
    source.set_component_value(last, offset, 3.);
    source.set_component_value(last, enabled, false);
    source.set_component_value(last, space, 1u);
    source.document["keys"][0]["component_values"][0]["future"] = 17;
    document->edit(document->revision(), "Animate camera controls",
                   [&](auto& value) { value = source.document; });
    document->undo();
    check(document->source().clip().component_bindings.empty(), "Camera track Undo failed");
    document->redo();
    check(document->source().document == source.document,
          "Camera track Redo lost values, stamps or opaque metadata");
    document->save();
    AssetImportService importer(lease, transform_animation_import_registry(),
                                {"linux", "none", "cpu"});
    importer.submit(
        importer.prepare(document->locator(), {}, source.asset()),
        [](auto& candidate, const auto& plan, const auto&) {
            prepare_transform_animation_publication(candidate, plan);
        },
        [](const auto&, const auto&) {});
    check(importer.wait_idle(10s), "Camera control clip publication stalled");
    const auto results = importer.poll();
    check(results.size() == 1 && results.front().published,
          "Camera control clip publication failed");
    const auto catalog = std::make_shared<const AssetCatalog>(results[0].publication->catalog);
    scene.entity(actor).set<TransformAnimator>({{source.asset()}});
    auto saved = scene.snapshot();
    for (auto& row : saved.at("entities"))
        if (row.at("id") == actor) {
            row["components"]["forge.camera_aim"]["future"] = "retained";
            row["components"]["forge.camera_follow"]["future"] = 23;
        }
    scene.restore_snapshot(saved);
    const auto unchanged = scene.document();
    const EntityRef camera{scene.asset_id(), EntityId::parse(actor)};
    auto preview = scene.effective_document();
    check(apply_transform_animation_preview(preview, camera, source.clip(), animation_timebase,
                                            &schema),
          "Camera control copied preview unavailable");
    const auto row = std::find_if(preview.at("entities").begin(), preview.at("entities").end(),
                                  [&](const auto& value) { return value.at("id") == actor; });
    const auto& components = row->at("components");
    near(components.at("forge.camera_aim").at("weight").get<double>(), .5);
    near(components.at("forge.camera_follow").at("weight").get<double>(), .5);
    near(components.at("forge.camera_follow").at("offset_x").get<double>(), 0);
    check(components.at("forge.camera_aim").at("enabled") == true &&
              components.at("forge.camera_follow").at("offset_space") == 0 &&
              components.at("forge.camera_aim").at("future") == "retained" &&
              components.at("forge.camera_follow").at("future") == 23 &&
              components.at("forge.camera_follow").at("target") == Json(reference),
          "Camera sampling lost held, reference or opaque state");
    auto invalid = source;
    invalid.set_component_curve(source.clip().keys.front().id, aim, {true, 0, 8});
    auto rejected = scene.effective_document();
    const auto before = rejected;
    rejects(
        [&] { apply_transform_animation_preview(rejected, camera, invalid.clip(), 0, &schema); });
    check(rejected == before, "Cubic weight overshoot partially changed preview");
    invalid = source;
    invalid.set_component_value(last, space, 2u);
    rejects([&] {
        prepare_animation_components(engine.world(), scene.entity(actor), invalid.clip(), 0, true);
    });
    invalid = source;
    invalid.document["component_bindings"][0]["admission"]["digest"] = "wrong";
    rejects([&] {
        prepare_animation_components(engine.world(), scene.entity(actor), invalid.clip(), 0, true);
    });
    invalid = source;
    invalid.document["component_bindings"][0]["field"] = "target";
    for (auto& key : invalid.document["keys"])
        key["component_values"][0]["field"] = "target";
    rejects([&] {
        prepare_animation_components(engine.world(), scene.entity(actor), invalid.clip(), 0, true);
    });
    const auto candidates = prepare_animation_components(engine.world(), scene.entity(actor),
                                                         source.clip(), animation_timebase, true);
    check(candidates.size() == 2 && scene.document() == unchanged,
          "Detached camera candidates wrote native or authored values");
    Runtime live(root, saved, catalog);
    live.ready();
    live.simulation.tick(.5f);
    near(live.scene.entity(actor).get<CameraFollow>().weight, .25);
    near(live.scene.entity(actor).get<CameraFollow>().offset_x, -1.5);
    near(live.scene.entity(actor).get<CameraAim>().weight, .25);
    const auto checkpoint = live.animation->checkpoint();
    Runtime recovered(root, live.scene.snapshot(), catalog);
    recovered.ready();
    recovered.animation->restore(checkpoint);
    recovered.simulation.tick(.5f);
    near(recovered.scene.entity(actor).get<CameraFollow>().weight, .5);
    near(recovered.scene.entity(actor).get<CameraAim>().weight, .5);
    check(recovered.scene.entity(actor).get<CameraFollow>().target == reference &&
              recovered.scene.entity(actor).get<LocalTranslation>() == LocalTranslation{0, 3, -8} &&
              scene.document() == unchanged,
          "Camera playback changed Target, native pose or authored Scene");
    std::array<Json, 5> values;
    source.clip().sample_component_values(2 * animation_timebase, values);
    check(values[3] == false && values[4] == 1u,
          "Camera discrete tracks did not switch exactly at the endpoint");
}
