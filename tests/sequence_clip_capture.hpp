#pragma once
void capture_sequence_pair(const std::filesystem::path& gallery,
                           const std::filesystem::path& scratch,
                           const std::filesystem::path& output, bool nested = false,
                           bool camera_transition = false, bool property_pair = false) {
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root.parent_path());
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    if (!SDL_Init(SDL_INIT_VIDEO))
        throw std::runtime_error(SDL_GetError());
    {
        const int height = camera_transition ? 1400 : 1100;
        test::ImGuiSoftwareCapture capture(720, height);
        ui::style(1);
        EngineContext engine;
        Scene scene(engine.world());
        SceneDocument project(scene);
        project.open_project(root);
        project.open_scene(property_pair ? "Scenes/183-sequence-property-crossfades.scene.json"
                           : camera_transition ? "Scenes/153-camera-transitions.scene.json"
                           : nested            ? "Scenes/146-nested-sequences.scene.json"
                                               : "Scenes/145-sequence-clip-blending.scene.json");
        EntityRef controller;
        std::string actor;
        const auto source = scene.snapshot();
        for (const auto& row : source.at("entities")) {
            if (row.at("components").contains("forge.animation_sequence")) {
                if (row.at("components")
                        .at("forge.animation_sequence")
                        .value("sequence", Json{})
                        .is_null())
                    controller = {scene.asset_id(), row.at("id").get<EntityId>()};
                else if (nested)
                    actor = row.at("id").get<std::string>();
            }
            if (camera_transition && row.at("components").contains("forge.camera_shot") &&
                row.at("components").at("forge.camera_shot").at("start_seconds") == 2)
                actor = row.at("id").get<std::string>();
            if (!nested && !camera_transition &&
                row.at("components").contains("forge.transform_animator") &&
                (!property_pair || row.at("components").contains("forge.light")))
                actor = row.at("id").get<std::string>();
        }
        check(bool(controller.entity) && !actor.empty(), "Missing pair capture track");
        ui::EditorUiContext owner;
        ui::ContextScope scope(owner);
        owner.selection.select_entity(actor);
        AnimationTimelineEditor editor;
        editor.open_sequence(project, scene, controller);
        test::observe_ui = true;
        std::string message;
        auto frame = [&] {
            test::ui_targets.clear();
            editor.poll(project, scene, false, 0, message);
            capture.new_frame();
            ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({720, float(height)}, ImGuiCond_Always);
            editor.draw(project, scene, false);
            capture.render();
        };
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (!editor.frame_export_ready()) {
            frame();
            check(std::chrono::steady_clock::now() < deadline, "Pair capture admission timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        test::ImGuiInputDriver input(frame);
        if (property_pair) {
            const auto authored = scene.document();
            const auto revision = scene.revision();
            const auto token = editor.begin_frame_export();
            for (double seconds : {0., 1.25, 1.5, 2.}) {
                editor.sample_frame_export(token, seconds);
                const auto preview = editor.preview(authored);
                bool light = false, sprite = false;
                const double weight = std::clamp(seconds - 1, 0., 1.);
                for (const auto& row : preview.at("entities")) {
                    const auto& components = row.at("components");
                    if (row.at("id") == actor) {
                        const auto& value = components.at("forge.light");
                        near(value.at("intensity"), 2 + 2 * weight);
                        near(value.at("color_r"), .1 + .9 * weight);
                        light = true;
                    }
                    if (components.contains("forge.sprite_renderer")) {
                        near(components.at("forge.sprite_renderer").at("frame"),
                             seconds < 1.5 ? 0 : 3);
                        sprite = true;
                    }
                }
                check(light && sprite && scene.document() == authored &&
                          scene.revision() == revision,
                      "Property pair preview changed authored Scene/history or lost values");
            }
            editor.end_frame_export(token);
            frame();
        }
        input.click(((camera_transition ? "sequence:shot:"
                      : nested          ? "sequence:nested:"
                                        : "sequence:track:") +
                     actor)
                        .c_str());
        for (unsigned i = 0; i < 4; ++i)
            frame();
        const auto controls =
            camera_transition
                ? std::vector<const char*>{"sequence:shot_start", "sequence:shot_end",
                                           "sequence:shot_blend", "sequence:apply_shot"}
            : nested ? std::vector<const char*>{"sequence:nested_start", "sequence:apply_nested",
                                                "sequence:open_nested", "sequence:remove_nested"}
                     : std::vector<const char*>{"sequence:start", "sequence:next_start",
                                                "sequence:clip", "sequence:next_clip"};
        for (const char* key : controls) {
            const auto& box = test::ui_targets.at(key);
            check(box.enabled && box.minimum.x >= box.clip_minimum.x &&
                      box.maximum.x <= box.clip_maximum.x && box.minimum.y >= box.clip_minimum.y &&
                      box.maximum.y <= box.clip_maximum.y,
                  "Pair capture clipped a required control");
        }
        std::filesystem::create_directories(output.parent_path());
        capture.save(output);
        test::observe_ui = false;
    }
    SDL_Quit();
}
