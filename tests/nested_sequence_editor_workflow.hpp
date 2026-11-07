#pragma once
// The existing actual-input fixture supplies its pointer/keyboard/scroll driver.
template <class Frame, class Click, class Type>
void nested_editor_workflow(SceneDocument& project, Scene& scene, AnimationTimelineEditor& editor,
                            ui::EditorUiContext& owner, const std::string& parent, AssetId clip,
                            Frame frame, Click click, Type type) {
    const auto child =
        authoring_command(
            scene, "entity.create",
            {{"recipe", "animation.sequence"},
             {"name", "Local child sequence"},
             {"component_values",
              {{"duration", 4}, {"loop", true}, {"play_on_start", false}, {"playback_speed", 2}}}})
            .at("selected")
            .get<std::string>();
    const auto actor = authoring_command(scene, "entity.create",
                                         {{"recipe", "primitive.0"}, {"name", "Child platform"}})
                           .at("selected")
                           .get<std::string>();
    authoring_command(scene, "component.add",
                      {{"entity", actor}, {"component", "forge.transform_animator"}});
    const EntityRef child_ref{scene.asset_id(), EntityId::parse(child)};
    const Json binding{{"clip", clip}, {"sequence", child_ref}};
    for (const auto& [field, value] : binding.items())
        authoring_command(scene, "property.set",
                          {{"entity", actor},
                           {"component", "forge.transform_animator"},
                           {"field", field},
                           {"value", value}});
    owner.selection.select_entity(child);
    frame();
    const auto before = scene.document();
    click("sequence:add_nested");
    check(scene.entity(child).get<AnimationSequence>().sequence.entity == EntityId::parse(parent),
          "Add nested sequence lost its parent reference");
    editor.undo();
    frame();
    check(scene.document() == before, "Nested attachment was not one Scene Undo step");
    editor.redo();
    frame();
    click("sequence:nested:" + child);
    const auto timing = scene.document();
    type("sequence:nested_start", "1");
    type("sequence:nested_rate", "2");
    click("sequence:nested_loop");
    check(scene.document() == timing, "Pending nested start changed the Scene");
    click("sequence:apply_nested");
    near(scene.entity(child).get<AnimationSequence>().sequence_start_seconds, 1);
    near(scene.entity(child).get<AnimationSequence>().sequence_rate, 2);
    check(scene.entity(child).get<AnimationSequence>().sequence_loop,
          "Timeline placement loop did not reach the typed controller");
    editor.undo();
    frame();
    check(scene.document() == timing, "Nested start did not use Scene history");
    editor.redo();
    frame();
    const auto rated = scene.document();
    type("sequence:nested_rate", "0");
    click("sequence:apply_nested");
    check(scene.document() == rated, "Invalid placement rate partially changed Scene history");
    click("sequence:reset_nested");
    type("sequence:nested_rate", "1");
    click("sequence:apply_nested");
    near(scene.entity(child).get<AnimationSequence>().sequence_rate, 1);
    type("sequence:nested_start", "7");
    click("sequence:apply_nested");
    near(scene.entity(child).get<AnimationSequence>().sequence_start_seconds, 1);
    check(!test::ui_targets.at("sequence:open_nested").enabled &&
              !test::ui_targets.at("sequence:remove_nested").enabled,
          "Pending nested start could be silently discarded");
    click("sequence:reset_nested");
    const auto single = scene.document();
    click("sequence:add_nested_extra");
    type("sequence:nested_extra_start:0", "4");
    type("sequence:nested_extra_rate:0", "0.5");
    click("sequence:nested_extra_loop:0");
    check(scene.document() == single, "Pending repeated placement changed the Scene");
    click("sequence:apply_nested");
    const auto repeated = scene.document();
    const auto& extra = scene.entity(child).get<AnimationSequence>().sequence_extra_placements;
    check(extra.size() == 1 && extra[0].start_seconds == 4 && extra[0].rate == .5 && extra[0].loop,
          "Timeline additional start/rate/loop did not reach the native value");
    editor.undo();
    frame();
    check(scene.document() == single, "Repeated placement did not use one Undo operation");
    editor.redo();
    frame();
    check(scene.document() == repeated, "Repeated placement Redo lost its configuration");
    click("sequence:nested_loop"); // Finite primary would overlap the additional placement.
    click("sequence:apply_nested");
    check(scene.document() == repeated && !test::ui_targets.at("sequence:open_nested").enabled,
          "Overlapping repeated placement was partially applied");
    click("sequence:reset_nested");
    click("sequence:remove_nested_extra:0");
    check(scene.document() == repeated, "Removing a pending placement changed the Scene");
    click("sequence:apply_nested");
    check(scene.entity(child).get<AnimationSequence>().sequence_extra_placements.empty(),
          "Timeline did not remove just the extra placement");
    editor.undo();
    frame();
    check(scene.document() == repeated, "Placement removal Undo did not restore the list");
    const auto ready = [&] {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (!editor.frame_export_ready()) {
            check(std::chrono::steady_clock::now() < end, "Nested Timeline admission timed out");
            frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    click("sequence:refresh");
    ready();
    const auto authored = scene.document();
    const auto token = editor.begin_frame_export();
    editor.sample_frame_export(token, 2);
    auto preview = editor.preview(scene.document());
    bool found = false;
    for (const auto& row : preview.at("entities"))
        if (row.at("id") == actor) {
            near(row.at("components").at("forge.local_translation").at("x"), 0);
            found = true;
        }
    check(found && scene.document() == authored,
          "Parent frame sampling lost or authored its nested track");
    editor.end_frame_export(token);
    click("sequence:open_nested");
    ready();
    check(editor.frame_export_info().controller == child_ref &&
              test::ui_targets.contains("sequence:track:" + actor),
          "Open nested timeline did not compose the local child view");
    const auto local = editor.begin_frame_export();
    editor.sample_frame_export(local, 1);
    preview = editor.preview(scene.document());
    for (const auto& row : preview.at("entities"))
        if (row.at("id") == actor)
            near(row.at("components").at("forge.local_translation").at("x"), 0);
    editor.end_frame_export(local);
    click("sequence:open_parent");
    ready();
    check(editor.frame_export_info().controller.entity == EntityId::parse(parent),
          "Parent navigation lost its arrangement");
    click("sequence:nested:" + child);
    const auto placed = scene.document();
    click("sequence:remove_nested");
    const auto unbound = scene.entity(child).get<AnimationSequence>();
    check(!unbound.sequence.entity && unbound.sequence_loop && unbound.loop &&
              !unbound.play_on_start && unbound.playback_speed == 2 &&
              unbound.sequence_extra_placements.size() == 1,
          "Removing nested placement lost standalone settings");
    editor.undo();
    frame();
    check(scene.document() == placed, "Nested removal Undo did not restore its binding");
    check(project.dirty(), "Nested authoring bypassed the Scene document");
}
