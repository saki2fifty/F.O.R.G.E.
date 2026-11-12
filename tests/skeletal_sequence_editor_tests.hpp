#pragma once
#include "sequence_collection_editor_workflow.hpp"
void skeletal_sequence_editor(const std::filesystem::path& gallery,
                              const std::filesystem::path& scratch,
                              const std::filesystem::path& output, bool collections = false) {
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root.parent_path());
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
    {
        test::ImGuiSoftwareCapture capture(720, 1100);
        ImGui::GetIO().ConfigInputTrickleEventQueue = false;
        ui::style(1);
        EngineContext engine;
        Scene scene(engine.world());
        SceneDocument project(scene);
        project.open_project(root);
        project.open_scene("Scenes/36-animation-root-motion.scene.json");
        std::string actor;
        const auto original = scene.document();
        for (const auto& row : original.at("entities")) {
            const auto entity = scene.entity(row.at("id").get<std::string>());
            if (entity.has<Animator>() && entity.get<Animator>().root_motion == 0) {
                actor = row.at("id");
                break;
            }
        }
        check(!actor.empty(), "Missing original skeletal sequence model");
        const auto catalog = AssetCatalog::open_project(root);
        const auto model = scene.entity(actor).get<ModelSource>().model.id;
        const auto selected = asset_detail::load_model_selection(root, catalog, model);
        auto animator = scene.entity(actor).get<Animator>();
        animator.clip.id = selected.bindings.at("/animations/0");
        animator.playback_speed = 0;
        animator.play_on_start = false;
        scene.entity(actor).set(animator);
        const auto controller_id =
            authoring_command(scene, "entity.create", {{"recipe", "animation.sequence"}})
                .at("selected")
                .get<std::string>();
        AnimationSequence sequence;
        sequence.duration = 2;
        scene.entity(controller_id).set(sequence);
        const auto controller = scene.reference(controller_id);
        ui::EditorUiContext owner;
        ui::ContextScope scope(owner);
        owner.selection.select_entity(actor);
        AnimationTimelineEditor editor;
        editor.open_sequence(project, scene, controller);
        ComponentInspector inspector;
        EntityRef opened_controller;
        inspector.open_sequence_timeline = [&](EntityRef value) {
            opened_controller = value;
            editor.open_sequence(project, scene, value);
        };
        bool inspecting = false;
        test::observe_ui = true;
        std::string message;
        const auto frame = [&] {
            test::ui_targets.clear();
            editor.poll(project, scene, false, 0, message);
            capture.new_frame();
            ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({720, 1100}, ImGuiCond_Always);
            if (inspecting) {
                ImGui::Begin("Inspector fixture");
                inspector.draw(scene, project, actor);
                ImGui::End();
            } else
                editor.draw(project, scene, false);
            capture.render();
        };
        test::ImGuiInputDriver input(frame);
        const auto ready = [&] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (!editor.frame_export_ready()) {
                frame();
                check(std::chrono::steady_clock::now() < deadline,
                      "Skeletal Timeline admission timed out: " + message);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        };
        frame();
        frame();
        input.click("sequence:add");
        if (scene.entity(actor).get<Animator>().sequence != controller) {
            std::filesystem::create_directories(output.parent_path());
            capture.save(output);
        }
        check(scene.entity(actor).get<Animator>().sequence == controller &&
                  !scene.entity(actor).has<TransformAnimator>(),
              "Timeline did not bind the existing skeletal Animator");
        input.select(("sequence:track:" + actor + ":skeletal").c_str());
        input.click("sequence:refresh");
        ready();
        input.click("asset-picker:animation_clip:Next clip");
        input.type("asset-search:animation_clip:Next clip", "LocomotionColumn");
        input.select(("picker-option:" + selected.bindings.at("/animations/1").str()).c_str());
        expect_close(scene.entity(actor).get<Animator>().sequence_next_start_seconds, .5);
        const auto before = scene.document();
        input.type("sequence:start", "1");
        input.type("sequence:next_start", "1.5");
        input.click("sequence:apply_start");
        expect_close(scene.entity(actor).get<Animator>().sequence_start_seconds, 1);
        expect_close(scene.entity(actor).get<Animator>().sequence_next_start_seconds, 1.5);
        editor.undo();
        frame();
        check(scene.document() == before, "Skeletal timing was not one complete Undo transaction");
        if (collections)
            sequence_collection_editor_workflow(scene, editor, input, frame, ready, controller_id,
                                                actor, selected.bindings.at("/animations/0"));
        else {
            input.click("sequence:refresh");
            ready();
            const auto authored = scene.document();
            const auto revision = scene.revision();
            const auto token = editor.begin_frame_export();
            editor.sample_frame_export(token, .75);
            const auto preview = editor.preview(authored);
            const auto near_pose = [](double actual, double expected) {
                check(std::isfinite(actual) && std::abs(actual - expected) < .003,
                      "Stopped skeletal sample exceeds the native animation tolerance");
            };
            bool model_found = false, joint_found = false;
            for (const auto& row : preview.at("entities")) {
                if (row.at("id") == actor) {
                    check(row.at("model_animation_ready"), "Stopped skeletal model was not ready");
                    const auto& pose = row.at("animation_pose").at("local")[0];
                    near_pose(pose.at("translation")[0], .25);
                    near_pose(pose.at("translation")[2], .75);
                    model_found = true;
                }
                const auto entity = scene.entity(row.at("id").get<std::string>());
                if (entity.has<ModelSource>() &&
                    entity.get<ModelSource>().node.id == selected.bindings.at("/nodes/0") &&
                    entity.target(flecs::ChildOf) == scene.entity(actor)) {
                    near_pose(row.at("components").at("forge.local_translation").at("x"), .25);
                    near_pose(row.at("components").at("forge.local_translation").at("z"), .75);
                    joint_found = true;
                }
            }
            check(model_found && joint_found && scene.document() == authored &&
                      scene.revision() == revision,
                  "Stopped skeletal preview lost its native pose or changed authored content");
            editor.end_frame_export(token);
            input.click("sequence:remove");
            const auto standalone = scene.entity(actor).get<Animator>();
            check(!standalone.sequence.entity && !standalone.sequence_next_clip.id &&
                      standalone.clip == animator.clip && standalone.playback_speed == 0 &&
                      !standalone.play_on_start,
                  "Removing a skeletal track changed retained standalone settings");
            editor.undo();
            frame();
            inspector.fixture_focus_component = "forge.animator";
            inspecting = true;
            frame();
            frame();
            input.click("component-sequence:forge.animator");
            check(opened_controller == controller,
                  "Inspector did not open the skeletal player's owning sequence");
            inspecting = false;
            const auto light_scene = read_json(root / "Scenes/62-coordinated-animation.scene.json");
            AssetId property_clip;
            for (const auto& row : light_scene.at("entities"))
                if (row.at("components").contains("forge.light") &&
                    row.at("components").contains("forge.transform_animator"))
                    property_clip = row.at("components")
                                        .at("forge.transform_animator")
                                        .at("clip")
                                        .get<AssetId>();
            check(bool(property_clip), "Missing published mixed property clip");
            scene.entity(actor).set(Light{}).set(AudioSource{});
            TransformAnimator properties{{property_clip}};
            properties.sequence = controller;
            scene.entity(actor).set(properties);
            frame();
            input.select(("sequence:track:" + actor).c_str());
            input.click("sequence:refresh");
            ready();
            input.click("asset-picker:transform_animation:Next clip");
            input.type("asset-search:transform_animation:Next clip", "LightAndSound");
            input.select(("picker-option:" + property_clip.str()).c_str());
            expect_close(scene.entity(actor).get<TransformAnimator>().sequence_next_start_seconds,
                         1);
            expect_close(scene.entity(actor).get<Animator>().sequence_next_start_seconds, .5);
            const auto mixed = scene.document();
            input.type("sequence:start", ".25");
            input.type("sequence:next_start", "1.25");
            input.click("sequence:apply_start");
            expect_close(scene.entity(actor).get<TransformAnimator>().sequence_start_seconds, .25);
            expect_close(scene.entity(actor).get<Animator>().sequence_start_seconds, 0);
            editor.undo();
            frame();
            check(scene.document() == mixed,
                  "Mixed Timeline property editing changed the skeletal player or Undo boundary");
            input.select(("sequence:track:" + actor + ":skeletal").c_str());
            input.click("sequence:refresh");
            ready();
        }
        frame();
        std::filesystem::create_directories(output.parent_path());
        capture.save(output);
        test::observe_ui = false;
    }
    SDL_Quit();
}
