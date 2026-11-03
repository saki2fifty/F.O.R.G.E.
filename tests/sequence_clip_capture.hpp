#pragma once
void capture_sequence_pair(const std::filesystem::path& gallery,
                           const std::filesystem::path& scratch,
                           const std::filesystem::path& output) {
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root.parent_path());
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    if (!SDL_Init(SDL_INIT_VIDEO))
        throw std::runtime_error(SDL_GetError());
    {
        test::ImGuiSoftwareCapture capture(720, 1100);
        ui::style(1);
        EngineContext engine;
        Scene scene(engine.world());
        SceneDocument project(scene);
        project.open_project(root);
        project.open_scene("Scenes/145-sequence-clip-blending.scene.json");
        EntityRef controller;
        std::string actor;
        const auto source = scene.snapshot();
        for (const auto& row : source.at("entities")) {
            if (row.at("components").contains("forge.animation_sequence"))
                controller = {scene.asset_id(), row.at("id").get<EntityId>()};
            if (row.at("components").contains("forge.transform_animator"))
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
            ImGui::SetNextWindowSize({720, 1100}, ImGuiCond_Always);
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
        input.click(("sequence:track:" + actor).c_str());
        for (unsigned i = 0; i < 4; ++i)
            frame();
        for (const char* key :
             {"sequence:start", "sequence:next_start", "sequence:clip", "sequence:next_clip"}) {
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
