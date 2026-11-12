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
        expect_close(scene.entity(actor).get<Animator>().sequence_extra_clips.back().start_seconds,
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
    expect_close(scene.entity(actor).get<Animator>().sequence_start_seconds, .25);
    expect_close(scene.entity(actor).get<Animator>().sequence_next_start_seconds, .75);
    expect_close(scene.entity(actor).get<Animator>().sequence_extra_clips[0].start_seconds, 1.75);
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
    input.click("sequence:multiway");
    check(scene.document() == before_preview && !editor.frame_export_ready(),
          "Pending overlap mode mutated the Scene or allowed export");
    input.type("sequence:start", "0");
    input.type("sequence:next_start", ".125");
    for (std::size_t i = 0; i < sequence_clip_limit - 2; ++i)
        input.type(("sequence:extra_start:" + std::to_string(i)).c_str(),
                   std::to_string(.125 * (i + 2)).c_str());
    input.click("sequence:apply_start");
    check(scene.entity(actor).get<Animator>().sequence_multiway,
          "Timeline overlap opt-in did not reach native authoring");
    const auto layered = scene.document();
    editor.undo();
    frame();
    check(scene.document() == before_preview,
          "Overlap mode and every pending start were not one complete Undo step");
    editor.redo();
    frame();
    check(scene.document() == layered, "Multiway timing Redo changed intent");
    input.click("sequence:refresh");
    ready();
    const auto layered_token = editor.begin_frame_export();
    editor.sample_frame_export(layered_token, .75);
    const auto layered_preview = editor.preview(layered);
    double first_weight = 1;
    for (std::size_t i = 1; i < sequence_clip_limit; ++i)
        first_weight *= 1 - std::clamp((.75 - .125 * i) / .875, 0., 1.);
    bool layered_sampled = false;
    for (const auto& row : layered_preview.at("entities"))
        if (row.at("id") == actor) {
            check(std::abs(
                      row.at("animation_pose").at("local")[0].at("translation")[2].get<double>() -
                      1.5 * first_weight) < .003,
                  "Timeline multiway preview used its two-slot summary as a pose recipe");
            layered_sampled = true;
        }
    editor.end_frame_export(layered_token);
    check(layered_sampled && scene.document() == layered,
          "Multiway Timeline preview changed authored content");
    editor.undo();
    frame();
    check(scene.document() == before_preview, "Multiway preview disturbed the complete Undo step");
    // Resetting a pending mode and rejecting a stale combined edit must keep the
    // same Scene revision/history owner as timing-only edits.
    input.click("sequence:multiway");
    input.click("button:Reset pending timing");
    frame();
    check(scene.document() == before_preview &&
              !scene.entity(actor).get<Animator>().sequence_multiway,
          "Reset pending timing committed the overlap mode");
    input.click("sequence:multiway");
    scene.rename_entity(controller, "Multiway controller renamed");
    const auto newer = scene.document();
    input.click("sequence:apply_start");
    check(scene.document() == newer && !scene.entity(actor).get<Animator>().sequence_multiway,
          "A stale pending overlap mode overwrote the newer Scene revision");
    input.click("button:Reset pending timing");
    editor.undo();
    frame();
    check(scene.document() == before_preview, "Rejected overlap mode changed rename Undo history");
    input.click("sequence:refresh");
    ready();
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
