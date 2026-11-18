#pragma once
// Included in the existing native stopped-animation presentation fixture.
inline std::array<Pixels, 3> clip_frames(DiligentPresentation& presentation,
                                         Diligent::IDeviceContext* graphics,
                                         const std::filesystem::path& root,
                                         const std::filesystem::path& output,
                                         const std::filesystem::path& font) {
    std::filesystem::create_directories(output);
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    project.open_scene("Scenes/151-sequence-ui.scene.json");
    const auto saved = scene.snapshot();
    const auto revision = scene.revision();
    std::string target;
    for (const auto& row : saved.at("entities"))
        if (row.at("components").contains("forge.ui_values"))
            target = row.at("id");
    AnimationTimelineEditor timeline;
    timeline.open(project, "Assets/Sequences/UiValues.animation.json");
    const auto resources = std::make_shared<MeshResourceHost>(presentation, graphics, root);
    resources->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
    FrameRenderer renderer(presentation);
    renderer.resources(resources);
    PlaySession play;
    RuntimeUiHost ui(nullptr, presentation.device(), font);
    ui.resources(resources);
    constexpr unsigned width = 800, height = 600;
    std::array<Pixels, 3> pixels;
    float pulse_opacity = -1;
    for (unsigned i = 0; i < pixels.size(); ++i) {
        timeline.seek_preview(scene, target, .5 * i);
        Diligent::ITextureView* image = nullptr;
        unsigned stable = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (stable < 3) {
            presentation.begin_frame();
            ui.sync(play, root, true, ui.sequence_preview_active());
            // Match Game composition: one complete raw proposal supplies both consumers.
            const auto sampled = timeline.preview(scene.document());
            const auto snapshot = timeline.preview_ui_snapshot(project, scene, sampled);
            check(snapshot.is_object() && snapshot.at("errors").empty(),
                  "Clip Game UI projection failed");
            check(ui.prepare_sequence_frame(play, root, snapshot, width, height),
                  ui.diagnostic().c_str());
            RenderTextureOverlay overlay = [&](auto* context, AssetId id, auto* view, unsigned w,
                                               unsigned h) {
                ui.draw_output(play, context, id, view, w, h);
            };
            image = renderer.game(graphics, extract_render_scene(scene.preview_document(sampled)),
                                  width, height, 0, nullptr, &overlay);
            check(image && ui.draw(play, graphics, image), "Clip Game composition failed");
            stable = renderer.pending() ? 0 : stable + 1;
            resources->submit();
            presentation.end_frame(graphics);
            graphics->Flush();
            graphics->FinishFrame();
            presentation.device()->ReleaseStaleResources();
            check(std::chrono::steady_clock::now() < deadline, "Clip Game resources stalled");
        }
        auto* document = Rml::GetContext(0)->GetDocument(0);
        auto* fill = document->GetElementById("fill");
        const float amount = .2f + .4f * i, expected_width = 80.f + 140.f * i;
        check(std::abs(fill->GetBox().GetSize().x - expected_width) < .05f &&
                  std::abs(fill->GetProperty<float>("opacity") - amount) < .001f &&
                  fill->IsClassSet("bright") == (i != 0),
              "Clip Game native UI differs from the selected clip time");
        const auto pulse = document->GetElementById("pulse")->GetProperty<float>("opacity");
        if (pulse_opacity < 0)
            pulse_opacity = pulse;
        check(pulse == pulse_opacity, "Clip Game preview advanced CSS time");
        test::ViewImageReference reference;
        reference.queue(presentation.device(), graphics, image);
        pixels[i] = reference.read(graphics);
        save_view_png(output / ("clip-" + std::to_string(i) + ".png"), width, height, pixels[i]);
        const auto origin = fill->GetAbsoluteOffset();
        const auto foreground = i == 0 ? std::array{72, 182, 213} : std::array{233, 183, 91};
        const auto background = std::array{16, 29, 41};
        for (unsigned channel = 0; channel < 3; ++channel) {
            const auto inside = (std::size_t(int(origin.y) + 16) * width + int(origin.x) + 20) * 4;
            const auto outside = (std::size_t(int(origin.y) + 16) * width + int(origin.x) +
                                  int(expected_width) + 8) *
                                 4;
            check(std::abs(int(pixels[i].at(inside + channel)) -
                           (foreground[channel] * amount + background[channel] * (1 - amount))) <=
                          3 &&
                      std::abs(int(pixels[i].at(outside + channel)) - background[channel]) <= 2,
                  "Actual clip Game pixels disagree with sampled width/opacity/class");
        }
        const auto snapshot =
            timeline.preview_ui_snapshot(project, scene, timeline.preview(scene.document()));
        const auto instance = snapshot.at("documents").at(0).at("instance").get<std::string>();
        check(ui.buttons(instance).is_null() && !ui.command(instance, "Pause", {}),
              "Clip Game preview exposed gameplay authority");
    }
    timeline.stop_preview();
    ui.sync(play, root, true);
    check(!ui.sequence_preview_active() && !ui.presenter_alive() && !ui.asset_snapshot() &&
              scene.snapshot() == saved && scene.revision() == revision && !project.dirty(),
          "Stopping clip Game preview retained authority or changed authoring data");
    return pixels;
}
