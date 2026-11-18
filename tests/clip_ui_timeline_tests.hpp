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
        const auto frame_snapshot = editor.frame_export_ui_snapshot();
        check(snapshot.at("generation") == generation && editor.frame_export_ready() &&
                  frame_snapshot.at("documents") == snapshot.at("documents") &&
                  frame_snapshot.at("session") == snapshot.at("session"),
              "Clip preview replaced its scope or lost the same-owner frame observation");
        check(scene.snapshot() == authored && scene.revision() == revision && !project.dirty() &&
                  editor.document()->source().document == document &&
                  editor.document()->revision() == document_revision,
              "Timeline seek changed Scene/source values or their history");
    }
    const auto refused = [&](auto operation) {
        bool failed = false;
        try {
            operation();
        } catch (const std::exception&) {
            failed = true;
        }
        check(failed, "Clip frame lease accepted an invalid or competing operation");
    };
    const auto info = editor.frame_export_info();
    AnimationTimelineEditor unopened;
    refused([&] { unopened.bind(scene, target); });
    check(info.clip == editor.document()->source().asset() &&
              info.controller == scene.reference(target) && info.duration == 2 &&
              info.revision == revision && info.catalog,
          "Clip frame source lost its published clip/target/Scene identity");
    editor.play_preview(true);
    const auto token = editor.begin_frame_export();
    check(editor.frame_export_current(token) && !editor.frame_export_ready() &&
              !editor.can_undo() && !editor.can_redo(),
          "Clip frame export failed to retain exclusive Timeline authority");
    refused([&] { editor.begin_frame_export(); });
    refused([&] { editor.seek_preview(scene, target, .5); });
    refused([&] { editor.play_preview(true); });
    refused([&] { editor.bind(scene, target); });
    refused([&] { editor.sample_frame_export(token, 2); });
    refused([&] { editor.sample_frame_export(token + 1, .5); });
    for (const double seconds : {0., .5, 1., 1.5}) {
        editor.sample_frame_export(token, seconds);
        const auto snapshot = editor.frame_export_ui_snapshot();
        const auto triangle = seconds <= 1 ? seconds : 2 - seconds;
        expect_close(snapshot.at("documents").at(0).at("model").at("width"), 80 + 280 * triangle);
        std::string message;
        editor.poll(project, scene, false, 10, message);
        check(editor.frame_export_ui_snapshot().at("documents") == snapshot.at("documents") &&
                  editor.frame_export_document() == scene.document() &&
                  scene.snapshot() == authored && scene.revision() == revision,
              "GUI time advanced a held clip frame or mutated authored state");
    }
    editor.end_frame_export(token + 1);
    check(editor.frame_export_current(token), "Retired clip token released another capture");
    editor.end_frame_export(token);
    expect_close(editor.frame_export_ui_snapshot().at("documents").at(0).at("model").at("width"),
                 150);
    std::string restored_message;
    editor.poll(project, scene, false, .1, restored_message);
    const auto restored_sample = editor.preview(scene.document());
    expect_close(editor.preview_ui_snapshot(project, scene, restored_sample)
                     .at("documents")
                     .at(0)
                     .at("model")
                     .at("width"),
                 178);
    check(editor.document()->source().document == document &&
              editor.document()->revision() == document_revision && scene.snapshot() == authored &&
              asset_detail::read_bytes(path, 1024 * 1024) == bytes,
          "Clip export changed source bytes, values or history");
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
    editor.seek_preview(scene, target, .5);
    const auto stale = editor.begin_frame_export();
    authoring_command(scene, "entity.create", {{"name", "Intentional clip source edit"}});
    const auto edited = scene.document();
    check(!editor.frame_export_current(stale), "Changed Scene retained a clip capture token");
    refused([&] { editor.sample_frame_export(stale, .5); });
    editor.end_frame_export(stale);
    check(!editor.preview_active() && scene.document() == edited,
          "Stale clip capture restored obsolete source state");
    editor.seek_preview(scene, target, .5);
    const auto stopped = editor.begin_frame_export();
    editor.stop_preview();
    editor.end_frame_export(stopped);
    check(!editor.preview_active() && !editor.frame_export_current(stopped),
          "Stop restored obsolete clip capture authority");
    editor.seek_preview(scene, target, .5);
    const auto catalog_token = editor.begin_frame_export();
    auto replaced_catalog = std::make_shared<AssetCatalog>(*info.catalog);
    auto replaced = replaced_catalog->records().begin()->second;
    replaced.metadata["clip_capture_test"] = true;
    replaced_catalog->replace(std::move(replaced));
    editor.asset_catalog_changed(replaced_catalog);
    editor.end_frame_export(catalog_token);
    check(!editor.frame_export_current(catalog_token) && !editor.preview_active(),
          "Catalog publication retained an old clip capture or restored its cursor");
    editor.asset_catalog_changed(info.catalog);
    editor.seek_preview(scene, target, .5);
    const auto scene_token = editor.begin_frame_export();
    project.open_scene("Scenes/151-sequence-ui.scene.json");
    check(!editor.frame_export_current(scene_token),
          "Reopening the same Scene identity retained a previous clip capture generation");
    editor.end_frame_export(scene_token);
    check(!editor.preview_active(), "Scene generation change restored an obsolete clip cursor");
    editor.seek_preview(scene, target, .5);
    check(editor.frame_export_ready(), "Current Scene generation could not rebind its clip target");
    std::cout << "Single-clip Timeline UI: complete sample, seek, guards, Play handoff and "
                 "Scene/source/history isolation PASS\n";
}
