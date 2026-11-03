#pragma once
constexpr auto pair_gallery_scene = "Scenes/145-sequence-clip-blending.scene.json";
void author_sequence_pair(const std::filesystem::path& root) {
    check(!std::filesystem::exists(root / pair_gallery_scene),
          "Sequence pair Gallery already exists");
    const auto lease = std::make_shared<ProjectLease>(root);
    AssetImportService importer(lease, transform_animation_import_registry(),
                                {"windows", "d3d12", "desktop"});
    auto publish = [&](const char* path, bool reverse) {
        auto document = TransformAnimationDocument::create(lease, path);
        document->edit(document->revision(), "Original sequence motion", [&](Json& json) {
            TransformAnimationSource source{json};
            source.set_key(0, {{reverse ? 3. : -3., 1, 0},
                               rotation_about_axis({0, 1, 0}, reverse ? 90 : 0),
                               {1, 1, 1}});
            source.set_key(2 * animation_timebase,
                           {{reverse ? -1. : 1., 1, 0},
                            rotation_about_axis({0, 1, 0}, reverse ? 180 : 90),
                            {1, 1, 1}});
            json = source.document;
        });
        document->save();
        const auto id = document->source().asset();
        importer.submit(
            importer.prepare(path, {}, id),
            [](auto& candidate, const auto& plan, const auto&) {
                prepare_transform_animation_publication(candidate, plan);
            },
            [](const auto&, const auto&) {});
        check(importer.wait_idle(std::chrono::seconds(20)), "Sequence pair cook timed out");
        const auto receipts = importer.poll();
        check(receipts.size() == 1 && receipts[0].published, "Sequence pair clip did not publish");
        return id;
    };
    const auto first = publish("Assets/Sequences/BlendArrival.animation.json", false);
    const auto next = publish("Assets/Sequences/BlendReturn.animation.json", true);
    EngineContext engine;
    Scene scene(engine.world());
    auto source = duplicate_scene_asset(read(root / "Scenes/102-temporal-antialiasing.scene.json"),
                                        AssetId::generate(), scene.schema());
    std::erase_if(source["entities"].get_ref<Json::array_t&>(), [](const Json& row) {
        const auto& c = row.at("components");
        return !c.contains("forge.camera") && !c.contains("forge.light");
    });
    source.erase("rendering");
    scene.reset(source);
    const auto controller = authoring_command(scene, "entity.create",
                                              {{"recipe", "animation.sequence"},
                                               {"name", "Two clips - four second loop"}})
                                .at("selected")
                                .get<std::string>();
    AnimationSequence sequence;
    sequence.duration = 4;
    sequence.loop = true;
    scene.entity(controller).set(sequence);
    const auto actor = authoring_command(scene, "entity.create",
                                         {{"recipe", "primitive.0"},
                                          {"name", "Blended cube - select to inspect both clips"}})
                           .at("selected")
                           .get<std::string>();
    TransformAnimator player{{first}};
    player.sequence = {scene.asset_id(), EntityId::parse(controller)};
    player.sequence_next_clip = {next};
    player.sequence_next_start_seconds = 1;
    scene.entity(actor).set(player);
    const auto floor =
        authoring_command(scene, "entity.create", {{"recipe", "primitive.3"}, {"name", "Ground"}})
            .at("selected")
            .get<std::string>();
    scene.entity(floor).set<LocalScale>({10, 1, 5});
    scene.save(root / pair_gallery_scene);
    auto catalog = AssetCatalog::open_project(root);
    const auto record = catalog.add_scene(pair_gallery_scene);
    catalog.save(AssetCatalog::project_index(root));
    const std::array roots{record.id};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
}
void exercise_sequence_pair(const std::filesystem::path& root, const Json& source) {
    EntityRef controller;
    std::string actor;
    for (const auto& row : source.at("entities")) {
        if (row.at("components").contains("forge.animation_sequence"))
            controller = {source.at("asset_id").get<AssetId>(), row.at("id").get<EntityId>()};
        if (row.at("components").contains("forge.transform_animator"))
            actor = row.at("id").get<std::string>();
    }
    check(bool(controller.entity) && !actor.empty(), "Pair Gallery lost its track/controller");
    World live(root, source);
    for (const auto [time, expected] : std::array<std::array<double, 2>, 6>{
             {{0, -3}, {.5, -2}, {1, -1}, {1.5, 1}, {2, 1}, {3.5, -1}}}) {
        AnimationPlaybackAccess::control(*live.animation, controller,
                                         {{"operation", "seek"}, {"position_seconds", time}});
        near(live.scene.entity(actor).get<LocalTranslation>().x, expected);
        check(extract_render_scene(live.simulation.presentation(1)).meshes.size() == 2,
              "Sequence pair did not reach the actual render Scene");
    }
    AnimationPlaybackAccess::control(*live.animation, controller,
                                     {{"operation", "seek"}, {"position_seconds", 1.5}});
    const auto checkpoint = live.animation->checkpoint();
    check(checkpoint.at("version") == 6, "Pair Gallery did not use exact recovery");
    World recovered(root, source);
    recovered.animation->restore(checkpoint);
    near(recovered.scene.entity(actor).get<LocalTranslation>().x, 1);
}
void exercise_pair_with_cues(const std::filesystem::path& root, Json source) {
    EntityRef controller;
    std::string actor;
    for (auto& row : source["entities"]) {
        auto& components = row["components"];
        if (components.contains("forge.animation_sequence"))
            controller = {source.at("asset_id").get<AssetId>(), row.at("id").get<EntityId>()};
        if (components.contains("forge.transform_animator")) {
            actor = row.at("id").get<std::string>();
            auto& track = components["forge.transform_animator"];
            track["sequence_next_clip"] = track.at("clip");
            track["sequence_next_start_seconds"] = 1.5;
        }
    }
    World live(root, source);
    AnimationPlaybackAccess::control(*live.animation, controller,
                                     {{"operation", "seek"}, {"position_seconds", 1.75}});
    near(live.scene.entity(actor).get<LocalTranslation>().x, .09375);
    const auto checkpoint = live.animation->checkpoint();
    check(checkpoint.at("version") == 6 && checkpoint.at("camera_shots").size() == 2 &&
              checkpoint.at("audio_cues").size() == 3 &&
              checkpoint.at("sequence_entries")[0].contains("entry_pending"),
          "Paired recovery lost the camera/audio/entry profile");
    World recovered(root, source);
    recovered.animation->restore(checkpoint);
    near(recovered.scene.entity(actor).get<LocalTranslation>().x, .09375);
    check(recovered.animation->checkpoint() == checkpoint,
          "Paired camera/audio recovery changed exact sequence state");
    live.simulation.tick(.25);
    recovered.simulation.tick(.25);
    near(live.scene.entity(actor).get<LocalTranslation>().x, 0);
    near(recovered.scene.entity(actor).get<LocalTranslation>().x, 0);
}
