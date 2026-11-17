#pragma once
// The existing copied sequence world projects its sampled values through UiRuntime.
// Native RML presentation is checked separately; this is the data/lifecycle boundary.
void sequence_ui_frame_source(const std::filesystem::path& gallery,
                              const std::filesystem::path& scratch,
                              const std::filesystem::path& font = {}) {
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root.parent_path());
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    project.open_scene("Scenes/151-sequence-ui.scene.json");
    EntityRef controller;
    const auto definitions = scene.document();
    for (const auto& row : definitions.at("entities")) {
        const auto actor = scene.entity(row.at("id").get<std::string>());
        if (actor.has<AnimationSequence>() && !actor.get<AnimationSequence>().sequence.entity)
            controller = scene.reference(row.at("id").get<std::string>());
    }
    check(bool(controller.entity), "UI Gallery root sequence is missing");
    AnimationSequenceEditor source;
    check(source.preview_ui_snapshot().is_null(), "Closed sequence exposed a UI snapshot");
    source.open(project, scene, controller);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!source.frame_export_ready() && std::chrono::steady_clock::now() < deadline) {
        source.poll(false, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(source.frame_export_ready(), "UI sequence clips did not become ready");
    const auto authored = scene.document();
    const auto revision = scene.revision();
    const auto token = source.begin_frame_export();
    UiTestRenderer renderer;
    std::unique_ptr<UiPresenter> presenter;
    if (!font.empty()) {
        presenter = std::make_unique<UiPresenter>(renderer, root,
                                                  asset_detail::read_bytes(font, 4 * 1024 * 1024));
        const auto initial = source.preview_ui_snapshot();
        presenter->reset(initial.at("session").get<std::string>(),
                         initial.at("generation").get<std::uint64_t>());
    }
    float pulse = -1;
    std::string instance;
    for (double seconds : {0., .5, 1., 1.25, 1.5, 2., 2.5, 3., 3.5, 0.}) {
        source.sample_frame_export(token, seconds);
        const auto snapshot = source.preview_ui_snapshot();
        check(!snapshot.is_null() && snapshot.at("errors").empty() &&
                  snapshot.at("documents").size() == 1,
              "Sampled sequence UI lost its admitted document");
        const auto& document = snapshot.at("documents").at(0);
        const auto& model = document.at("model");
        const double local = std::clamp(seconds - 1, 0., 2.);
        const double triangle = local <= 1 ? local : 2 - local;
        expect_close(model.at("amount").get<double>(), .2 + .8 * triangle);
        expect_close(model.at("width").get<double>(), 80 + 280 * triangle);
        check(model.at("tick").get<std::uint64_t>() == 0 && model.at("paused").get<bool>(),
              "Stopped UI projection advanced a gameplay clock");
        if (presenter) {
            check(presenter->accept(snapshot), "Native UI did not admit the sampled model");
            presenter->update(0, 800, 600);
            auto* native = Rml::GetContext(0)->GetDocument(0);
            auto* fill = native->GetElementById("fill");
            expect_close(fill->GetBox().GetSize().x, 80 + 280 * triangle);
            expect_close(fill->GetProperty<float>("opacity"), .2 + .8 * triangle);
            check(fill->IsClassSet("bright") == (.2 + .8 * triangle >= .6),
                  "Native UI class does not follow the sampled value");
            const auto now = native->GetElementById("pulse")->GetProperty<float>("opacity");
            if (pulse < 0)
                pulse = now;
            check(now == pulse, "Native CSS progressed while the stopped sequence sought values");
            check(presenter->command(document.at("instance").get<std::string>(), "Pause"),
                  "Native command queue fixture did not enqueue");
            presenter->discard_commands();
            check(!presenter->pending_command(), "Preview command discard retained gameplay work");
            check(presenter->diagnostic().empty(), presenter->diagnostic().c_str());
        }
        const auto current = document.at("instance").get<std::string>();
        if (instance.empty())
            instance = current;
        check(instance == current, "Seeking replaced the retained UI model instance");
        check(scene.document() == authored && scene.revision() == revision && !project.dirty(),
              "UI projection changed the authored Scene, history or dirty state");
    }
    source.end_frame_export(token);
    check(!source.preview_active() && source.preview_ui_snapshot().is_null() &&
              scene.document() == authored,
          "Ending frame export retained stopped UI presentation authority");
    std::cout << "Stopped sequence UI snapshot: nested values, repeat seek, clock and authoring "
                 "isolation PASS\n";
}
