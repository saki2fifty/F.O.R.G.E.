// Included by the existing native UI Gallery fixture.
void clip_ui_preview(const std::filesystem::path& root, const std::vector<std::byte>& font) {
    EngineContext engine;
    Scene scene(engine.world());
    scene.restore_snapshot(read(root / scene_path));
    const auto authored = scene.snapshot();
    const auto revision = scene.revision();
    EntityRef target;
    AssetId clip_id;
    for (const auto& row : authored.at("entities"))
        if (row.at("components").contains("forge.ui_values")) {
            target = scene.reference(row.at("id").get<std::string>());
            clip_id = row.at("components").at("forge.transform_animator").at("clip").get<AssetId>();
        }
    check(bool(target.entity) && bool(clip_id), "Single-clip UI fixture needs its original target");
    const auto catalog = AssetCatalog::open_project(root);
    const auto clip_record = catalog.resolve(clip_id, TransformAnimationAsset::type);
    check(clip_record.state == AssetState::Available, "UI clip source is unavailable");
    const auto clip = TransformAnimationSource{read(root / clip_record.record->source)}.clip();
    const auto schema = scene.schema();
    AnimationUiPreview preview;
    UiTestRenderer renderer;
    {
        UiPresenter presenter(renderer, root, font);
        std::string instance;
        std::uint64_t generation = 0;
        float pulse = -1;
        for (const double seconds : {0., .5, 1., 1.5, 2., .25}) {
            auto sampled = scene.document();
            check(apply_transform_animation_preview(sampled, target, clip,
                                                    seconds * animation_timebase, &schema),
                  "Single-clip UI target was not sampled");
            const auto snapshot = preview.snapshot(root, scene, sampled, target, clip_id, 1);
            check(snapshot.at("documents").size() == 1 && snapshot.at("errors").empty(),
                  "Copied single-clip UI did not project its document");
            const auto& document = snapshot.at("documents").at(0);
            const auto triangle = seconds <= 1 ? seconds : 2 - seconds;
            near(document.at("model").at("amount"), .2 + .8 * triangle);
            near(document.at("model").at("width"), 80 + 280 * triangle);
            check(document.at("model").at("tick") == 0 && document.at("model").at("paused") == true,
                  "Single-clip UI preview advanced gameplay");
            if (instance.empty()) {
                instance = document.at("instance");
                generation = snapshot.at("generation");
                presenter.reset(snapshot.at("session"), generation);
            }
            check(document.at("instance") == instance && snapshot.at("generation") == generation,
                  "Seeking replaced the retained clip UI model");
            check(presenter.accept(snapshot), presenter.diagnostic().c_str());
            presenter.update(0, 640, 480);
            presenter.render();
            auto* native = Rml::GetContext(0)->GetDocument(0);
            auto* fill = native->GetElementById("fill");
            near(fill->GetProperty<float>("width"), 80 + 280 * triangle);
            near(fill->GetProperty<float>("opacity"), .2 + .8 * triangle);
            check(fill->IsClassSet("bright") == (.2 + .8 * triangle >= .6),
                  "Single-clip UI class did not follow its sampled value");
            const auto opacity = native->GetElementById("pulse")->GetProperty<float>("opacity");
            if (pulse < 0)
                pulse = opacity;
            check(opacity == pulse, "Single-clip preview advanced native CSS time");
            check(scene.snapshot() == authored && scene.revision() == revision,
                  "Single-clip UI projection modified the source Scene");
        }
        auto sampled = scene.document();
        for (auto& row : sampled.at("entities"))
            if (row.at("id") == target.entity.str())
                row["components"]["forge.ui_document"]["visible"] = false;
        const auto hidden = preview.snapshot(root, scene, sampled, target, clip_id, 1);
        check(hidden.at("documents").at(0).at("visible") == false &&
                  hidden.at("documents").at(0).at("instance") == instance,
              "Clip UI projection lost sampled document fields or its stable handle");
        const auto restored = preview.snapshot(root, scene, scene.document(), target, clip_id, 1);
        near(restored.at("documents").at(0).at("model").at("width"), 80);
        check(restored.at("documents").at(0).at("visible") == true &&
                  restored.at("documents").at(0).at("instance") == instance,
              "Unanimated source values did not replace the earlier sample");
        const auto reopened = preview.snapshot(root, scene, scene.document(), target, clip_id, 2);
        check(reopened.at("generation").get<std::uint64_t>() > generation,
              "Reopening a document reused an older clip UI generation");
        bool refused = false;
        try {
            (void)preview.snapshot(root, scene, scene.document(), target, {}, 2);
        } catch (const std::exception&) {
            refused = true;
        }
        check(refused && scene.snapshot() == authored && scene.revision() == revision,
              "Invalid clip UI scope was accepted or modified source state");
    }
    preview.reset();
    check(renderer.geometry.empty() && renderer.textures.empty(),
          "Single-clip UI resources survived presenter retirement");
}
