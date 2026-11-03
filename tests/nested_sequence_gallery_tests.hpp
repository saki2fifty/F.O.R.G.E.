#pragma once
constexpr auto nested_gallery_scene = "Scenes/146-nested-sequences.scene.json";
void author_nested_sequence(const std::filesystem::path& root) {
    check(!std::filesystem::exists(root / nested_gallery_scene),
          "Nested sequence Gallery already exists");
    EngineContext author;
    Scene scene(author.world());
    scene.reset(duplicate_scene_asset(read(root / "Scenes/70-sequence-audio-cues.scene.json"),
                                      AssetId::generate(), scene.schema()));
    const auto source = scene.snapshot();
    std::string child;
    for (const auto& row : source.at("entities"))
        if (row.at("components").contains("forge.animation_sequence"))
            child = row.at("id").get<std::string>();
    check(!child.empty(), "Nested Gallery source has no child sequence");
    const auto parent = authoring_command(scene, "entity.create",
                                          {{"recipe", "animation.sequence"},
                                           {"name", "Parent - eight-second loop"},
                                           {"component_values", {{"duration", 8}, {"loop", true}}}})
                            .at("selected")
                            .get<std::string>();
    auto config = scene.entity(child).get<AnimationSequence>();
    config.sequence = {scene.asset_id(), EntityId::parse(parent)};
    config.sequence_start_seconds = 1;
    config.play_on_start = false;
    config.playback_speed = 2; // Stored standalone intent; parent timing governs this placement.
    scene.entity(child).set(config);
    authoring_command(scene, "entity.rename",
                      {{"entity", child}, {"name", "Child - six seconds placed at parent 1s"}});
    scene.save(root / nested_gallery_scene);
    const auto lease = std::make_shared<ProjectLease>(root);
    auto catalog = AssetCatalog::open_project(root);
    const auto record = catalog.add_scene(nested_gallery_scene);
    catalog.save(AssetCatalog::project_index(root));
    const std::array roots{record.id};
    prepare_runtime_content_catalog(*lease, roots, {}, scene.schema());
}
void exercise_nested_sequence(const std::filesystem::path& root, const Json& source) {
    EntityRef parent, child;
    const auto scene_id = source.at("asset_id").get<AssetId>();
    EntityId moving, fallback;
    std::map<double, EntityId> cameras;
    for (const auto& row : source.at("entities")) {
        const auto& c = row.at("components");
        const auto id = row.at("id").get<EntityId>();
        if (c.contains("forge.animation_sequence")) {
            const auto binding = c.at("forge.animation_sequence").value("sequence", Json{});
            if (binding.is_null())
                parent = {scene_id, id};
            else
                child = {scene_id, id};
        }
        if (c.contains("forge.transform_animator"))
            moving = id;
        if (c.contains("forge.camera") && c.at("forge.camera").at("enabled").get<bool>())
            fallback = id;
        if (c.contains("forge.camera_shot")) {
            const auto& shot = c.at("forge.camera_shot");
            cameras.emplace(shot.at("start_seconds").get<double>(),
                            shot.at("camera").get<EntityRef>().entity);
        }
    }
    check(parent.entity && child.entity && moving && fallback && cameras.size() == 2,
          "Nested Gallery lost its participants");
    check(parent.scene == scene_id && child.scene == scene_id,
          "Nested Gallery collected an inconsistent Scene reference");
    World live(root, source);
    auto inspect = [&](World& run, double time, double alpha = 1) {
        const auto parent_status = run.animation->playback(parent);
        const auto child_status = run.animation->playback(child);
        if (!parent_status || !child_status)
            throw std::runtime_error(
                "Nested Gallery playback unavailable at " + std::to_string(time) +
                "; parent=" + Json(parent).dump() + "; child=" + Json(child).dump() +
                "; parent_status=" + std::to_string(bool(parent_status)) +
                "; entries=" + run.animation->checkpoint().at("sequence_entries").dump());
        near(parent_status->time, time);
        const auto child_time = child_status->time;
        const auto expected_child = std::clamp(time - 1, 0., 6.);
        if (std::abs(child_time - expected_child) > .00001)
            throw std::runtime_error("Nested child time differs at root " + std::to_string(time) +
                                     ": " + std::to_string(child_time) + " vs " +
                                     std::to_string(expected_child) +
                                     "; child=" + Json(child).dump() + "; " +
                                     run.animation->checkpoint().at("sequence_entries").dump());
        near(run.scene.entity(moving.str()).get<LocalTranslation>().x, -2 + 4 * ease(time, 1.5, 2));
        const auto render = extract_render_scene(run.simulation.presentation(alpha));
        const auto target = time < 1 || time > 7 ? fallback : cameras.at(time < 4 ? 0 : 3);
        check(render.diagnostics.empty() && render.cameras.size() == 1 &&
                  render.cameras.front().entity == target && render.meshes.size() == 5 &&
                  render.lights.size() == 1,
              "Nested Gallery lost timed cameras or actual geometry/light consumers");
    };
    inspect(live, 0);
    for (unsigned step = 1; step <= 34; ++step) {
        live.simulation.tick(.25f);
        inspect(live, std::fmod(step * .25, 8.));
    }
    for (const double time : {0., .999999, 1., 3.999999, 4., 6.999999, 7., 7.5}) {
        AnimationPlaybackAccess::control(*live.animation, parent,
                                         {{"operation", "seek"}, {"position_seconds", time}});
        inspect(live, time);
    }
    AnimationPlaybackAccess::control(*live.animation, parent,
                                     {{"operation", "seek"}, {"position_seconds", 3.25}});
    const auto checkpoint = live.animation->checkpoint();
    check(checkpoint.at("version") == 7 && checkpoint.at("sequence_entries").size() == 2,
          "Nested Gallery omitted exact recovery");
    World recovered(root, source);
    recovered.animation->restore(checkpoint);
    inspect(recovered, 3.25);
    check(recovered.animation->checkpoint() == checkpoint,
          "Nested Gallery recovery changed source-derived controller state");
}
