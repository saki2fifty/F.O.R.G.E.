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
</style></head><body><div id="panel" style="position:absolute;left:80px;top:90px;width:200px;height:100px;padding:10px;box-sizing:border-box;color:#ffffff;">Canvas</div></body></rml>
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
