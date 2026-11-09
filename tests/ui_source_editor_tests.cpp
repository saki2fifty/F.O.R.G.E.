#include "editor/ui_source_editor.hpp"
#include "imgui_input_driver.hpp"
#include "imgui_software_capture.hpp"
#include <imgui_internal.h>
#include <iostream>

namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F&& operation) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Invalid source owner operation accepted");
}
void exercise(const std::filesystem::path& output, int width) {
    using namespace forge;
    const auto root = output / AssetId::generate().str();
    SceneDocument::create_project(root, "UI source editor");
    std::filesystem::create_directories(root / "Assets");
    const auto file = root / "Assets/Hud.rml";
    const std::string source = "<!-- retained -->\r\n<rml><body><p>First</p></body></rml>\r\n";
    asset_storage::replace(file, source);
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    const auto authored = scene.snapshot();
    const auto revision = scene.revision();
    const auto catalog = asset_storage::read(root / "forge.assets.json");
    test::ImGuiSoftwareCapture capture(width, 950);
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    ui::UiSourceEditor editor;
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
    editor.open(project, "Assets/Hud.rml");
    frame();
    frame();
    check(!editor.dirty() && editor.outline().size() == 3, "Opening source changed its baseline");
    editor.save(project);
    check(asset_storage::read(file) == source, "Unchanged Save normalized original source bytes");
    const auto original = editor.state().at("text");
    const auto old_clipboard = ImGui::GetPlatformIO().Platform_GetClipboardTextFn;
    const auto old_data = ImGui::GetPlatformIO().Platform_ClipboardUserData;
    test::ImGuiInputDriver input(frame);
    input.click("ui-source:elements-tab");
    input.click("ui-source:element:2");
    input.type("ui-source:duplicate-prefix", "second");
    input.click("ui-source:element-duplicate");
    auto duplicate_expected = original.get<std::string>();
    const std::string opening = "<p>First</p>";
    duplicate_expected.replace(duplicate_expected.find(opening), opening.size(),
                               opening + "<p id=\"second\">First</p>");
    check(editor.state().at("text") == duplicate_expected &&
              editor.state().at("selection") == Json::array({3}),
          "Actual duplication controls lost the copied source, ID prefix or new selection");
    input.click("button:Undo");
    check(editor.state().at("text") == original && !editor.dirty(),
          "Native subtree duplication needed more than one Undo");
    input.click("button:Redo");
    check(editor.state().at("text") == duplicate_expected,
          "Native subtree duplication Redo lost source");
    input.click("ui-source:element:3");
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    frame();
    input.click("ui-source:element:2");
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    frame();
    check(editor.state().at("selection").size() == 2,
          "Elements Ctrl selection did not retain both source siblings");
    input.click("ui-source:selection-remove");
    check(editor.outline().size() == 2 && editor.state().at("selection").empty(),
          "Actual group removal left a source member or stale selection");
    input.click("button:Undo");
    check(editor.state().at("text") == duplicate_expected,
          "Native source group removal needed more than one Undo");
    input.click("button:Undo");
    check(editor.state().at("text") == original,
          "Copy/remove history did not restore its baseline");
    input.click("ui-source:element:2");
    input.type("ui-source:literal", "New & <literal>");
    input.click("ui-source:text-set");
    check(editor.dirty() && editor.state().at("text").get<std::string>().find(
                                "New &amp; &lt;literal&gt;") != std::string::npos,
          "Actual element text controls did not edit the owning source widget");
    const auto edited = editor.state().at("text");
    input.click("button:Undo");
    check(editor.state().at("text") == original && !editor.dirty(),
          "Native source Undo did not restore its single operation");
    input.click("button:Redo");
    check(editor.state().at("text") == edited && editor.dirty(), "Native source Redo lost intent");
    check(ImGui::GetPlatformIO().Platform_GetClipboardTextFn == old_clipboard &&
              ImGui::GetPlatformIO().Platform_ClipboardUserData == old_data,
          "Structured source replacement changed the OS clipboard adapter");
    const auto before_invalid = editor.state();
    rejects([&] { editor.attribute(2, "style", "font-size:999999px;"); });
    check(editor.state() == before_invalid, "Invalid attribute changed source/history/revision");
    input.click("ui-source:save");
    check(!editor.dirty() && asset_storage::read(file)->ends_with("\r\n"),
          "Saved UI source lost its uniform CRLF convention");
    check(asset_storage::read(file)->find("New &amp; &lt;literal&gt;") != std::string::npos,
          "Native source Save did not persist the candidate");
    input.click("ui-source:element:2");
    frame();
    capture.save(output / ("ui-source-elements-" + std::to_string(width) + ".png"));
    input.click("ui-source:source-tab");
    frame();
    capture.save(output / ("ui-source-text-" + std::to_string(width) + ".png"));
    const auto saved = asset_storage::read(file);
    input.click("ui-source:elements-tab");
    input.click("ui-source:element:2");
    input.type("ui-source:attribute-name", "class");
    input.type("ui-source:attribute-value", "accent");
    input.click("ui-source:attribute-set");
    check(editor.state().at("text").get<std::string>().find("class=\"accent\"") !=
              std::string::npos,
          "Native attribute controls did not edit the source");
    input.click("button:Undo");
    input.click("ui-source:element:1");
    input.click("ui-source:child-add");
    check(editor.outline().size() == 4 && editor.outline()[3].tag == "div",
          "Native Add child did not append to the selected container");
    input.click("ui-source:element:3");
    input.click("button:Move up");
    check(editor.outline()[2].tag == "div", "Native Move up lost sibling order");
    input.click("ui-source:element:2");
    input.click("button:Move down");
    check(editor.outline()[3].tag == "div", "Native Move down lost sibling order");
    input.click("ui-source:element:3");
    input.click("ui-source:element-remove");
    check(editor.outline().size() == 3 && !editor.dirty(),
          "Native structural controls did not restore the saved draft");
    editor.replace_text("<rml><body>");
    check(!editor.state().at("outline_error").get<std::string>().empty(),
          "Incomplete source did not remain editable with an explicit outline error");
    rejects([&] { editor.save(project); });
    check(asset_storage::read(file) == saved, "Invalid draft replaced usable saved UI");
    editor.undo(false);
    editor.text(2, "Unsaved");
    const std::string external = "<rml><body><p>External</p></body></rml>";
    asset_storage::replace(file, external);
    rejects([&] { editor.save(project); });
    check(asset_storage::read(file) == external && editor.dirty(),
          "Save conflict lost external bytes or the source draft");
    rejects([&] { editor.reload(project); });
    editor.reload(project, true);
    check(!editor.dirty() &&
              editor.state().at("text").get<std::string>().find("External") != std::string::npos,
          "Explicit reload did not replace the draft and baseline");
    locked = true;
    frame();
    rejects([&] { editor.replace_text("<rml><body></body></rml>"); });
    locked = false;
    frame();
    editor.text(2, "Pending close");
    check(workspace.source_drafts_dirty() && !workspace.close_pending_sources() &&
              editor.close_pending(),
          "Project close bypassed the UI source draft");
    input.click("button:Keep editing");
    check(workspace.consume_close_cancellation() && editor.dirty() && !editor.close_pending(),
          "Native close cancellation lost the source or blocked future edits");
    check(!workspace.close_pending_sources(), "Dirty source stopped blocking close");
    input.click("button:Discard");
    check(!editor.is_open() && !workspace.source_drafts_dirty() &&
              workspace.close_pending_sources(),
          "Explicit discard did not settle the workspace source guard");
    editor.open(project, "Assets/Hud.rml");
    const auto moved = root / "Assets/Moved.rml";
    std::filesystem::rename(file, moved);
    editor.source_file_changed("Assets/Hud.rml", std::filesystem::path("Assets/Moved.rml"));
    check(editor.state().at("path") == "Assets/Moved.rml",
          "Clean source move lost the open document");
    editor.text(2, "Moved source");
    editor.save(project);
    check(!std::filesystem::exists(file) &&
              asset_storage::read(moved)->find("Moved source") != std::string::npos,
          "Save after relocation wrote to the old source");
    std::filesystem::remove(moved);
    editor.source_file_changed("Assets/Moved.rml", {});
    check(!editor.is_open(), "Deleted clean source retained its document");
    asset_storage::replace(file, external);
    editor.open(project, "Assets/Hud.rml");
    check(scene.snapshot() == authored && scene.revision() == revision &&
              asset_storage::read(root / "forge.assets.json") == catalog,
          "UI source editing changed Scene history or registered resources implicitly");
    const auto before_large_attribute = editor.state();
    rejects([&] { editor.attribute(2, "class", std::string(9000, 'a')); });
    check(editor.state() == before_large_attribute,
          "Oversized attribute changed the source instead of using existing tag admission");
    editor.text(2, std::string(9000, 'a'));
    frame();
    input.click("ui-source:element:2");
    const auto long_text = editor.state();
    const auto disabled = test::ui_targets.at("ui-source:text-set");
    check(!disabled.enabled, "Long leaf text exposed a truncated native replacement");
    io.AddMousePosEvent((disabled.minimum.x + disabled.maximum.x) * .5f,
                        (disabled.minimum.y + disabled.maximum.y) * .5f);
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    check(editor.state() == long_text,
          "Native text field silently replaced a leaf exceeding its display capacity");
    editor.reload(project, true);
    editor.request_close();
    check(!editor.is_open(), "Clean source close retained a document");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need capture/scratch directory");
        const auto output = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(output);
        for (const auto width : {1180, 700, 400})
            exercise(output, width);
        std::cout << "Native UI source controls/history/storage at1180/700/400 passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
