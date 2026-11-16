#pragma once
template <class Frame, class Click, class Target, class Mouse>
void camera_shot_strip_workflow(Scene& scene, AnimationTimelineEditor& editor,
                                ui::EditorUiContext& context, const std::string& controller,
                                const std::string& shot, const std::string& actor, bool& locked,
                                ImVec2& display, Frame&& frame, Click&& click, Target&& target,
                                Mouse&& mouse) {
    auto& io = ImGui::GetIO();
    click("button:Fit sequence");
    click("sequence:shot_timing_mode");
    struct Drag {
        Json source;
        CameraShot value;
        ImVec2 at;
        double delta;
        std::uint64_t revision;
        std::string diagnostic;
    };
    const auto begin = [&](const char* part, double seconds) {
        (void)target("sequence:shot_span:" + shot);
        const auto box = test::ui_targets.at("sequence:shot_span:" + shot);
        const auto value = scene.entity(shot).get<CameraShot>();
        const auto duration = scene.entity(controller).get<AnimationSequence>().duration;
        const auto width = box.maximum.x - box.minimum.x;
        const double time = std::string_view(part) == "start" ? value.start_seconds
                            : std::string_view(part) == "end"
                                ? value.end_seconds
                                : (value.start_seconds + value.end_seconds) / 2;
        const ImVec2 from{box.minimum.x + float(time / duration) * width,
                          (box.minimum.y + box.maximum.y) / 2};
        const ImVec2 to{from.x + float(seconds / duration) * width, from.y};
        const auto source = scene.document();
        const auto revision = scene.revision();
        mouse(from, false);
        mouse(from, true);
        const auto down = io.MousePos.x;
        mouse(to, true);
        check(scene.document() == source && scene.revision() == revision,
              "Shot drag published source before release");
        const double delta =
            std::round(double(io.MousePos.x - down) / width * duration * 1000) / 1000;
        std::string diagnostic;
        for (const auto& [name, box] : test::ui_targets) {
            (void)box;
            if (name.starts_with("sequence:shot_cancelled_geometry:"))
                diagnostic += name;
        }
        return Drag{source, value, to, delta, revision, diagnostic};
    };
    const auto commit = [&](const char* part, double seconds) {
        const auto drag = begin(part, seconds);
        mouse(drag.at, false);
        frame();
        auto expected = drag.value;
        if (std::string_view(part) == "move") {
            const auto duration = scene.entity(controller).get<AnimationSequence>().duration;
            const auto delta =
                std::clamp(drag.delta, -expected.start_seconds, duration - expected.end_seconds);
            expected.start_seconds += delta;
            expected.end_seconds += delta;
        } else if (std::string_view(part) == "start")
            expected.start_seconds += drag.delta;
        else
            expected.end_seconds += drag.delta;
        check(scene.entity(shot).get<CameraShot>() == expected,
              std::string("Actual shot ") + part + " expected " +
                  std::to_string(expected.start_seconds) + ":" +
                  std::to_string(expected.end_seconds) + " got " +
                  std::to_string(scene.entity(shot).get<CameraShot>().start_seconds) + ":" +
                  std::to_string(scene.entity(shot).get<CameraShot>().end_seconds) + " " +
                  drag.diagnostic);
        const auto committed = scene.document();
        check(committed != drag.source, "Changed shot gesture did not commit");
        editor.undo();
        frame();
        check(scene.document() == drag.source, "Shot gesture was not one Scene Undo step");
        editor.redo();
        frame();
        check(scene.document() == committed, "Shot gesture Redo changed unrelated source");
        editor.undo();
        frame();
    };
    const auto refuse = [&](const char* part, double seconds) {
        const auto drag = begin(part, seconds);
        mouse(drag.at, false);
        frame();
        check(scene.document() == drag.source && scene.revision() == drag.revision,
              "Invalid/no-op shot gesture changed source or history");
    };
    commit("move", 1);
    commit("move", -2); // Disjoint move must validate all component fields together.
    commit("start", .25);
    commit("end", .5);
    commit("move", 20); // Body moves clamp to the existing controller extent.
    commit("move", -20);
    refuse("move", 0);
    refuse("start", 2);
    refuse("end", -2);
    refuse("start", -10);
    refuse("end", 10);
    authoring_command(scene, "property.set_many",
                      {{"entity", shot},
                       {"component", "forge.camera_shot"},
                       {"values", {{"blend_in_seconds", .8}, {"blend_curve", 2u}}}});
    frame();
    refuse("end", -.4); // Trimming cannot silently shorten an incoming blend.
    authoring_command(scene, "property.set_many",
                      {{"entity", shot},
                       {"component", "forge.camera_shot"},
                       {"values", {{"blend_in_seconds", 0.}, {"blend_curve", 0u}}}});
    const auto neighbour =
        authoring_command(scene, "entity.create",
                          {{"recipe", "animation.camera_shot"},
                           {"name", "Neighbour shot"},
                           {"component_values",
                            {{"sequence", EntityRef{scene.asset_id(), EntityId::parse(controller)}},
                             {"camera", scene.entity(shot).get<CameraShot>().camera},
                             {"start_seconds", 4.5},
                             {"end_seconds", 5.5}}}})
            .at("selected")
            .get<std::string>();
    frame();
    refuse("move", 1);
    refuse("end", 1);
    authoring_command(scene, "entity.delete", {{"entity", neighbour}});
    frame();
    const auto early =
        authoring_command(scene, "entity.create",
                          {{"recipe", "animation.camera_shot"},
                           {"name", "Earlier shot created last"},
                           {"component_values",
                            {{"sequence", EntityRef{scene.asset_id(), EntityId::parse(controller)}},
                             {"camera", scene.entity(shot).get<CameraShot>().camera},
                             {"start_seconds", .25},
                             {"end_seconds", .75}}}})
            .at("selected")
            .get<std::string>();
    const auto ordered_source = scene.document();
    frame();
    check(test::ui_targets.at("sequence:shot_span:" + early).minimum.y <
                  test::ui_targets.at("sequence:shot_span:" + shot).minimum.y &&
              scene.document() == ordered_source,
          "Shot rows did not sort chronologically without reordering source");
    authoring_command(scene, "entity.delete", {{"entity", early}});
    frame();
    click("sequence:refresh");
    const auto export_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!editor.frame_export_ready()) {
        check(std::chrono::steady_clock::now() < export_deadline,
              "Camera cancellation fixture did not prepare its frame source");
        frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto cancel = [&](auto action) {
        const bool export_ready = editor.frame_export_ready();
        const auto drag = begin("move", .5);
        check(!export_ready || !editor.frame_export_ready(),
              "Frame export accepted an unfinished shot gesture");
        action();
        check(scene.document() == drag.source && scene.revision() == drag.revision,
              "Cancelling shot drag changed source/history");
        mouse(drag.at, false);
        frame();
        check(scene.document() == drag.source && scene.revision() == drag.revision,
              "Cancelled shot drag committed on later release");
        if (export_ready && editor.sequence_mode())
            check(editor.frame_export_ready(),
                  "Cancelling an unchanged shot gesture invalidated its ready frame source");
    };
    cancel([&] {
        io.AddKeyEvent(ImGuiKey_Escape, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Escape, false);
        frame();
    });
    cancel([&] {
        editor.undo();
        frame();
    });
    cancel([&] {
        const auto position = io.MousePos;
        mouse({-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()}, true);
        check(!ImGui::IsMousePosValid(), "Missing pointer fixture remained valid");
        mouse(position, true);
    });
    cancel([&] {
        io.AddFocusEvent(false);
        frame();
        io.AddFocusEvent(true);
        frame();
    });
    cancel([&] {
        locked = true;
        frame();
        locked = false;
        frame();
    });
    cancel([&] {
        display.x -= 24;
        frame();
        display.x += 24;
        frame();
    });
    cancel([&] {
        const auto previous = context.selection.entity();
        context.selection.select_entity(previous == actor ? controller : actor);
        frame();
        context.selection.select_entity(previous);
        frame();
    });
    {
        const auto drag = begin("move", .5);
        authoring_command(scene, "entity.rename",
                          {{"entity", actor}, {"name", "External shot edit"}});
        const auto external = scene.document();
        frame();
        mouse(drag.at, false);
        frame();
        check(scene.document() == external, "Stale shot gesture overwrote an external Scene edit");
    }
    cancel([&] {
        editor.request_save();
        frame();
    });
    cancel([&] {
        editor.request_close();
        frame();
    });
    context.selection.select_entity(controller);
    frame();
    click("button:Open sequence timeline");
    check(editor.sequence_mode(), "Closing a gesture prevented reopening its Timeline");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!test::ui_targets.contains("sequence:preview") ||
           !test::ui_targets.at("sequence:preview").enabled) {
        check(std::chrono::steady_clock::now() < deadline,
              "Reopened camera workflow was not ready");
        frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    click("sequence:track:" + actor);
}

template <class Frame, class Click, class Type>
void camera_shot_operations_workflow(Scene& scene, AnimationTimelineEditor& editor,
                                     const std::string& controller, const std::string& shot,
                                     const std::string& actor, Frame&& frame, Click&& click,
                                     Type&& type) {
    const auto before_setup = scene.document();
    click("sequence:shot:" + shot);
    auto value = scene.entity(shot).get<CameraShot>();
    authoring_command(scene, "property.set_many",
                      {{"entity", shot},
                       {"component", "forge.camera_shot"},
                       {"values",
                        {{"blend_in_seconds", (value.end_seconds - value.start_seconds) / 4},
                         {"blend_curve", 2u}}}});
    frame();
    value = scene.entity(shot).get<CameraShot>();
    const auto before = scene.document();
    const auto set_time = [&](double time) {
        const auto text = std::to_string(time);
        auto& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
        frame();
        type("sequence:playhead", text.c_str());
    };
    const auto history = [&](Json after) {
        editor.undo();
        frame();
        check(scene.document() == before, "Native shot operation was not one Scene Undo");
        editor.redo();
        frame();
        check(scene.document() == after, "Native shot operation Redo changed data/identity");
        editor.undo();
        frame();
        check(scene.document() == before, "Native shot operation repeat Undo lost source");
        click("sequence:shot:" + shot);
    };
    set_time(value.start_seconds);
    const auto revision = scene.revision();
    click("sequence:split_shot");
    check(scene.document() == before && scene.revision() == revision,
          "Native boundary split published partial source");
    const double split = (value.start_seconds + value.end_seconds) / 2;
    set_time(split);
    click("sequence:split_shot");
    const auto split_source = scene.document();
    check(scene.entity(shot).get<CameraShot>().end_seconds == split && split_source != before,
          "Native Split at playhead did not update the selected Shot");
    check(!editor.frame_export_ready(), "Split left a stale frame source available");
    history(split_source);
    set_time(value.end_seconds);
    click("sequence:duplicate_shot");
    const auto copied_source = scene.document();
    check(copied_source.at("entities").size() == before.at("entities").size() + 1 &&
              scene.entity(shot).get<CameraShot>() == value,
          "Native duplication changed original timing or failed to create its copy");
    history(copied_source);
    set_time(scene.entity(controller).get<AnimationSequence>().duration);
    const auto no_space_revision = scene.revision();
    click("sequence:duplicate_shot");
    check(scene.document() == before && scene.revision() == no_space_revision,
          "Native no-space duplication changed source/history");
    const auto pending_start = std::to_string(value.start_seconds + .01);
    type("sequence:shot_start", pending_start.c_str());
    frame();
    for (const auto* probe : {"sequence:split_shot", "sequence:duplicate_shot"})
        check(test::ui_targets.contains(probe) && !test::ui_targets.at(probe).enabled,
              "Pending fields did not disable shot transformations");
    editor.undo();
    frame();
    check(scene.document() == before, "Pending shot cancellation changed source");
    editor.undo();
    frame();
    check(scene.document() == before_setup,
          "Shot controls fixture did not restore its source setup");
    set_time(0);
    click("sequence:track:" + actor);
}

// Shared actual-control workflow for the full Timeline and focused camera gate.
template <class Frame, class Click, class Type, class Target, class Mouse>
std::string camera_sequence_workflow(Scene& scene, AnimationTimelineEditor& editor,
                                     ui::EditorUiContext& context, const std::string& controller,
                                     const std::string& actor, Frame&& frame, Click&& click,
                                     Type&& type, Target&& target, Mouse&& mouse) {
    const auto camera = authoring_command(scene, "entity.create",
                                          {{"name", "Equal camera name"},
                                           {"recipe", "render.camera"},
                                           {"component_values", {{"enabled", false}}}})
                            .at("selected")
                            .get<std::string>();
    const auto other_camera = authoring_command(scene, "entity.create",
                                                {{"name", "Equal camera name"},
                                                 {"recipe", "render.camera"},
                                                 {"component_values", {{"enabled", false}}}})
                                  .at("selected")
                                  .get<std::string>();
    context.selection.select_entity(camera);
    const auto before_shot = scene.document();
    click("sequence:add_shot");
    std::string shot;
    const auto with_shot = scene.document();
    for (const auto& row : with_shot.at("entities")) {
        const auto id = row.at("id").get<std::string>();
        if (scene.entity(id).has<CameraShot>())
            shot = id;
    }
    check(!shot.empty() &&
              scene.entity(shot).get<CameraShot>().camera.entity == EntityId::parse(camera),
          "Timeline did not create a bound shot from the selected camera");
    editor.undo();
    frame();
    check(scene.document() == before_shot, "Shot creation was not one Scene Undo transaction");
    editor.redo();
    frame();
    check(scene.entity(shot).get<CameraShot>().sequence.entity == EntityId::parse(controller),
          "Shot Redo did not retain its durable reference");
    const auto initial_shot = scene.entity(shot).get<CameraShot>();
    const auto blend = std::min(.25, (initial_shot.end_seconds - initial_shot.start_seconds) * .5);
    const auto blend_text = std::to_string(blend);
    type("sequence:shot_blend", blend_text.c_str());
    click("sequence:shot_curve");
    click("property:choice:blend_curve:1");
    click("sequence:apply_shot");
    expect_close(scene.entity(shot).get<CameraShot>().blend_in_seconds, blend);
    check(scene.entity(shot).get<CameraShot>().blend_curve == 1,
          "Timeline curve did not use the reflected enum choices");
    editor.undo();
    frame();
    check(scene.entity(shot).get<CameraShot>() == initial_shot,
          "Shot blend edit was not one Scene Undo transaction");
    editor.redo();
    frame();
    expect_close(scene.entity(shot).get<CameraShot>().blend_in_seconds, blend);
    check(scene.entity(shot).get<CameraShot>().blend_curve == 1,
          "Shot curve Redo did not restore its value with the duration");
    editor.undo();
    frame();
    type("sequence:shot_blend", "9999");
    click("sequence:apply_shot");
    check(scene.entity(shot).get<CameraShot>() == initial_shot,
          "Invalid shot blend changed authored values");
    click("sequence:reset_shot");
    type("sequence:shot_start", "3");
    type("sequence:shot_end", "4");
    check(scene.entity(shot).get<CameraShot>() == initial_shot &&
              !test::ui_targets.at("sequence:add_shot").enabled,
          "Pending shot fields were published or discarded by another action");
    click("sequence:apply_shot");
    expect_close(scene.entity(shot).get<CameraShot>().start_seconds, 3);
    expect_close(scene.entity(shot).get<CameraShot>().end_seconds, 4);
    editor.undo();
    frame();
    check(scene.entity(shot).get<CameraShot>() == initial_shot,
          "Shot interval edit was not one Scene Undo transaction");
    editor.redo();
    frame();
    type("sequence:shot_start", "2");
    authoring_command(scene, "entity.rename", {{"entity", actor}, {"name", "Renamed door"}});
    frame();
    click("sequence:apply_shot");
    expect_close(scene.entity(shot).get<CameraShot>().start_seconds, 3);
    click("sequence:reset_shot");
    type("sequence:shot_end", "7");
    click("sequence:apply_shot");
    expect_close(scene.entity(shot).get<CameraShot>().end_seconds, 4);
    click("sequence:reset_shot");
    click("sequence:shot_camera");
    check(!test::ui_targets.contains("entity-picker-row:" + actor),
          "Shot camera picker offered an object without Camera");
    click("entity-picker-row:" + other_camera);
    click("sequence:apply_shot");
    check(scene.entity(shot).get<CameraShot>().camera.entity == EntityId::parse(other_camera),
          "Same-name camera picker rebound by label instead of ID");
    click("sequence:refresh");
    const auto shot_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!test::ui_targets.at("sequence:preview").enabled) {
        frame();
        check(std::chrono::steady_clock::now() < shot_deadline, "Shot preview did not prepare");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    click("sequence:preview");
    const auto shot_span = target("sequence:shot_span:" + shot);
    const auto shot_box = test::ui_targets.at("sequence:shot_span:" + shot);
    const ImVec2 shot_sample{shot_box.minimum.x + (shot_box.maximum.x - shot_box.minimum.x) * .6f,
                             shot_span.y};
    mouse(shot_sample, false);
    mouse(shot_sample, true);
    mouse(shot_sample, false);
    frame();
    const auto shot_authored = scene.document();
    const auto shot_render =
        extract_render_scene(scene.preview_document(editor.preview(scene.document())));
    check(shot_render.cameras.size() == 1 &&
              shot_render.cameras.front().entity == EntityId::parse(other_camera),
          "Sequence preview did not project its selected Camera");
    check(scene.document() == shot_authored && !scene.entity(other_camera).get<Camera>().enabled,
          "Shot preview changed authored Camera values");
    editor.stop_preview();
    check(editor.preview(scene.document()) == scene.document(),
          "Stopping shot preview did not restore authored composition");
    click("sequence:remove_shot");
    check(!scene.entity(shot).get<CameraShot>().sequence.entity &&
              scene.entity(shot).get<CameraShot>().camera.entity == EntityId::parse(other_camera),
          "Removing shot destroyed its Camera reference");
    editor.undo();
    frame();
    click("sequence:shot:" + shot);
    type("sequence:shot_start", "2.5");
    editor.request_close();
    frame();
    click("button:Keep editing");
    check(editor.sequence_mode(), "Pending shot close cancelled the sequence view");
    editor.undo();
    frame();
    expect_close(scene.entity(shot).get<CameraShot>().start_seconds, 3);
    click("sequence:track:" + actor);
    return shot;
}
