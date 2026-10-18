#pragma once
// Included inside the sequence UI test's namespace, after its independent oracles.
template <class F> void rejected(F&& operation, const char* reason) {
    bool failed = false;
    try {
        operation();
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, reason);
}
void frame_plans() {
    ui::SequenceFramePlan plan{.25, 24, 25};
    plan.validate(2);
    check(plan.sample(0) == .25 && plan.sample(6) == .5 && plan.sample(24) == 1.25,
          "Frame samples changed their explicit sequence time");
    check(plan.filename(0) == "frame-000000.png" && plan.filename(24) == "frame-000024.png",
          "Numbered frame filenames lost their exact index");
    plan = {0, 30, 4096};
    plan.validate(137);
    check(plan.sample(4095) == 136.5, "Frame times accumulated rounding drift");
    rejected([&] { plan.sample(4096); }, "Out-of-range frame accepted");
    for (int count : {0, -1, std::numeric_limits<int>::min(), 4097}) {
        auto invalid = plan;
        invalid.frame_count = count;
        rejected([&] { invalid.validate(200); }, "Invalid count accepted");
    }
    for (double fps : {0., -1., 241., std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN()}) {
        auto invalid = plan;
        invalid.frames_per_second = fps;
        rejected([&] { invalid.validate(200); }, "Invalid FPS accepted");
    }
    for (double start :
         {-1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        auto invalid = plan;
        invalid.start_seconds = start;
        rejected([&] { invalid.validate(200); }, "Invalid start accepted");
    }
    for (double duration : {0., -1., std::numeric_limits<double>::infinity(),
                            std::numeric_limits<double>::quiet_NaN()})
        rejected([&] { plan.validate(duration); }, "Invalid duration accepted");
    plan = {0, 1, 7};
    rejected([&] { plan.validate(6); }, "Exact loop endpoint silently wrapped to zero");
    plan = {0, std::numeric_limits<double>::denorm_min(), 2};
    rejected([&] { plan.validate(6); }, "Overflowing frame time accepted");
}
void frame_source(const std::filesystem::path& gallery, const std::filesystem::path& scratch) {
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root.parent_path());
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    project.open_scene("Scenes/70-sequence-audio-cues.scene.json");
    EntityRef controller;
    std::string moving;
    EntityId front, side;
    const auto initial = scene.snapshot();
    for (const auto& row : initial.at("entities")) {
        const auto id = row.at("id").get<EntityId>();
        const auto& values = row.at("components");
        if (values.contains("forge.animation_sequence"))
            controller = {scene.asset_id(), id};
        if (values.contains("forge.transform_animator") && !values.contains("forge.camera") &&
            !values.contains("forge.light"))
            moving = id.str();
        if (values.contains("forge.camera_shot")) {
            const auto& shot = values.at("forge.camera_shot");
            (shot.at("start_seconds") == 0 ? front : side) =
                shot.at("camera").get<EntityRef>().entity;
        }
    }
    check(bool(controller.entity) && !moving.empty() && bool(front) && bool(side),
          "Frame source Gallery consumer is incomplete");
    authoring_command(scene, "property.set",
                      {{"entity", controller.entity.str()},
                       {"component", "forge.animation_sequence"},
                       {"field", "playback_speed"},
                       {"value", 4}});
    AnimationSequenceEditor source;
    auto prepare = [&] {
        source.open(project, scene, controller);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (!source.frame_export_ready() && std::chrono::steady_clock::now() < deadline) {
            source.poll(false, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(source.frame_export_ready(), "Frame source clips did not become ready");
    };
    prepare();
    const auto original = scene.document();
    const auto revision = scene.revision();
    const auto info = source.frame_export_info();
    check(info.controller == controller && info.duration == 6 && info.revision == revision,
          "Frame source identity differs");
    const auto token = source.begin_frame_export();
    rejected([&] { source.begin_frame_export(); }, "Overlapping preview exports admitted");
    check(!source.can_undo() && !source.can_redo(), "Capture allowed sequence history edits");
    for (double time : {0., 1.5, 3.}) {
        source.sample_frame_export(token, time);
        const auto preview = source.preview(scene.effective_document());
        const double u = std::clamp((time - .5) / 2, 0., 1.);
        bool found = false;
        for (const auto& row : preview.at("entities"))
            if (row.at("id") == moving) {
                found = true;
                near(row.at("components").at("forge.local_translation").at("x"),
                     -2 + 4 * u * u * (3 - 2 * u));
            }
        check(found, "Frame preview lost its moving actor");
        const auto rendered = extract_render_scene(preview);
        check(rendered.cameras.size() == 1 &&
                  rendered.cameras.front().entity == (time < 3 ? front : side),
              "Frame sample lost an exact sequence camera cut");
        source.poll(false, 10);
        check(source.preview(scene.effective_document()) == preview,
              "Elapsed GUI time advanced a held frame sample");
    }
    rejected([&] { source.sample_frame_export(token, 6); }, "Exact loop endpoint accepted");
    source.end_frame_export(token);
    check(!source.preview_active() && scene.document() == original && scene.revision() == revision,
          "Frame source changed authored values/history or failed to restore stopped preview");
    const auto retired = source.begin_frame_export();
    source.close();
    prepare();
    const auto current = source.begin_frame_export();
    source.end_frame_export(retired);
    check(current != retired && source.frame_export_current(current),
          "Retired export restored into a reopened preview");
    source.end_frame_export(current);
    const auto changed_token = source.begin_frame_export();
    auto changed_catalog = std::make_shared<AssetCatalog>(*info.catalog);
    auto changed = changed_catalog->records().begin()->second;
    changed.metadata["frame_export_test"] = true;
    changed_catalog->replace(std::move(changed));
    source.asset_catalog_changed(changed_catalog);
    check(!source.frame_export_current(changed_token) && !source.preview_active(),
          "Published asset changes retained an old capture context");
    source.end_frame_export(changed_token);
    prepare();
    const auto stale = source.begin_frame_export();
    authoring_command(scene, "entity.create", {{"name", "Intentional source edit"}});
    const auto edited = scene.document();
    check(!source.frame_export_current(stale), "Source edit retained capture admission");
    source.end_frame_export(stale);
    check(scene.document() == edited && !source.preview_active(),
          "Stale capture restoration overwrote an authored edit");
}
