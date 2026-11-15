#include "asset_bytes.hpp"
#include "editor/ui_source_editor.hpp"
#include "imgui_input_driver.hpp"
#include "imgui_software_capture.hpp"
#include "ui_test_renderer.hpp"
#include <iostream>

namespace {
using namespace forge;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid canvas operation accepted");
}
void exercise(const std::filesystem::path& output, const std::vector<std::byte>& font, int width) {
    const auto root = output / AssetId::generate().str();
    SceneDocument::create_project(root, "UI canvas");
    std::filesystem::create_directories(root / "Assets");
    const std::string source = R"rml(<!-- retained source -->
<rml><head><style>body {display:block;width:100%;height:100%;font-family:Lato;font-size:20px;}
</style></head><body><div id="panel" style="position:absolute;left:80px;top:90px;width:200px;height:100px;padding:10px;box-sizing:border-box;color:#ffffff;">Canvas
<div id="nested" style="position:absolute;left:0px;top:0px;width:30px;height:20px;">N</div></div>
<div id="first" style="position:absolute;left:350px;top:90px;width:80px;height:50px;padding:10px;">First</div>
<div id="second" style="position:absolute;left:500px;top:220px;width:80px;height:50px;">Second</div></body></rml>
)rml";
    asset_storage::replace(root / "Assets/Canvas.rml", source);
    const auto asset = register_ui_document(root, "Assets/Canvas.rml");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    const auto scene_before = scene.snapshot();
    const auto catalog = asset_storage::read(root / "forge.assets.json");
    test::ImGuiSoftwareCapture capture(width, 950);
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    UiTestRenderer renderer;
    std::unique_ptr<UiPresenter> native;
    ui::UiSourceEditor editor;
    editor.preview = [&](const auto& path, std::string_view text, int w, int h, float density,
                         bool refresh) {
        if (!native) {
            native = std::make_unique<UiPresenter>(renderer, root, font);
        }
        if (native->preview_document({asset.id}, path, text, w, h, density, refresh))
            native->render();
        return ui::UiSourcePreviewFrame{native->preview_layout(), 0};
    };
    editor.preview_hit = [&](float x, float y) {
        return native ? native->preview_hit(x, y) : std::string{};
    };
    editor.close_preview = [&] { native.reset(); };
    ui::style(1);
    auto& io = ImGui::GetIO();
    io.ConfigInputTrickleEventQueue = false;
    test::observe_ui = true;
    bool locked = false;
    ui::DocumentWorkspace workspace;
    workspace.add(editor.workspace_document(project, context, [&] { return locked; }));
    const auto frame = [&] {
        test::ui_targets.clear();
        capture.new_frame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({float(width), 950}, ImGuiCond_Always);
        workspace.draw();
        capture.render();
    };
    editor.open(project, "Assets/Canvas.rml");
    auto view = editor.preview_state(project, 800, 600, 1);
    const auto element = view.at("elements").at(0).at("source_index").get<std::size_t>();
    check(view.at("elements").at(0).at("editable").get<bool>(),
          "Native canvas fixture is not editable");
    const auto index_for = [&](const char* id) {
        for (const auto& item : view.at("elements"))
            if (item.at("id") == id)
                return item.at("source_index").get<std::size_t>();
        throw std::runtime_error("Group fixture native element missing");
    };
    const auto first = index_for("first"), second = index_for("second"),
               nested = index_for("nested");
    const auto apply_group = [&](std::span<const UiMarkupLayoutEdit> changes) {
        view = editor.preview_state(project, 800, 600, 1);
        editor.layouts(changes, view.at("instance").get<std::string>(),
                       view.at("revision").get<std::uint64_t>());
    };
    const auto group_before = editor.state();
    const std::array group{UiMarkupLayoutEdit{first, {400, 100, 80, 50}},
                           UiMarkupLayoutEdit{second, {550, 230, 80, 50}}};
    auto invalid_group = group;
    invalid_group[1] = {element, {80, 90, 1, 1}};
    rejects([&] { apply_group(invalid_group); });
    check(editor.state() == group_before,
          "A later native-clamped group member partially changed source/history");
    invalid_group[1] = {nested, {1, 2, 30, 20}};
    rejects([&] { apply_group(invalid_group); });
    check(editor.state() == group_before, "Mixed-parent group layout changed the source/history");
    apply_group(group);
    const auto grouped_text = editor.state().at("text").get<std::string>();
    check(read_ui_markup_layout(grouped_text, first) == group[0].pixels &&
              read_ui_markup_layout(grouped_text, second) == group[1].pixels && editor.can_undo(),
          "Native group application did not publish both admitted source rectangles");
    editor.undo(false);
    check(editor.state().at("text") == group_before.at("text") && !editor.can_undo(),
          "Atomic native group application needed more than one Undo");
    editor.undo(true);
    check(editor.state().at("text") == grouped_text,
          "Native group Redo did not restore the complete candidate");
    editor.undo(false);
    view = editor.preview_state(project, 800, 600, 1);
    const std::array group_indices{first, element, second};
    editor.arrange(group_indices, "align_right", 0, 0, view.at("instance").get<std::string>(),
                   view.at("revision").get<std::uint64_t>());
    const auto aligned_text = editor.state().at("text").get<std::string>();
    check(read_ui_markup_layout(aligned_text, first) == UiPixelLayout{480, 90, 80, 50} &&
              read_ui_markup_layout(aligned_text, element) == UiPixelLayout{380, 90, 200, 100},
          "Native group alignment confused padded border width with source width");
    view = editor.preview_state(project, 800, 600, 1);
    for (const auto& item : view.at("elements"))
        if (item.at("id") == "first" || item.at("id") == "panel" || item.at("id") == "second")
            check(item.at("bounds").at(0).get<float>() + item.at("bounds").at(2).get<float>() ==
                      580,
                  "Aligned native group border edges differ from the actual candidate");
    editor.undo(false);
    check(editor.state().at("text") == group_before.at("text") && !editor.can_undo(),
          "Group alignment needed more than one native Undo");
    view = editor.preview_state(project, 800, 600, 1);
    const auto before_image = editor.state();
    rejects([&] { editor.image_source(project); });
    check(editor.state() == before_image,
          "Metadata-only image request changed the source or invented pixels");
    const auto before_clamp = editor.state();
    rejects([&] {
        editor.layout(element, {80, 90, 1, 1}, view.at("instance").get<std::string>(),
                      view.at("revision").get<std::uint64_t>());
    });
    check(editor.state() == before_clamp, "Native box-clamped candidate changed source/history");
    view = editor.preview_state(project, 800, 600, 1);
    // Source bytes alone cannot keep a retired native preview target valid.
    native.reset();
    const auto before = editor.state();
    rejects([&] {
        editor.layout(element, {1, 2, 3, 4}, view.at("instance").get<std::string>(),
                      view.at("revision").get<std::uint64_t>());
    });
    check(editor.state() == before, "Retired-preview edit changed source/history");
    view = editor.preview_state(project, 800, 600, 1);
    frame();
    frame();
    test::ImGuiInputDriver input(frame);
    input.click("ui-source:preview-tab");
    frame();
    const auto point = [&](float x, float y) {
        const auto box = test::ui_targets.at("ui-source:canvas");
        const float scale = (box.maximum.x - box.minimum.x) / 800;
        return ImVec2{box.minimum.x + x * scale, box.minimum.y + y * scale};
    };
    const auto press = [&](ImVec2 position) {
        io.AddMousePosEvent(position.x, position.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        check(editor.gesture_active(), "Canvas press did not own a gesture");
    };
    const auto release = [&] {
        io.AddMouseButtonEvent(0, false);
        frame();
        frame();
    };
    const auto original = editor.state().at("text");
    auto origin = point(150, 140);
    const auto destination = point(210, 170);
    press(origin);
    const auto pointer_start = ImGui::GetMousePos();
    auto canvas = test::ui_targets.at("ui-source:canvas");
    const float move_scale = (canvas.maximum.x - canvas.minimum.x) / 800;
    io.AddMousePosEvent(destination.x, destination.y);
    frame();
    const auto pointer_end = ImGui::GetMousePos();
    check(editor.state().at("text") == original && !editor.dirty() && !editor.can_undo(),
          "Drag wrote source before release");
    rejects([&] { editor.save(project); });
    release();
    auto pixels = read_ui_markup_layout(editor.state().at("text").get<std::string>(), element);
    std::cout << Json{{"window_width", width},
                      {"move", {pixels.left, pixels.top, pixels.width, pixels.height}},
                      {"pointer", {origin.x, origin.y, destination.x, destination.y}},
                      {"gesture", editor.gesture_active()}}
                     .dump()
              << '\n';
    check(std::abs(pixels.left - (80 + (pointer_end.x - pointer_start.x) / move_scale)) < .01f &&
              std::abs(pixels.top - (90 + (pointer_end.y - pointer_start.y) / move_scale)) < .01f &&
              pixels.width == 200 && pixels.height == 100,
          "Pointer movement changed the wrong pixels");
    const auto moved_pixels = pixels;
    const auto moved = editor.state().at("text");
    editor.undo(false);
    check(editor.state().at("text") == original, "Move was not one native Undo record");
    editor.undo(true);
    check(editor.state().at("text") == moved, "Move Redo changed source");
    frame();
    // A click selects without creating history; the next press owns resize.
    press(point(200, 160));
    release();
    origin = point(pixels.left + pixels.width - 2, pixels.top + pixels.height - 2);
    const auto resized = point(pixels.left + pixels.width + 38, pixels.top + pixels.height + 18);
    press(origin);
    const auto resize_start = ImGui::GetMousePos();
    canvas = test::ui_targets.at("ui-source:canvas");
    const float resize_scale = (canvas.maximum.x - canvas.minimum.x) / 800;
    io.AddMousePosEvent(resized.x, resized.y);
    frame();
    const auto resize_end = ImGui::GetMousePos();
    check(editor.state().at("text") == moved, "Resize wrote source before release");
    release();
    pixels = read_ui_markup_layout(editor.state().at("text").get<std::string>(), element);
    check(std::abs(pixels.width - (200 + (resize_end.x - resize_start.x) / resize_scale)) < .01f &&
              std::abs(pixels.height - (100 + (resize_end.y - resize_start.y) / resize_scale)) <
                  .01f &&
              pixels.left == moved_pixels.left && pixels.top == moved_pixels.top,
          "Resize changed position or used a different display scale");
    editor.undo(false);
    check(editor.state().at("text") == moved, "Resize was not one native Undo record");
    frame();
    origin = point(200, 160);
    press(origin);
    io.AddMousePosEvent(origin.x + 24, origin.y + 16);
    frame();
    io.AddKeyEvent(ImGuiKey_Escape, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Escape, false);
    release();
    check(!editor.gesture_active() && editor.state().at("text") == moved,
          "Escape committed a transient layout draft");
    press(point(200, 160));
    io.AddMousePosEvent(origin.x + 18, origin.y + 12);
    frame();
    io.AddFocusEvent(false);
    frame();
    check(!editor.gesture_active() && editor.state().at("text") == moved,
          "Application focus loss committed a transient layout draft");
    io.AddFocusEvent(true);
    release();
    press(point(200, 160));
    locked = true;
    frame();
    check(!editor.gesture_active() && editor.state().at("text") == moved,
          "Owning-task lock committed a canvas gesture");
    release();
    locked = false;
    frame();
    const auto toggle = [&](float x, float y) {
        const auto target = point(x, y);
        io.AddMousePosEvent(target.x, target.y);
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        frame();
        check(!editor.gesture_active(), "Ctrl selection started an unwanted group drag");
    };
    toggle(365, 115);
    toggle(520, 240);
    check(editor.state().at("selection").size() == 3 && editor.state().at("text") == moved,
          "Native preview Ctrl toggles changed source or lost group membership");
    const auto group_source = editor.state().at("text").get<std::string>();
    const auto selected_group = editor.state().at("selection").get<std::vector<std::size_t>>();
    const auto check_copies = [&](std::string_view prefix) {
        const auto current = editor.state();
        const auto text = current.at("text").get<std::string>();
        const auto roots = current.at("selection").get<std::vector<std::size_t>>();
        const auto nodes = inspect_ui_markup(text);
        check(roots.size() == 3 && roots != selected_group,
              "Group duplication did not select all new roots");
        const std::array ids{"panel", "first", "second"};
        for (std::size_t i = 0; i < roots.size(); ++i) {
            const auto opening = nodes.at(roots[i]).opening;
            check(text.substr(opening.begin, opening.end - opening.begin)
                          .find("id=\"" + std::string(prefix) + "-" + ids[i] + "\"") !=
                      std::string::npos,
                  "Group duplication did not preserve root source order and new IDs");
        }
        check(text.find("id=\"" + std::string(prefix) + "-nested\"") != text.npos &&
                  editor.preview_state(project, 800, 600, 1).at("elements").size() == 8,
              "Group duplication lost a descendant or actual native geometry");
        check(asset_storage::read(root / "Assets/Canvas.rml") == source &&
                  scene.snapshot() == scene_before &&
                  asset_storage::read(root / "forge.assets.json") == catalog,
              "Group duplication changed saved source, Scene or catalog");
        return text;
    };
    input.click("ui-source:elements-tab");
    input.type("ui-source:duplicate-prefix", "batch");
    input.click("ui-source:selection-duplicate");
    const auto copied_group = check_copies("batch");
    frame(); // Present the new outline and selection after the button's mutation.
    capture.save(output / ("ui-source-duplicate-group-" + std::to_string(width) + ".png"));
    editor.undo(false);
    check(editor.state().at("text") == group_source,
          "Elements group duplication needed more than one Undo");
    editor.undo(true);
    check(editor.state().at("text") == copied_group, "Elements group duplication Redo failed");
    editor.undo(false);
    editor.selection(selected_group);
    input.click("ui-source:preview-tab");
    frame();
    press(point(200, 160));
    release();
    const auto before_shortcut = editor.state();
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_D, true);
    frame();
    io.AddKeyEvent(ImGuiKey_D, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    frame();
    frame();
    // This CPU fixture observes native geometry without a borrowed image. The
    // actual rendered Ctrl+D path is exercised by ui_source_canvas_capture.
    check(editor.state() == before_shortcut,
          "Metadata-only canvas accepted a structural shortcut without an image");
    frame();
    const auto native_group = [&](const Json& snapshot) {
        std::array<UiPixelLayout, 3> rectangles;
        for (std::size_t i = 0; i < selected_group.size(); ++i) {
            bool found = false;
            for (const auto& item : snapshot.at("elements"))
                if (item.at("source_index") == selected_group[i]) {
                    const auto& b = item.at("bounds");
                    rectangles[i] = {b.at(0).get<float>(), b.at(1).get<float>(),
                                     b.at(2).get<float>(), b.at(3).get<float>()};
                    found = true;
                }
            check(found, "Group resize lost a native observation");
        }
        return ui_layout_bounds(rectangles);
    };
    for (const auto mode : {0, 1, 2}) {
        editor.selection(selected_group);
        frame();
        if (mode == 2)
            input.click("ui-source:snap-dragging");
        const auto before_resize = editor.state();
        const auto before_native = native_group(editor.preview_state(project, 800, 600, 1));
        frame();
        const auto handle = test::ui_targets.at("ui-source:canvas-group-resize");
        check(handle.enabled, "Complete native sibling selection has no resize handle");
        io.AddKeyEvent(ImGuiMod_Shift, mode == 1);
        frame();
        press(
            {(handle.minimum.x + handle.maximum.x) / 2, (handle.minimum.y + handle.maximum.y) / 2});
        const auto start = ImGui::GetMousePos();
        const auto canvas = test::ui_targets.at("ui-source:canvas");
        const double display_scale = (canvas.maximum.x - canvas.minimum.x) / 800;
        io.AddMousePosEvent(start.x + 24, start.y + 16);
        frame();
        const auto finish = ImGui::GetMousePos();
        check(editor.state().at("text") == group_source,
              "Group resize published before the real mouse release");
        release();
        io.AddKeyEvent(ImGuiMod_Shift, false);
        frame();
        const auto resized = editor.state();
        const auto bounds = native_group(editor.preview_state(project, 800, 600, 1));
        const double w = before_native[2] - before_native[0];
        const double h = before_native[3] - before_native[1];
        const double dx = (finish.x - start.x) / display_scale;
        const double dy = (finish.y - start.y) / display_scale;
        double expected_width = w + dx, expected_height = h + dy;
        if (mode == 1) {
            const double scale = 1 + (std::abs(dx / w) >= std::abs(dy / h) ? dx / w : dy / h);
            expected_width = w * scale;
            expected_height = h * scale;
        } else if (mode == 2) {
            expected_width = std::round(expected_width / 8) * 8;
            expected_height = std::round(expected_height / 8) * 8;
        }
        check(std::abs(bounds[0] - before_native[0]) < .1 &&
                  std::abs(bounds[1] - before_native[1]) < .1 &&
                  std::abs(bounds[2] - bounds[0] - expected_width) < .6 &&
                  std::abs(bounds[3] - bounds[1] - expected_height) < .6 &&
                  resized.at("selection") == selected_group,
              "Real group handle changed its union anchor, aspect, grid or selection");
        editor.undo(false);
        check(editor.state().at("text") == before_resize.at("text"),
              "Group resize needed more than one Undo");
        editor.undo(true);
        check(editor.state().at("text") == resized.at("text"), "Group resize Redo changed source");
        editor.undo(false);
        frame();
        if (mode == 2)
            input.click("ui-source:snap-dragging");
    }
    editor.selection(selected_group);
    frame();
    const auto resize_handle = test::ui_targets.at("ui-source:canvas-group-resize");
    const auto before_cancel_resize = editor.state();
    press({(resize_handle.minimum.x + resize_handle.maximum.x) / 2,
           (resize_handle.minimum.y + resize_handle.maximum.y) / 2});
    io.AddMousePosEvent(ImGui::GetMousePos().x + 18, ImGui::GetMousePos().y + 10);
    frame();
    io.AddKeyEvent(ImGuiKey_Escape, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Escape, false);
    release();
    check(!editor.gesture_active() && editor.state().at("text") == group_source &&
              editor.state().at("revision") == before_cancel_resize.at("revision"),
          "Escape published a transient group resize");
    editor.selection(selected_group);
    frame();
    origin = point(365, 115);
    press(origin);
    canvas = test::ui_targets.at("ui-source:canvas");
    const float group_scale = (canvas.maximum.x - canvas.minimum.x) / 800;
    const auto group_start = ImGui::GetMousePos();
    io.AddMousePosEvent(origin.x + 24, origin.y + 14);
    frame();
    const auto group_end = ImGui::GetMousePos();
    check(editor.state().at("text") == group_source, "Group drag published before release");
    release();
    const auto group_moved = editor.state().at("text").get<std::string>();
    for (const auto index : group_indices) {
        const auto before = read_ui_markup_layout(group_source, index);
        const auto after = read_ui_markup_layout(group_moved, index);
        check(std::abs(after.left - before.left - (group_end.x - group_start.x) / group_scale) <
                      .01f &&
                  std::abs(after.top - before.top - (group_end.y - group_start.y) / group_scale) <
                      .01f &&
                  after.width == before.width && after.height == before.height,
              "Group drag distorted spacing, sizes or a selected member");
    }
    editor.undo(false);
    check(editor.state().at("text") == group_source, "Group drag needed more than one Undo");
    editor.undo(true);
    check(editor.state().at("text") == group_moved, "Group drag Redo lost a member");
    editor.undo(false);
    frame();
    toggle(365, 115);
    toggle(520, 240);
    check(editor.state().at("selection").size() == 2, "Group reselection failed after Undo");
    origin = point(365, 115);
    press(origin);
    io.AddMousePosEvent(origin.x + 18, origin.y + 10);
    frame();
    io.AddKeyEvent(ImGuiKey_Escape, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Escape, false);
    release();
    check(editor.state().at("text") == group_source && !editor.gesture_active(),
          "Escape published a transient group move");
    input.click("ui-source:group-apply");
    const auto left_aligned = editor.state().at("text").get<std::string>();
    check(read_ui_markup_layout(left_aligned, first).left == 350 &&
              read_ui_markup_layout(left_aligned, second).left == 350,
          "Actual group controls did not align both source members");
    editor.undo(false);
    check(editor.state().at("text") == group_source, "Group controls needed more than one Undo");
    frame();
    toggle(200, 160);
    toggle(365, 115);
    toggle(520, 240);
    input.click("ui-source:group-action");
    input.select("ui-source:group-choice:distribute_horizontal");
    input.click("ui-source:group-apply");
    view = editor.preview_state(project, 800, 600, 1);
    std::vector<std::array<float, 4>> spaced;
    for (const auto& item : view.at("elements"))
        if (item.at("id") == "panel" || item.at("id") == "first" || item.at("id") == "second")
            spaced.push_back(item.at("bounds").get<std::array<float, 4>>());
    std::ranges::sort(spaced, {}, [](const auto& box) { return box[0]; });
    check(spaced.size() == 3 && std::abs((spaced[1][0] - spaced[0][0] - spaced[0][2]) -
                                         (spaced[2][0] - spaced[1][0] - spaced[1][2])) < .01f,
          "Actual distribution controls did not create equal native border gaps");
    editor.undo(false);
    check(editor.state().at("text") == group_source, "Distribution needed more than one Undo");
    frame();
    toggle(365, 115);
    toggle(520, 240);
    check(!test::ui_targets.at("ui-source:group-apply").enabled &&
              test::ui_targets.at("ui-source:group-remove").enabled,
          "A distribution size limit disabled an otherwise valid source removal");
    input.click("ui-source:group-remove");
    const auto removed_group = editor.state().at("text").get<std::string>();
    check(removed_group.find("id=\"first\"") == removed_group.npos &&
              removed_group.find("id=\"second\"") == removed_group.npos &&
              removed_group.find("id=\"panel\"") != removed_group.npos,
          "Preview group removal left a member or removed unrelated source");
    input.click("button:Undo");
    check(editor.state().at("text") == group_source && !editor.gesture_active(),
          "Preview source removal needed more than one Undo");
    frame();
    const auto problems = context.problems.size();
    editor.replace_text("<rml>");
    frame();
    check(context.problems.size() == problems + 1,
          "Preview failure did not reach the Problems owner");
    frame();
    check(context.problems.size() == problems + 1,
          "Unchanged preview error duplicated Problems entries");
    editor.undo(false);
    frame();
    check(editor.state().at("text") == moved, "Restoring a failed draft lost layout history");
    press(point(200, 160));
    editor.request_close();
    check(!editor.gesture_active() && editor.close_pending(),
          "Close did not cancel before its dirty guard");
    editor.discard();
    release();
    check(!editor.is_open() && !native && renderer.geometry.empty() && renderer.textures.empty(),
          "Closing the source retained native preview resources");
    check(scene.snapshot() == scene_before &&
              asset_storage::read(root / "forge.assets.json") == catalog &&
              asset_storage::read(root / "Assets/Canvas.rml") == source,
          "Canvas gestures changed saved source, Scene or catalog");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need scratch directory and UI font");
        std::filesystem::create_directories(argv[1]);
        const auto font = forge::asset_detail::read_bytes(argv[2], 4 * 1024 * 1024);
        for (const int width : {1180, 700, 400})
            exercise(std::filesystem::absolute(argv[1]), font, width);
        std::cout << "Native source-canvas move/resize/cancel/history at1180/700/400 passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
