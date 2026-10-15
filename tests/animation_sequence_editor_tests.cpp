#include "animation_timeline.hpp"
#include <imgui_internal.h>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
void check(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error(why);
}
void near(double a, double b) {
    check(std::abs(a - b) < 1e-5,
          "Sequence UI numeric oracle differs: " + std::to_string(a) + " vs " + std::to_string(b));
}
void exercise(const std::filesystem::path& gallery, const std::filesystem::path& scratch,
              float scale, ImVec2 display) {
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
            id.starts_with("picker-option:") || id.starts_with("sequence:track:");
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
    near(scene.entity(actor).get<TransformAnimator>().sequence_start_seconds, 0);
    click("sequence:apply_start");
    near(scene.entity(actor).get<TransformAnimator>().sequence_start_seconds, 1);
    editor.undo();
    frame();
    near(scene.entity(actor).get<TransformAnimator>().sequence_start_seconds, 0);
    editor.redo();
    frame();
    near(scene.entity(actor).get<TransformAnimator>().sequence_start_seconds, 1);
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
    const auto preview = editor.preview(scene.effective_document());
    bool found = false;
    for (const auto& row : preview.at("entities"))
        if (row.at("id") == actor) {
            near(row.at("components").at("forge.local_translation").at("x"), expected_x);
            found = true;
        }
    check(found && scene.document() == authored && scene.revision() == revision,
          "Preview changed authored Scene/history");
    editor.stop_preview();
    check(editor.preview(scene.effective_document()) == scene.effective_document(),
          "Stopping preview did not restore authored view");
    type("sequence:start", "99");
    check(!test::ui_targets.at("sequence:add").enabled &&
              !test::ui_targets.at("sequence:remove").enabled &&
              !test::ui_targets.at("asset-picker:transform_animation:Clip").enabled,
          "Pending start could be silently discarded by another arrangement edit");
    click("sequence:apply_start");
    near(scene.entity(actor).get<TransformAnimator>().sequence_start_seconds, 1);
    editor.undo();
    frame();
    near(scene.entity(actor).get<TransformAnimator>().sequence_start_seconds, 1);
    type("sequence:start", "1.5");
    editor.request_close();
    frame();
    click("button:Keep editing");
    check(editor.sequence_mode(), "Pending-field close cancel lost the view");
    editor.request_save();
    frame();
    near(scene.entity(actor).get<TransformAnimator>().sequence_start_seconds, 1.5);
    check(!project.dirty(), "Sequence Save did not use Scene persistence");
    locked = true;
    frame();
    check(!test::ui_targets.at("sequence:add").enabled &&
              !test::ui_targets.at("sequence:apply_start").enabled &&
              !test::ui_targets.at("sequence:refresh").enabled,
          "Sequence edits stayed enabled during Play");
    locked = false;
    frame();
    click("sequence:remove");
    check(!scene.entity(actor).get<TransformAnimator>().sequence.entity &&
              scene.entity(actor).get<TransformAnimator>().clip.id == clip,
          "Removing track lost clip/standalone settings");
    editor.undo();
    frame();
    check(scene.entity(actor).get<TransformAnimator>().sequence.entity ==
              EntityId::parse(controller),
          "Remove track Undo lost binding");
    // One central workspace keeps the two document owners' pending choices distinct.
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
    near(scene.entity(actor).get<TransformAnimator>().sequence_start_seconds, 1.5);
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
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need Gallery and sequence UI scratch root");
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
