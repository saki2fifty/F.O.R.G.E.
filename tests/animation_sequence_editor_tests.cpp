#include "animation_timeline.hpp"
#include "component_inspector.hpp"
#include "fixtures/skeletal_sequence_gallery.hpp"
#include "imgui_input_driver.hpp"
#include "imgui_software_capture.hpp"
#include "model_selection.hpp"
#include "sequence_frame_plan.hpp"
#include <forge/engine_assets.hpp>
#include <forge/render_scene.hpp>
#include <imgui_internal.h>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
void check(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error(why);
}
void expect_close(double a, double b) {
    check(std::abs(a - b) < 1e-5,
          "Sequence UI numeric oracle differs: " + std::to_string(a) + " vs " + std::to_string(b));
}
#include "camera_sequence_editor_workflow.hpp"
#include "nested_sequence_editor_workflow.hpp"
#include "sequence_frame_export_tests.hpp"
void exercise(const std::filesystem::path& gallery, const std::filesystem::path& scratch,
              float scale, ImVec2 display, bool nested_only = false, bool camera_only = false) {
    const auto began = std::chrono::steady_clock::now();
    const auto progress = [&](const char* stage) {
        std::cerr << "Sequence UI " << scale << " " << display.x << "x" << display.y << " " << stage
                  << " "
                  << std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count()
                  << " s\n";
    };
    progress("begin");
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root.parent_path());
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    const auto controller =
        authoring_command(scene, "entity.create",
                          {{"name", "UI Sequence"}, {"recipe", "animation.sequence"}})
            .at("selected")
            .get<std::string>();
    authoring_command(scene, "property.set",
                      {{"entity", controller},
                       {"component", "forge.animation_sequence"},
                       {"field", "duration"},
                       {"value", 6}});
    const auto actor =
        authoring_command(scene, "entity.create", {{"name", "UI door"}, {"recipe", "primitive.0"}})
            .at("selected")
            .get<std::string>();
    authoring_command(scene, "property.set",
                      {{"entity", actor},
                       {"component", "forge.local_translation"},
                       {"field", "x"},
                       {"value", 42}});
    AssetId clip;
    const auto catalog = AssetCatalog::open_project(root);
    for (const auto& [id, row] : catalog.records())
        if (row.source == "Assets/EasedPlatform.animation.json")
            clip = id;
    check(bool(clip), "Missing original Gallery eased clip");
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = display;
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::style(scale);
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    context.selection.select_entity(controller);
    AnimationTimelineEditor editor;
    bool locked = false;
    std::string message;
    test::observe_ui = true;
    auto frame = [&] {
        test::ui_targets.clear();
        editor.poll(project, scene, locked, io.DeltaTime, message);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(display, ImGuiCond_Always);
        if (editor.is_open())
            editor.draw(project, scene, locked);
        else {
            ImGui::Begin("Content fixture");
            editor.content(project, scene, locked);
            ImGui::End();
        }
        if (editor.take_scene_save_request())
            project.save();
        ImGui::Render();
    };
    auto target = [&](const std::string& id) {
        frame();
        check(test::ui_targets.contains(id), "Missing sequence control " + id);
        auto box = test::ui_targets.at(id);
        const bool selectable =
            id.starts_with("picker-option:") || id.starts_with("sequence:track:") ||
            id.starts_with("sequence:nested:") || id.starts_with("sequence:shot:") ||
            id.starts_with("sequence:cue:") || id.starts_with("entity-picker-row:");
        auto inside = [&] {
            const ImVec2 center{(box.minimum.x + box.maximum.x) / 2,
                                (box.minimum.y + box.maximum.y) / 2};
            return selectable ? center.x >= box.clip_minimum.x && center.x <= box.clip_maximum.x &&
                                    center.y >= box.clip_minimum.y && center.y <= box.clip_maximum.y
                              : box.minimum.x >= box.clip_minimum.x &&
                                    box.maximum.x <= box.clip_maximum.x &&
                                    box.minimum.y >= box.clip_minimum.y &&
                                    box.maximum.y <= box.clip_maximum.y;
        };
        if (!inside()) {
            auto* window = ImGui::FindWindowByName("###Animation Timeline");
            check(window != nullptr, "Missing Timeline window");
            ImGui::SetScrollY(window, window->Scroll.y + (box.minimum.y + box.maximum.y) / 2 -
                                          (box.clip_minimum.y + box.clip_maximum.y) / 2);
            frame();
            frame();
            check(test::ui_targets.contains(id), "Sequence control disappeared " + id);
            box = test::ui_targets.at(id);
        }
        check(box.enabled && inside(),
              "Clipped/disabled sequence control " + id + " at " + std::to_string(scale));
        return ImVec2{(box.minimum.x + box.maximum.x) / 2, (box.minimum.y + box.maximum.y) / 2};
    };
    auto mouse = [&](ImVec2 p, bool down) {
        io.AddMousePosEvent(p.x, p.y);
        io.AddMouseButtonEvent(0, down);
        frame();
    };
    auto click = [&](const std::string& id) {
        const auto p = target(id);
        mouse(p, false);
        mouse(p, true);
        mouse(p, false);
        frame();
    };
    auto type = [&](const std::string& id, const char* text) {
        const auto p = target(id);
        mouse(p, false);
        mouse(p, true);
        mouse(p, false);
        check(ImGui::GetActiveID() == test::ui_targets.at(id).id,
              "Sequence typing did not focus its numeric field: " + id);
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
        io.AddKeyEvent(ImGuiKey_A, true);
        frame();
        io.AddKeyEvent(ImGuiKey_A, false);
        io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        io.AddInputCharactersUTF8(text);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        frame();
    };
    frame();
    frame();
    click("button:Open sequence timeline");
    check(editor.sequence_mode(), "Content did not open sequence mode");
    context.selection.select_entity(actor);
    if (camera_only) {
        click("sequence:add");
        authoring_command(scene, "property.set",
                          {{"entity", actor},
                           {"component", "forge.transform_animator"},
                           {"field", "clip"},
                           {"value", clip}});
        const auto shot = camera_sequence_workflow(scene, editor, context, controller, actor, frame,
                                                   click, type, target, mouse);
        camera_shot_strip_workflow(scene, editor, context, controller, shot, actor, locked, display,
                                   frame, click, target, mouse);
        camera_shot_operations_workflow(scene, editor, controller, shot, actor, frame, click, type);
        test::observe_ui = false;
        ImGui::DestroyContext();
        progress("camera workflow passed");
        return;
    }
    const auto before_add = scene.document();
    click("sequence:add");
    check(scene.entity(actor).has<TransformAnimator>() &&
              scene.entity(actor).get<TransformAnimator>().sequence.entity ==
                  EntityId::parse(controller),
          "Add object did not bind its sequence");
    editor.undo();
    frame();
    check(scene.document() == before_add, "Add track Undo was not one Scene transaction");
    editor.redo();
    frame();
    click("asset-picker:transform_animation:Clip");
    type("asset-search:transform_animation:Clip", "EasedPlatform.animation.json");
    click("picker-option:" + clip.str());
    check(scene.entity(actor).get<TransformAnimator>().clip.id == clip,
          "Typed asset picker did not assign clip");
    type("sequence:start", "1");
    expect_close(scene.entity(actor).get<TransformAnimator>().sequence_start_seconds, 0);
    click("sequence:apply_start");
    expect_close(scene.entity(actor).get<TransformAnimator>().sequence_start_seconds, 1);
    editor.undo();
    frame();
    expect_close(scene.entity(actor).get<TransformAnimator>().sequence_start_seconds, 0);
    editor.redo();
    frame();
    expect_close(scene.entity(actor).get<TransformAnimator>().sequence_start_seconds, 1);
    click("sequence:refresh");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!test::ui_targets.contains("sequence:preview") ||
           !test::ui_targets.at("sequence:preview").enabled) {
        check(std::chrono::steady_clock::now() < deadline, "Sequence preview did not become ready");
        frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto authored = scene.document();
    const auto revision = scene.revision();
    click("sequence:preview");
    check(editor.preview_active(), "Sequence preview did not start");
    const auto before_scrub = editor.sequence_presentation_generation();
    const auto span = target("sequence:span:" + actor);
    const auto box = test::ui_targets.at("sequence:span:" + actor);
    const ImVec2 sample{box.minimum.x + (box.maximum.x - box.minimum.x) * .25f, span.y};
    mouse(sample, false);
    mouse(sample, true);
    mouse(sample, false);
    frame();
    // ImGui quantizes pointer positions to pixels. Derive the actual requested
    // time from that position, then use an independent smoothstep pose oracle.
    const double requested =
        std::clamp(double(io.MousePos.x - box.minimum.x) / double(box.maximum.x - box.minimum.x),
                   0., 1.) *
        6;
    const double u = std::clamp((requested - 1) / 2, 0., 1.);
    const double expected_x = -3 + 6 * u * u * (3 - 2 * u);
    const auto preview = editor.preview(scene.document());
    bool found = false;
    for (const auto& row : preview.at("entities"))
        if (row.at("id") == actor) {
            expect_close(row.at("components").at("forge.local_translation").at("x"), expected_x);
            found = true;
        }
    check(found && scene.document() == authored && scene.revision() == revision,
          "Preview changed authored Scene/history");
    check(editor.sequence_presentation_generation() > before_scrub,
          "Manual Timeline scrub did not signal a presentation discontinuity");
    const auto held = editor.begin_frame_export();
    const auto frozen_generation = editor.sequence_presentation_generation();
    editor.sample_frame_export(held, 2);
    frame();
    check(editor.sequence_presentation_generation() == frozen_generation,
          "Explicit sampling/warmup changed its preview generation");
    editor.end_frame_export(held);
    check(editor.preview_active() && editor.preview(scene.document()) == preview,
          "Frame export failed to restore the prior scrubbed preview");
    click("sequence:play");
    const auto before_hold = editor.preview(scene.document());
    const auto playing_hold = editor.begin_frame_export();
    editor.sample_frame_export(playing_hold, 2);
    const auto held_preview = editor.preview(scene.document());
    frame();
    frame();
    check(editor.preview(scene.document()) == held_preview,
          "Running preview advanced during frame sampling");
    editor.end_frame_export(playing_hold);
    check(editor.preview(scene.document()) == before_hold,
          "Frame sampling failed to restore the running preview cursor");
    const auto resumed_generation = editor.sequence_presentation_generation();
    frame();
    check(editor.preview(scene.document()) != before_hold && scene.document() == authored &&
              scene.revision() == revision,
          "Frame sampling failed to resume preview playback without authored edits");
    check(editor.sequence_presentation_generation() == resumed_generation,
          "Continuous forward preview needlessly reset completed motion");
    editor.stop_preview();
    check(editor.preview(scene.document()) == scene.document(),
          "Stopping preview did not restore authored view");
    type("sequence:start", "99");
    check(!editor.frame_export_ready(), "Pending fields admitted a frame export");
    check(!test::ui_targets.at("sequence:add").enabled &&
              !test::ui_targets.at("sequence:remove").enabled &&
              !test::ui_targets.at("asset-picker:transform_animation:Clip").enabled,
          "Pending start could be silently discarded by another arrangement edit");
    click("sequence:apply_start");
    expect_close(scene.entity(actor).get<TransformAnimator>().sequence_start_seconds, 1);
    editor.undo();
    frame();
    expect_close(scene.entity(actor).get<TransformAnimator>().sequence_start_seconds, 1);
    type("sequence:start", "1.5");
    editor.request_close();
    frame();
    click("button:Keep editing");
    check(editor.sequence_mode(), "Pending-field close cancel lost the view");
    editor.request_save();
    frame();
    expect_close(scene.entity(actor).get<TransformAnimator>().sequence_start_seconds, 1.5);
    check(!project.dirty(), "Sequence Save did not use Scene persistence");
    locked = true;
    frame();
    check(!test::ui_targets.at("sequence:add").enabled &&
              !test::ui_targets.at("sequence:apply_start").enabled &&
              !test::ui_targets.at("sequence:refresh").enabled,
          "Sequence edits stayed enabled during Play");
    locked = false;
    frame();
    // Both slots use the same asset picker, copied runtime and Scene transaction.
    click("asset-picker:transform_animation:Next clip");
    type("asset-search:transform_animation:Next clip", "EasedPlatform.animation.json");
    click("picker-option:" + clip.str());
    check(scene.entity(actor).get<TransformAnimator>().sequence_next_clip.id == clip,
          "Next clip picker did not use the selected asset identity");
    expect_close(scene.entity(actor).get<TransformAnimator>().sequence_next_start_seconds, 1.5);
    // The prior preview is stale after Save; a new slot starts at the primary
    // offset when no current admitted duration is available.
    const auto before_timing = scene.document();
    type("sequence:start", "1.75");
    type("sequence:next_start", "2.75");
    check(scene.document() == before_timing, "Pending pair timing edited the Scene");
    click("sequence:apply_start");
    expect_close(scene.entity(actor).get<TransformAnimator>().sequence_start_seconds, 1.75);
    expect_close(scene.entity(actor).get<TransformAnimator>().sequence_next_start_seconds, 2.75);
    editor.undo();
    frame();
    check(scene.document() == before_timing, "Pair timing was not one Undo transaction");
    editor.redo();
    frame();
    type("sequence:next_start", "1");
    click("sequence:apply_start");
    expect_close(scene.entity(actor).get<TransformAnimator>().sequence_next_start_seconds, 2.75);
    click("button:Reset pending timing");
    click("sequence:refresh");
    {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (!editor.frame_export_ready()) {
            check(std::chrono::steady_clock::now() < end, "Pair preview admission timed out");
            frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    const auto pair_authored = scene.document();
    const auto pair_token = editor.begin_frame_export();
    editor.sample_frame_export(pair_token, 3.25);
    {
        const auto pair_preview = editor.preview(scene.document());
        bool found = false;
        for (const auto& row : pair_preview.at("entities"))
            if (row.at("id") == actor) {
                expect_close(row.at("components").at("forge.local_translation").at("x"), 1.375);
                found = true;
            }
        check(found, "Pair frame sample lost its actor");
    }
    editor.end_frame_export(pair_token);
    check(scene.document() == pair_authored, "Pair frame sampling edited source");
    click("sequence:remove");
    check(!scene.entity(actor).get<TransformAnimator>().sequence.entity &&
              scene.entity(actor).get<TransformAnimator>().clip.id == clip &&
              !scene.entity(actor).get<TransformAnimator>().sequence_next_clip.id,
          "Removing track lost clip/standalone settings");
    editor.undo();
    frame();
    check(scene.entity(actor).get<TransformAnimator>().sequence.entity ==
                  EntityId::parse(controller) &&
              scene.entity(actor).get<TransformAnimator>().sequence_next_clip.id == clip,
          "Remove track Undo lost binding");
    // Camera shots reuse Scene commands/history and the isolated sequence clock.
    progress("object tracks passed");
    nested_editor_workflow(project, scene, editor, context, controller, clip, frame, click, type);
    progress("nested sequences passed");
    if (nested_only) {
        test::observe_ui = false;
        ImGui::DestroyContext();
        return;
    }
    const auto shot = camera_sequence_workflow(scene, editor, context, controller, actor, frame,
                                               click, type, target, mouse);
    camera_shot_strip_workflow(scene, editor, context, controller, shot, actor, locked, display,
                               frame, click, target, mouse);
    camera_shot_operations_workflow(scene, editor, controller, shot, actor, frame, click, type);
    // One central workspace keeps the two document owners' pending choices distinct.
    progress("camera shots passed");
    const auto speaker =
        authoring_command(
            scene, "entity.create",
            {{"name", "Equal speaker name"},
             {"recipe", "audio.source"},
             {"component_values", {{"clip", AssetId::generate()}, {"play_on_start", false}}}})
            .at("selected")
            .get<std::string>();
    const auto other_speaker =
        authoring_command(
            scene, "entity.create",
            {{"name", "Equal speaker name"},
             {"recipe", "audio.source"},
             {"component_values", {{"clip", AssetId::generate()}, {"play_on_start", false}}}})
            .at("selected")
            .get<std::string>();
    context.selection.select_entity(speaker);
    const auto before_cue = scene.document();
    click("sequence:add_cue");
    std::string cue;
    const auto with_cue = scene.document();
    for (const auto& row : with_cue.at("entities"))
        if (row.at("components").contains("forge.audio_cue"))
            cue = row.at("id").get<std::string>();
    check(!cue.empty() &&
              scene.entity(cue).get<AudioCue>().source.entity == EntityId::parse(speaker),
          "Timeline did not create a cue bound to the selected source");
    const auto original_cue = scene.entity(cue).get<AudioCue>();
    editor.undo();
    frame();
    check(scene.document() == before_cue, "Cue creation did not undo in one Scene step");
    editor.redo();
    frame();
    click("sequence:cue:" + cue);
    type("sequence:cue_time", "4");
    check(scene.entity(cue).get<AudioCue>() == original_cue &&
              !test::ui_targets.at("sequence:add_cue").enabled &&
              !test::ui_targets.at("sequence:remove_cue").enabled,
          "Pending cue fields changed Scene intent or could be silently discarded");
    click("sequence:apply_cue");
    expect_close(scene.entity(cue).get<AudioCue>().time_seconds, 4);
    editor.undo();
    frame();
    check(scene.entity(cue).get<AudioCue>() == original_cue,
          "Cue field edit did not undo atomically");
    editor.redo();
    frame();
    type("sequence:cue_time", "3");
    authoring_command(scene, "entity.rename", {{"entity", actor}, {"name", "Cue stale door"}});
    frame();
    click("sequence:apply_cue");
    expect_close(scene.entity(cue).get<AudioCue>().time_seconds, 4);
    click("sequence:reset_cue");
    type("sequence:cue_time", "7");
    click("sequence:apply_cue");
    expect_close(scene.entity(cue).get<AudioCue>().time_seconds, 4);
    click("sequence:reset_cue");
    click("sequence:cue_source");
    check(!test::ui_targets.contains("entity-picker-row:" + actor),
          "Cue source picker offered a non-Audio Source");
    click("entity-picker-row:" + other_speaker);
    click("sequence:apply_cue");
    check(scene.entity(cue).get<AudioCue>().source.entity == EntityId::parse(other_speaker),
          "Same-name source picker rebound by label instead of ID");
    locked = true;
    frame();
    check(!test::ui_targets.at("sequence:add_cue").enabled &&
              !test::ui_targets.at("sequence:cue_time").enabled &&
              !test::ui_targets.at("sequence:apply_cue").enabled,
          "Audio Cue editing stayed enabled during Play");
    locked = false;
    frame();
    click("sequence:remove_cue");
    check(!scene.entity(cue).get<AudioCue>().sequence.entity &&
              scene.entity(cue).get<AudioCue>().source.entity == EntityId::parse(other_speaker),
          "Removing cue discarded source intent");
    editor.undo();
    frame();
    click("sequence:cue:" + cue);
    type("sequence:cue_time", "3.5");
    editor.request_close();
    frame();
    click("button:Keep editing");
    check(editor.sequence_mode(), "Cancelling cue close lost its pending draft");
    editor.request_save();
    frame();
    expect_close(scene.entity(cue).get<AudioCue>().time_seconds, 3.5);
    check(!project.dirty(), "Cue Save did not use Scene persistence");
    click("sequence:track:" + actor);
    progress("audio cues passed");
    editor.open(project, "Assets/EasedPlatform.animation.json");
    frame();
    check(editor.document() && !editor.sequence_mode(), "Sequence did not switch to clip mode");
    editor.bind(scene, actor);
    click("button:Capture authored pose");
    click("button:Add / Update key at playhead");
    check(editor.dirty(), "Clip fixture did not create a real source draft");
    editor.open_sequence(project, scene, {scene.asset_id(), EntityId::parse(controller)});
    frame();
    click("button:Keep editing");
    check(editor.document() && !editor.sequence_mode(), "Keep editing lost pending clip changes");
    editor.open_sequence(project, scene, {scene.asset_id(), EntityId::parse(controller)});
    frame();
    click("button:Discard");
    frame();
    check(editor.sequence_mode() && !editor.document(), "Discard did not complete mode switch");
    // Opening the sequence starts real asynchronous clip preparation. Wait for
    // its readiness indicator before locating the fields below that status.
    const auto reopen_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!test::ui_targets.at("sequence:preview").enabled) {
        frame();
        check(std::chrono::steady_clock::now() < reopen_deadline,
              "Reopened sequence preview did not become ready");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    frame();
    click("sequence:track:" + actor);
    type("sequence:start", "2");
    editor.open(project, "Assets/EasedPlatform.animation.json");
    frame();
    click("button:Keep editing");
    frame();
    check(editor.sequence_mode(), "Queued clip opened after pending sequence field was retained");
    expect_close(scene.entity(actor).get<TransformAnimator>().sequence_start_seconds, 1.75);
    editor.undo();
    frame();
    type("sequence:start", "2");
    check(!test::ui_targets.at("sequence:add").enabled,
          "Retyped sequence field was not retained as pending after Undo");
    scene.delete_subtree(controller);
    frame();
    editor.request_close();
    frame();
    click("button:Apply and close");
    check(editor.sequence_mode(), "Missing controller silently consumed a pending start");
    click("button:Keep editing");
    editor.undo(); // Cancel the pending field first.
    editor.undo(); // Scene history restores the removed controller.
    frame();
    check(scene.entity(controller) && scene.entity(controller).has<AnimationSequence>(),
          "Sequence history could not restore a deleted controller");
    type("sequence:start", "2");
    editor.open(project, "Assets/EasedPlatform.animation.json");
    const auto next_project = scratch / AssetId::generate().str();
    std::filesystem::copy(gallery, next_project, std::filesystem::copy_options::recursive);
    project.open_project(next_project);
    frame();
    check(!editor.is_open(), "A queued Timeline action escaped into another project");
    test::observe_ui = false;
    ImGui::DestroyContext();
    progress("passed");
}
#include "sequence_clip_capture.hpp"
#include "skeletal_sequence_editor_tests.hpp"
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string_view(argv[1]) == "--camera-frame-source") {
            std::filesystem::create_directories(argv[3]);
            frame_plans();
            for (std::uint32_t curve = 0; curve < 4; ++curve)
                camera_transition_frame_source(argv[2], argv[3], curve);
            camera_aim_frame_source(argv[2], argv[3]);
            camera_aim_frame_source(argv[2], argv[3], true);
            std::cout << "Stopped Camera transition frame source, all timing curves and attached "
                         "producer isolation, and animated Camera Aim frame source PASS\n";
            return 0;
        }
        if (argc == 4 && std::string_view(argv[1]) == "--camera-workflow") {
            for (float scale : {1.f, 1.5f, 2.f})
                for (auto display : {ImVec2{1400, 1500}, ImVec2{960, 640}})
                    exercise(argv[2], argv[3], scale, display, false, true);
            return 0;
        }
        if (argc == 4 && std::string_view(argv[1]) == "--nested-workflow") {
            exercise(argv[2], argv[3], 1.f, {960, 640}, true);
            return 0;
        }
        if (argc == 5 && std::string_view(argv[1]) == "--collections") {
            skeletal_sequence_editor(argv[2], argv[3], argv[4], true);
            return 0;
        }
        if (argc == 5 && std::string_view(argv[1]) == "--skeletal") {
            skeletal_sequence_editor(argv[2], argv[3], argv[4]);
            return 0;
        }
        if (argc == 5 && std::string_view(argv[1]) == "--capture-properties") {
            capture_sequence_pair(argv[2], argv[3], argv[4], SequenceCapture::Properties);
            return 0;
        }
        if (argc == 5 && std::string_view(argv[1]) == "--capture-bindings") {
            capture_sequence_pair(argv[2], argv[3], argv[4], SequenceCapture::Bindings);
            return 0;
        }
        if (argc == 5 && std::string_view(argv[1]) == "--capture-pair") {
            capture_sequence_pair(argv[2], argv[3], argv[4]);
            return 0;
        }
        if (argc == 5 && std::string_view(argv[1]) == "--capture-camera-transition") {
            capture_sequence_pair(argv[2], argv[3], argv[4], SequenceCapture::CameraTransition);
            return 0;
        }
        if (argc == 5 && std::string_view(argv[1]) == "--capture-nested") {
            capture_sequence_pair(argv[2], argv[3], argv[4], SequenceCapture::Nested);
            return 0;
        }
        if (argc == 5 && std::string_view(argv[1]) == "--capture-child-loops") {
            capture_sequence_pair(argv[2], argv[3], argv[4], SequenceCapture::ChildLoops);
            return 0;
        }
        if (argc == 5 && std::string_view(argv[1]) == "--capture-multiway") {
            capture_sequence_pair(argv[2], argv[3], argv[4], SequenceCapture::Multiway);
            return 0;
        }
        if (argc == 5 && std::string_view(argv[1]) == "--capture-placements") {
            capture_sequence_pair(argv[2], argv[3], argv[4], SequenceCapture::ControllerPlacements);
            return 0;
        }
        check(argc == 3, "Need Gallery and sequence UI scratch root");
        frame_plans();
        frame_source(argv[1], argv[2]);
        for (std::uint32_t curve = 0; curve < 4; ++curve)
            camera_transition_frame_source(argv[1], argv[2], curve);
        for (float scale : {1.f, 1.5f, 2.f})
            for (auto display : {ImVec2{1400, 1500}, ImVec2{960, 640}})
                exercise(argv[1], argv[2], scale, display);
        std::cout << "Actual sequence Timeline binding/asset picker/offset/Undo/copied "
                     "preview/save/close/Play lock at three scales passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
