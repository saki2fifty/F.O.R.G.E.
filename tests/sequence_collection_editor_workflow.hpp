#pragma once
void sequence_collection_editor_workflow(Scene& scene, AnimationTimelineEditor& editor,
                                         test::ImGuiInputDriver& input,
                                         const std::function<void()>& frame,
                                         const std::function<void()>& ready,
                                         const std::string& controller, const std::string& actor,
                                         AssetId forward) {
    authoring_command(scene, "property.set",
                      {{"entity", controller},
                       {"component", "forge.animation_sequence"},
                       {"field", "duration"},
                       {"value", 10}});
    input.click("sequence:refresh");
    ready();
    for (std::size_t i = 0; i < sequence_clip_limit - 2; ++i) {
        input.click("sequence:add_clip");
        check(scene.entity(actor).get<Animator>().sequence_extra_clips.size() == i + 1,
              "Timeline Add clip did not append one native placement");
        near(scene.entity(actor).get<Animator>().sequence_extra_clips.back().start_seconds,
             1.5 + i);
    }
    frame();
    check(!test::ui_targets.at("sequence:add_clip").enabled,
          "Timeline allowed a ninth clip placement");
    const auto eight = scene.document();
    input.type("sequence:extra_start:5", "10.25");
    input.click("sequence:apply_start");
    check(scene.document() == eight, "Out-of-duration collection timing edited the Scene");
    input.click("button:Reset pending timing");
    editor.undo();
    frame();
    check(scene.entity(actor).get<Animator>().sequence_extra_clips.size() == 5,
          "Adding a clip was not one Undo transaction");
    editor.redo();
    frame();
    check(scene.document() == eight, "Adding a clip did not Redo exactly");
    input.type("sequence:start", ".25");
    input.type("sequence:next_start", ".75");
    input.type("sequence:extra_start:0", "1.75");
    check(scene.document() == eight, "Pending collection timing edited the Scene");
    input.click("sequence:apply_start");
    near(scene.entity(actor).get<Animator>().sequence_start_seconds, .25);
    near(scene.entity(actor).get<Animator>().sequence_next_start_seconds, .75);
    near(scene.entity(actor).get<Animator>().sequence_extra_clips[0].start_seconds, 1.75);
    editor.undo();
    frame();
    check(scene.document() == eight, "Collection timing was not one complete Undo transaction");
    editor.redo();
    frame();
    input.type("sequence:extra_start:0", "1.875");
    scene.rename_entity(controller, "Collection controller renamed");
    const auto changed = scene.document();
    input.click("sequence:apply_start");
    check(scene.document() == changed, "Stale collection timing overwrote a newer Scene revision");
    input.click("button:Reset pending timing");
    input.click("asset-picker:animation_clip:Clip 8");
    input.type("asset-search:animation_clip:Clip 8", "LocomotionColumn");
    input.select(("picker-option:" + forward.str()).c_str());
    check(scene.entity(actor).get<Animator>().sequence_extra_clips.back().clip.id == forward,
          "Additional clip picker ignored the selected asset identity");
    const auto selected = scene.document();
    input.click("sequence:remove_extra:2");
    check(scene.entity(actor).get<Animator>().sequence_extra_clips.size() == 5,
          "Timeline Remove clip did not remove one placement");
    editor.undo();
    frame();
    check(scene.document() == selected, "Removing a clip did not Undo exactly");
    input.click("sequence:refresh");
    ready();
    const auto before_preview = scene.document();
    const auto revision = scene.revision();
    const auto token = editor.begin_frame_export();
    editor.sample_frame_export(token, 7.25);
    const auto preview = editor.preview(before_preview);
    bool sampled = false;
    for (const auto& row : preview.at("entities"))
        if (row.at("id") == actor) {
            const auto& pose = row.at("animation_pose").at("local")[0];
            check(std::abs(pose.at("translation")[2].get<double>() - 1.5) < .003,
                  "Copied Timeline preview did not sample the eighth clip's independent phase");
            sampled = true;
        }
    editor.end_frame_export(token);
    check(sampled && scene.document() == before_preview && scene.revision() == revision,
          "Collection preview changed authored content");
    input.click("sequence:remove");
    const auto standalone = scene.entity(actor).get<Animator>();
    check(!standalone.sequence.entity && !standalone.sequence_next_clip.id &&
              standalone.sequence_extra_clips.empty(),
          "Removing a collection track retained bound-only clip placements");
    editor.undo();
    frame();
    check(scene.document() == before_preview, "Track removal did not restore every placement");
    input.select(("sequence:track:" + actor + ":skeletal").c_str());
    input.click("sequence:refresh");
    ready();
}
