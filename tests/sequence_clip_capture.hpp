#pragma once
enum class SequenceCapture {
    Transforms,
    Nested,
    ChildLoops,
    ControllerPlacements,
    Multiway,
    CameraTransition,
    Properties,
    Bindings
};
void capture_sequence_pair(const std::filesystem::path& gallery,
                           const std::filesystem::path& scratch,
                           const std::filesystem::path& output,
                           SequenceCapture profile = SequenceCapture::Transforms) {
    const bool child_loops = profile == SequenceCapture::ChildLoops;
    const bool placements = profile == SequenceCapture::ControllerPlacements;
    const bool multiway = profile == SequenceCapture::Multiway;
    const bool nested = profile == SequenceCapture::Nested || child_loops || placements;
    const bool camera_transition = profile == SequenceCapture::CameraTransition;
    const bool property_pair = profile == SequenceCapture::Properties;
    const bool binding_pair = profile == SequenceCapture::Bindings;
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root.parent_path());
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    if (child_loops && !std::filesystem::exists(root / test::sequence_child_loop_scene_path))
        test::author_skeletal_sequence_gallery(root, test::SequenceGalleryProfile::ChildLoops);
    if (placements && !std::filesystem::exists(root / test::sequence_placement_scene_path))
        test::author_skeletal_sequence_gallery(root,
                                               test::SequenceGalleryProfile::ControllerPlacements);
    if (multiway && !std::filesystem::exists(root / test::sequence_multiway_scene_path))
        test::author_skeletal_sequence_gallery(root, test::SequenceGalleryProfile::Multiway);
    if (!SDL_Init(SDL_INIT_VIDEO))
        throw std::runtime_error(SDL_GetError());
    {
        const int height = multiway            ? 1900
                           : placements        ? 1700
                           : child_loops       ? 1200
                           : camera_transition ? 1400
                           : nested            ? 1300
                                               : 1100;
        test::ImGuiSoftwareCapture capture(720, height);
        ui::style(1);
        EngineContext engine;
        Scene scene(engine.world());
        SceneDocument project(scene);
        project.open_project(root);
        project.open_scene(multiway        ? test::sequence_multiway_scene_path
                           : placements    ? test::sequence_placement_scene_path
                           : child_loops   ? test::sequence_child_loop_scene_path
                           : binding_pair  ? "Scenes/184-sequence-binding-crossfades.scene.json"
                           : property_pair ? "Scenes/183-sequence-property-crossfades.scene.json"
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
                else if (nested &&
                         (placements
                              ? row.value("name", std::string()).starts_with("Locomotion -")
                              : !child_loops ||
                                    row.value("name", std::string()).starts_with("Fast repeating")))
                    actor = row.at("id").get<std::string>();
            }
            if (multiway && row.at("components").contains("forge.animator") &&
                row.at("name").get<std::string>().starts_with("Center"))
                actor = row.at("id").get<std::string>();
            if (camera_transition && row.at("components").contains("forge.camera_shot") &&
                row.at("components").at("forge.camera_shot").at("start_seconds") == 2)
                actor = row.at("id").get<std::string>();
            if (!nested && !camera_transition &&
                row.at("components").contains("forge.transform_animator") &&
                (!property_pair || row.at("components").contains("forge.light")) &&
                (!binding_pair || row.at("components").contains("forge.mesh_renderer")))
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
        if (property_pair || binding_pair) {
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
                        if (binding_pair) {
                            const auto& values =
                                components.at("forge.mesh_renderer").at("material_parameters");
                            check(values.size() == 2, "Binding preview lost Material tracks");
                            for (const auto& value : values) {
                                const auto& lanes = value.at("parameter").at("value");
                                if (value.at("key") == "baseColorFactor") {
                                    near(lanes[0], .1 + .8 * weight);
                                    near(lanes[1], .2 + .1 * weight);
                                    near(lanes[2], .9 - .85 * weight);
                                    near(lanes[3], 1);
                                } else {
                                    check(value.at("key") == "roughnessFactor",
                                          "Unexpected Material track");
                                    near(lanes[0], .1 + .8 * weight);
                                }
                            }
                        } else {
                            const auto& value = components.at("forge.light");
                            near(value.at("intensity"), 2 + 2 * weight);
                            near(value.at("color_r"), .1 + .9 * weight);
                        }
                        light = true;
                    }
                    if (binding_pair && components.contains("forge.ui_values")) {
                        const auto& values = components.at("forge.ui_values").at("values");
                        check(values.size() == 2, "Binding preview resized UI Values");
                        for (const auto& value : values)
                            near(value.at("value"), value.at("name") == "amount"
                                                        ? .2 + .6 * weight
                                                        : 80 + 140 * weight);
                        sprite = true;
                    } else if (!binding_pair && components.contains("forge.sprite_renderer")) {
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
                     actor + (multiway ? ":skeletal" : ""))
                        .c_str());
        for (unsigned i = 0; i < 4; ++i)
            frame();
        const auto controls =
            camera_transition
                ? std::vector<const char*>{"sequence:shot_start", "sequence:shot_end",
                                           "sequence:shot_blend", "sequence:apply_shot"}
            : nested ? std::vector<const char*>{"sequence:nested_start", "sequence:nested_rate",
                                                "sequence:nested_loop",  "sequence:apply_nested",
                                                "sequence:open_nested",  "sequence:remove_nested"}
                     : std::vector<const char*>{"sequence:start", "sequence:next_start",
                                                "sequence:clip", "sequence:next_clip"};
        const auto require_visible = [&](const char* key) {
            const auto& box = test::ui_targets.at(key);
            check(box.enabled && box.minimum.x >= box.clip_minimum.x &&
                      box.maximum.x <= box.clip_maximum.x && box.minimum.y >= box.clip_minimum.y &&
                      box.maximum.y <= box.clip_maximum.y,
                  "Pair capture clipped a required control");
        };
        for (const char* key : controls)
            require_visible(key);
        if (multiway) {
            require_visible("sequence:multiway");
            require_visible("sequence:apply_start");
            require_visible("sequence:extra_start:0");
            require_visible("sequence:extra_start:5");
            check(scene.entity(actor).get<Animator>().sequence_multiway &&
                      editor.frame_export_ready(),
                  "Multiway capture lost its saved overlap mode or introduced pending edits");
        }
        if (placements)
            for (const char* key :
                 {"sequence:nested_extra_start:0", "sequence:nested_extra_rate:0",
                  "sequence:nested_extra_loop:0", "sequence:remove_nested_extra:0",
                  "sequence:nested_extra_start:1", "sequence:nested_extra_rate:1",
                  "sequence:nested_extra_loop:1", "sequence:remove_nested_extra:1",
                  "sequence:add_nested_extra"})
                require_visible(key);
        std::filesystem::create_directories(output.parent_path());
        capture.save(output);
        test::observe_ui = false;
    }
    SDL_Quit();
}
