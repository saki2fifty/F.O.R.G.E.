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
        const auto preview = source.preview(scene.document());
        const double u = std::clamp((time - .5) / 2, 0., 1.);
        bool found = false;
        for (const auto& row : preview.at("entities"))
            if (row.at("id") == moving) {
                found = true;
                expect_close(row.at("components").at("forge.local_translation").at("x"),
                             -2 + 4 * u * u * (3 - 2 * u));
            }
        check(found, "Frame preview lost its moving actor");
        const auto rendered = extract_render_scene(scene.preview_document(preview));
        check(rendered.cameras.size() == 1 &&
                  rendered.cameras.front().entity == (time < 3 ? front : side),
              "Frame sample lost an exact sequence camera cut");
        source.poll(false, 10);
        check(source.preview(scene.document()) == preview,
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

// The stopped Timeline and its frame-export lease project the shared view after
// Scene has resolved the copied animated hierarchy, exactly as the editor does.
void camera_aim_frame_source(const std::filesystem::path& gallery,
                             const std::filesystem::path& scratch, bool following = false,
                             bool controls = false) {
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    project.open_scene(controls    ? "Scenes/221-camera-control-animation.scene.json"
                       : following ? "Scenes/220-camera-follow.scene.json"
                                   : "Scenes/219-camera-aim.scene.json");
    const auto authored = scene.document();
    const auto revision = scene.revision();
    EntityRef sequence;
    std::string camera;
    for (const auto& row : authored.at("entities")) {
        const auto id = row.at("id").get<std::string>();
        if (scene.entity(id).has<AnimationSequence>())
            sequence = {scene.asset_id(), EntityId::parse(id)};
        if (scene.entity(id).has<CameraAim>())
            camera = id;
    }
    check(bool(sequence.entity) && !camera.empty(), "Stopped Camera Aim actors are missing");
    AnimationSequenceEditor timeline;
    timeline.open(project, scene, sequence);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!timeline.frame_export_ready()) {
        timeline.poll(false, 0);
        check(std::chrono::steady_clock::now() < deadline, "Camera Aim Timeline did not prepare");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto lease = timeline.begin_frame_export();
    for (double time : {0., .25, .5, .75, 1., 1.5, 1.875}) {
        timeline.sample_frame_export(lease, time);
        auto source = scene.preview_document(timeline.preview(scene.document()));
        timeline.project_camera_shots(source);
        const auto render = extract_render_scene(source);
        const auto views = prepare_game_cameras(render, 960, 540);
        check(render.diagnostics.empty() && views.cameras.size() == 1 &&
                  views.cameras[0].entity == EntityId::parse(camera),
              "Stopped Camera Aim frame lost its view");
        const auto u = time / 2;
        if (controls) {
            const auto& view = views.cameras[0].view;
            const double x = (-3 + 6 * u) * u, y = 3 + .5 * u, z = -8 + 2 * u;
            expect_close(view.position[0], x);
            expect_close(view.position[1], y);
            expect_close(view.position[2], z);
            const double dx = -x, dy = 2 - y, dz = -z;
            const auto radial = std::hypot(dx, dy);
            const auto angle = std::atan2(radial, dz) * u;
            expect_close(view.forward[0], dx / radial * std::sin(angle));
            expect_close(view.forward[1], dy / radial * std::sin(angle));
            expect_close(view.forward[2], std::cos(angle));
            continue;
        }
        const auto x = -2 + 4 * u * u * (3 - 2 * u);
        const auto distance = following ? std::sqrt(38.25) : std::sqrt(x * x + 65);
        const auto& view = views.cameras[0].view;
        expect_close(view.position[0], following ? x : 0);
        expect_close(view.position[1], following ? 3.5 : 3);
        expect_close(view.position[2], following ? -6 : -8);
        expect_close(view.forward[0], following ? 0 : x / distance);
        expect_close(view.forward[1], (following ? -1.5 : -1) / distance);
        expect_close(view.forward[2], (following ? 6 : 8) / distance);
    }
    timeline.end_frame_export(lease);
    check(scene.document() == authored && scene.revision() == revision,
          "Stopped Camera Aim frame sampling changed Scene source/history");
}
void camera_transition_frame_source(const std::filesystem::path& gallery,
                                    const std::filesystem::path& scratch, std::uint32_t curve) {
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    project.open_scene("Scenes/68-sequence-camera-shots.scene.json");
    std::vector<std::pair<std::string, CameraShot>> shots;
    const auto initial_source = scene.document();
    for (const auto& item : initial_source.at("entities")) {
        const auto id = item.at("id").get<std::string>();
        if (scene.entity(id).has<CameraShot>())
            shots.emplace_back(id, scene.entity(id).get<CameraShot>());
    }
    std::sort(shots.begin(), shots.end(), [](const auto& a, const auto& b) {
        return a.second.start_seconds < b.second.start_seconds;
    });
    check(shots.size() == 3, "Transition source needs the original three-shot fixture");
    const auto outgoing = shots[0].second.camera.entity.str();
    const auto incoming = shots[1].second.camera.entity.str();
    auto profile = scene.entity(outgoing).get<Camera>();
    profile.enabled = false;
    profile.vertical_fov = 1.2;
    scene.entity(incoming).set(profile);
    MeshRenderer attached_mesh;
    attached_mesh.mesh = engine_primitive(0);
    scene.entity(incoming).set(attached_mesh);
    Light attached_light;
    attached_light.kind = unsigned(LightKind::Point);
    scene.entity(incoming).set(attached_light);
    auto shot = shots[1].second;
    shot.blend_in_seconds = 1;
    shot.blend_curve = curve;
    scene.entity(shots[1].first).set(shot);
    const auto authored = scene.document();
    const auto revision = scene.revision();
    const auto resolved = scene.preview_document(authored);
    const auto row = [](const Json& source, const std::string& id) -> const Json& {
        const auto& rows = source.at("entities");
        return *std::find_if(rows.begin(), rows.end(),
                             [&](const auto& value) { return value.at("id") == id; });
    };
    AnimationSequenceEditor timeline;
    timeline.open(project, scene, shot.sequence);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!timeline.frame_export_ready()) {
        timeline.poll(false, 0);
        check(std::chrono::steady_clock::now() < deadline, "Transition preview did not prepare");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto lease = timeline.begin_frame_export();
    for (double time : {2., 2.25, 2.5, 2.75, 3.}) {
        timeline.sample_frame_export(lease, time);
        auto source = scene.preview_document(timeline.preview(scene.document()));
        timeline.project_camera_shots(source);
        const auto render = extract_render_scene(source);
        check(render.cameras.size() == 1 && render.cameras[0].entity == EntityId::parse(incoming),
              "Stopped transition preview changed its incoming identity");
        const double linear = time - 2;
        const std::array<double, 4> weights{linear * linear * (3 - 2 * linear), linear,
                                            linear * linear, linear * (2 - linear)};
        const double weight = weights.at(curve);
        for (unsigned i : {3u, 7u, 11u})
            expect_close(render.cameras[0].world.m[i],
                         std::lerp(row(resolved, outgoing).at("world_affine").at(i).get<double>(),
                                   row(resolved, incoming).at("world_affine").at(i).get<double>(),
                                   weight));
        check(row(source, incoming).at("world_affine") ==
                  row(resolved, incoming).at("world_affine"),
              "Stopped camera blend changed the entity's resolved node pose");
        const auto mesh =
            std::find_if(render.meshes.begin(), render.meshes.end(), [&](const auto& value) {
                return value.entity == EntityId::parse(incoming);
            });
        const auto light =
            std::find_if(render.lights.begin(), render.lights.end(), [&](const auto& value) {
                return value.entity == EntityId::parse(incoming);
            });
        check(mesh != render.meshes.end() && light != render.lights.end(),
              "Stopped camera blend omitted an attached producer");
        check(mesh->world.m ==
                  row(resolved, incoming).at("world_affine").get<std::array<double, 12>>(),
              "Stopped camera blend moved an attached mesh");
        for (unsigned i = 0; i < 3; ++i)
            expect_close(light->light.position[i], mesh->world.m[4 * i + 3]);
        expect_close(render.cameras[0].camera.vertical_fov,
                     std::lerp(scene.entity(outgoing).get<Camera>().vertical_fov, 1.2, weight));
        check(row(source, incoming).at("components").at("forge.local_translation") ==
                  row(authored, incoming).at("components").at("forge.local_translation"),
              "Stopped camera blend wrote its local transform");
    }
    timeline.end_frame_export(lease);
    check(scene.document() == authored && scene.revision() == revision,
          "Transition frame sampling changed Scene source/history");
}
