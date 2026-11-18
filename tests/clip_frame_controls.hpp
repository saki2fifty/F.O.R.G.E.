#pragma once
#include <tuple>
// Actual Timeline controls, with its existing software UI capture/input owners.
void clip_frame_controls(const std::filesystem::path& gallery,
                         const std::filesystem::path& scratch) {
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root.parent_path());
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    if (!SDL_Init(SDL_INIT_VIDEO))
        throw std::runtime_error(SDL_GetError());
    for (const auto& [name, width, scale] : {std::tuple{"wide-100", 800, 1.f},
                                             {"narrow-100", 400, 1.f},
                                             {"wide-150", 1000, 1.5f},
                                             {"wide-200", 1200, 2.f}}) {
        test::ImGuiSoftwareCapture capture(width, 1000);
        ui::style(scale);
        EngineContext engine;
        Scene scene(engine.world());
        SceneDocument project(scene);
        project.open_project(root);
        project.open_scene("Scenes/151-sequence-ui.scene.json");
        const auto saved = scene.snapshot();
        std::string target;
        for (const auto& row : saved.at("entities"))
            if (row.at("components").contains("forge.ui_values"))
                target = row.at("id");
        ui::EditorUiContext context;
        ui::ContextScope scope(context);
        context.selection.select_entity(target);
        AnimationTimelineEditor editor;
        editor.open(project, "Assets/Sequences/UiValues.animation.json");
        editor.seek_preview(scene, target, .5);
        unsigned requests = 0;
        bool available = true, locked = false;
        editor.on_frame_export([&] { ++requests; }, [&] { return available; });
        test::observe_ui = true;
        std::string message;
        const auto frame = [&] {
            test::ui_targets.clear();
            editor.poll(project, scene, false, 0, message);
            capture.new_frame();
            ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({float(width), 1000}, ImGuiCond_Always);
            editor.draw(project, scene, locked);
            capture.render();
        };
        test::ImGuiInputDriver driver(frame);
        frame();
        frame();
        driver.click("animation:export-frames");
        check(requests == 1, "Native clip export button lost its existing routing callback");
        available = false;
        frame();
        check(!test::ui_targets.at("animation:export-frames").enabled,
              "Busy capture admitted another native clip export");
        available = true;
        locked = true;
        frame();
        check(!test::ui_targets.at("animation:export-frames").enabled,
              "Authoring lock admitted native clip export");
        locked = false;
        const auto token = editor.begin_frame_export();
        frame();
        check(!test::ui_targets.at("animation:export-frames").enabled,
              "Active clip lease admitted a second native export");
        editor.end_frame_export(token);
        frame();
        check(test::ui_targets.at("animation:export-frames").enabled && scene.snapshot() == saved,
              "Native clip export failed to restore controls or changed authored values");
        capture.save(scratch / (std::string(name) + ".png"));
        test::observe_ui = false;
    }
    SDL_Quit();
    std::cout
        << "Four native clip-export layouts: input/routing, busy/authoring/lease guards PASS\n";
}
