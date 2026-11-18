#pragma once
// Exercise the Timeline owner, not a second test-only sampler or UI model.
void clip_ui_timeline(const std::filesystem::path& gallery, const std::filesystem::path& scratch) {
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root.parent_path());
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    project.open_scene("Scenes/151-sequence-ui.scene.json");
    const auto authored = scene.snapshot();
    const auto revision = scene.revision();
    const auto path = root / "Assets/Sequences/UiValues.animation.json";
    const auto bytes = asset_detail::read_bytes(path, 1024 * 1024);
    std::string target;
    for (const auto& row : authored.at("entities"))
        if (row.at("components").contains("forge.ui_values"))
            target = row.at("id");
    check(!target.empty(), "Timeline UI fixture lost its authored target");
    AnimationTimelineEditor editor;
    editor.open(project, "Assets/Sequences/UiValues.animation.json");
    const auto document = editor.document()->source().document;
    const auto document_revision = editor.document()->revision();
    editor.seek_preview(scene, {}, .5);
    check(!editor.preview_active(), "Unbound scrubbing activated an arbitrary preview target");
    check(editor.preview_ui_snapshot(project, scene, scene.document()).is_null(),
          "Inactive Timeline exposed a UI model");
    std::uint64_t generation = 0;
    for (const double seconds : {0., .5, 1., 1.5, 2., .25}) {
        editor.seek_preview(scene, target, seconds);
        check(editor.preview_ui_snapshot(project, scene, scene.document()).is_null(),
              "A seek exposed the preceding incomplete sample");
        const auto sampled = editor.preview(scene.document());
        const auto snapshot = editor.preview_ui_snapshot(project, scene, sampled);
        check(snapshot.is_object() && snapshot.at("errors").empty() &&
                  snapshot.at("documents").size() == 1,
              "Timeline did not project its complete sampled proposal");
        const auto triangle = seconds <= 1 ? seconds : 2 - seconds;
        const auto& model = snapshot.at("documents").at(0).at("model");
        expect_close(model.at("amount"), .2 + .8 * triangle);
        expect_close(model.at("width"), 80 + 280 * triangle);
        if (!generation)
            generation = snapshot.at("generation");
        check(snapshot.at("generation") == generation && !editor.frame_export_ready() &&
                  editor.frame_export_ui_snapshot().is_null(),
              "Clip preview replaced its scope or advertised sequence frame export");
        check(scene.snapshot() == authored && scene.revision() == revision && !project.dirty() &&
                  editor.document()->source().document == document &&
                  editor.document()->revision() == document_revision,
              "Timeline seek changed Scene/source values or their history");
    }
    for (const double invalid : {-1., 2.01, std::numeric_limits<double>::infinity()}) {
        bool refused = false;
        try {
            editor.seek_preview(scene, target, invalid);
        } catch (const std::exception&) {
            refused = true;
        }
        check(refused && editor.preview_active(), "Invalid time disturbed a valid clip preview");
    }
    editor.play_preview(true);
    std::string message;
    editor.poll(project, scene, true, .1, message);
    check(!editor.preview_active() &&
              editor.preview_ui_snapshot(project, scene, scene.document()).is_null(),
          "Play handoff retained stopped clip UI authority");
    editor.seek_preview(scene, target, .5);
    const auto sampled = editor.preview(scene.document());
    const auto restarted = editor.preview_ui_snapshot(project, scene, sampled);
    check(restarted.at("generation").get<std::uint64_t>() > generation,
          "Restarted clip preview reused its retired presentation scope");
    editor.stop_preview();
    check(editor.preview(scene.document()) == scene.document() &&
              editor.preview_ui_snapshot(project, scene, sampled).is_null() &&
              scene.snapshot() == authored && scene.revision() == revision &&
              asset_detail::read_bytes(path, 1024 * 1024) == bytes,
          "Stopping Timeline retained UI authority or modified saved authoring data");
    std::cout << "Single-clip Timeline UI: complete sample, seek, guards, Play handoff and "
                 "Scene/source/history isolation PASS\n";
}
