#pragma once
constexpr auto property_gallery_scene = "Scenes/183-sequence-property-crossfades.scene.json";
void author_property_sequence(const std::filesystem::path& root) {
    check(!std::filesystem::exists(root / property_gallery_scene),
          "Property sequence Gallery already exists");
    const auto lease = std::make_shared<ProjectLease>(root);
    AssetImportService importer(lease, transform_animation_import_registry(),
                                {"windows", "d3d12", "desktop"});
    const auto publish = [&](const char* path, bool frames, bool incoming) {
        auto document = TransformAnimationDocument::create(lease, path);
        document->edit_source(
            document->revision(), "Property crossfade clip", [&](TransformAnimationSource& source) {
                source.retime_duration(2 * animation_timebase);
                if (frames)
                    source.add_property(AnimationProperty::SpriteFrame, incoming ? 3 : 0);
                else {
                    source.add_property(AnimationProperty::LightIntensity, incoming ? 4 : 2);
                    source.add_property(AnimationProperty::LightRed, incoming ? 1 : .1);
                    source.add_property(AnimationProperty::LightGreen, incoming ? .35 : .2);
                    source.add_property(AnimationProperty::LightBlue, incoming ? .05 : 1);
                }
                source.document["channels"] = Json::array();
            });
        document->save();
        const auto asset = document->source().asset();
        importer.submit(
            importer.prepare(path, {}, asset),
            [](auto& candidate, const auto& plan, const auto&) {
                prepare_transform_animation_publication(candidate, plan);
            },
            [](const auto&, const auto&) {});
        check(importer.wait_idle(std::chrono::seconds(20)),
              "Property sequence clip cook timed out");
        const auto receipts = importer.poll();
        check(receipts.size() == 1 && receipts.front().published,
              "Property sequence clip did not publish");
        return asset;
    };
    const auto light_first =
        publish("Assets/Sequences/PropertyLightBlue.animation.json", false, false);
    const auto light_next =
        publish("Assets/Sequences/PropertyLightWarm.animation.json", false, true);
    const auto frame_first =
        publish("Assets/Sequences/PropertyFrameFirst.animation.json", true, false);
    const auto frame_next =
        publish("Assets/Sequences/PropertyFrameNext.animation.json", true, true);
    EngineContext engine;
    Scene scene(engine.world());
    auto source = duplicate_scene_asset(read(root / "Scenes/102-temporal-antialiasing.scene.json"),
                                        AssetId::generate(), scene.schema());
    std::erase_if(source["entities"].get_ref<Json::array_t&>(), [](const Json& row) {
        const auto& components = row.at("components");
        return !components.contains("forge.camera") && !components.contains("forge.light");
    });
    source.erase("rendering");
    scene.reset(source);
    std::string light;
    unsigned cameras = 0, lights = 0;
    for (const auto& item : source.at("entities")) {
        const auto id = item.at("id").get<std::string>();
        if (item.at("components").contains("forge.camera")) {
            scene.entity(id).set(Camera{});
            ++cameras;
        }
        if (item.at("components").contains("forge.light")) {
            scene.entity(id).set(Light{});
            light = id;
            ++lights;
        }
    }
    check(cameras == 1 && lights == 1, "Property Gallery base must have one camera and light");
    scene.rename_entity(light, "Light - intensity and color crossfade");
    const auto controller =
        authoring_command(scene, "entity.create",
                          {{"recipe", "animation.sequence"}, {"name", "Four second property loop"}})
            .at("selected")
            .get<std::string>();
    AnimationSequence sequence;
    sequence.duration = 4;
    sequence.loop = true;
    scene.entity(controller).set(sequence);
    const EntityRef reference{scene.asset_id(), EntityId::parse(controller)};
    const auto bind = [&](const std::string& id, AssetId first, AssetId next) {
        TransformAnimator track{{first}};
        track.sequence = reference;
        track.sequence_next_clip = {next};
        track.sequence_next_start_seconds = 1;
        scene.entity(id).set(track);
    };
    bind(light, light_first, light_next);
    const auto cube = authoring_command(scene, "entity.create",
                                        {{"recipe", "primitive.0"},
                                         {"name", "Cube - animated lighting"},
                                         {"position", {{"x", -1.5}, {"y", 1}, {"z", 0}}}})
                          .at("selected")
                          .get<std::string>();
    scene.entity(cube).set<LocalScale>({1.5, 1.5, 1.5});
    const auto sprite = authoring_command(scene, "entity.create",
                                          {{"recipe", "render.sprite"},
                                           {"name", "Sprite - frame switches at midpoint"},
                                           {"position", {{"x", 1.5}, {"y", 1}, {"z", 0}}}})
                            .at("selected")
                            .get<std::string>();
    const auto catalog = AssetCatalog::open_project(root);
    SpriteRenderer image;
    for (const auto& [id, record] : catalog.records())
        if (record.source == "Assets/Sprites/SignalSheet.material.json")
            image.material = {id};
    check(bool(image.material.id), "Property Gallery Sprite material is missing");
    image.columns = 4;
    image.width = image.height = 2;
    scene.entity(sprite).set(image);
    bind(sprite, frame_first, frame_next);
    const auto floor =
        authoring_command(scene, "entity.create", {{"recipe", "primitive.3"}, {"name", "Ground"}})
            .at("selected")
            .get<std::string>();
    scene.entity(floor).set<LocalScale>({10, 1, 5});
    scene.save(root / property_gallery_scene);
    auto updated = AssetCatalog::open_project(root);
    const auto record = updated.add_scene(property_gallery_scene);
    updated.save(AssetCatalog::project_index(root));
    const std::array roots{record.id};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
}
void exercise_property_sequence(const std::filesystem::path& root, const Json& source) {
    EntityRef controller;
    std::string light, sprite;
    for (const auto& item : source.at("entities")) {
        const auto& components = item.at("components");
        if (components.contains("forge.animation_sequence"))
            controller = {source.at("asset_id").get<AssetId>(), item.at("id").get<EntityId>()};
        if (components.contains("forge.transform_animator") && components.contains("forge.light"))
            light = item.at("id");
        if (components.contains("forge.transform_animator") &&
            components.contains("forge.sprite_renderer"))
            sprite = item.at("id");
    }
    check(controller.entity && !light.empty() && !sprite.empty(),
          "Property Gallery tracks missing");
    World live(root, source);
    for (const auto& sample : std::array<std::array<double, 2>, 7>{
             {{0, 0}, {.5, 0}, {1, 0}, {1.5, .5}, {2, 1}, {2.75, 1}, {3.5, 1}}}) {
        AnimationPlaybackAccess::control(*live.animation, controller,
                                         {{"operation", "seek"}, {"position_seconds", sample[0]}});
        const auto& value = live.scene.entity(light).get<Light>();
        near(value.intensity, 2 + 2 * sample[1]);
        near(value.color_r, .1 + .9 * sample[1]);
        near(value.color_g, .2 + .15 * sample[1]);
        near(value.color_b, 1 - .95 * sample[1]);
        check(live.scene.entity(sprite).get<SpriteRenderer>().frame == (sample[1] < .5 ? 0u : 3u),
              "Gallery frame did not switch at overlap midpoint");
        const auto render = extract_render_scene(live.simulation.presentation(1));
        check(render.meshes.size() == 2 && render.sprites.size() == 1,
              "Property Gallery did not reach the shared render Scene");
    }
}
void property_sequence_export(const std::filesystem::path& gallery,
                              const std::filesystem::path& scratch) {
    const auto work = scratch / AssetId::generate().str();
    const auto root = work / "source";
    std::filesystem::create_directories(work);
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    const auto source = read(root / property_gallery_scene);
    const auto catalog = AssetCatalog::open_project(root);
    exercise_property_sequence(root, source);
    const std::array roots{source.at("asset_id").get<AssetId>()};
    {
        ProjectLease lease(root);
        prepare_runtime_content_catalog(lease, roots, {});
    }
    package_runtime_content(root, work / "export", roots, {"windows", "d3d12"});
    std::filesystem::rename(work / "export", work / "relocated");
    const auto shipped = open_runtime_content(work / "relocated", {"windows", "d3d12"});
    check(shipped.records().size() == 7, "Property Gallery export closure differs");
    for (const auto& [id, record] : shipped.records()) {
        (void)id;
        if (record.type == TransformAnimationAsset::type)
            check(!std::filesystem::exists(work / "relocated" / catalog.records().at(id).source),
                  "Editable property clip source leaked into export");
    }
    std::filesystem::rename(root, work / "source-offline");
    exercise_property_sequence(work / "relocated",
                               read(work / "relocated" / shipped.records().at(roots[0]).source));
    std::cout << "Property Gallery native values, render composition and relocated source-free "
                 "content passed\n";
}
