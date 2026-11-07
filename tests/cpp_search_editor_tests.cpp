#define FORGE_UI_FIXTURE 1
#include "cpp_source_editor.hpp"
#include "imgui_input_driver.hpp"
#include "imgui_software_capture.hpp"
#include <iostream>
#include <thread>

namespace {
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void exercise(const std::filesystem::path& scratch, const std::filesystem::path& captures,
              float scale, bool compact) {
    using namespace forge;
    const auto root = std::filesystem::absolute(scratch) / AssetId::generate().str();
    std::filesystem::create_directories(root.parent_path());
    SceneDocument::create_project(root, "SpinCubeTest");
    const std::string component =
        "#pragma once\n\nstruct Rotator {\n    float speed = 90.0f;\n};\n";
    atomic_write(root / "Native/Components/Rotator.hpp", component);
    atomic_write(root / "Native/Systems/RotationSystem.cpp",
                 "// Read the component speed on each fixed tick.\r\n"
                 "// This source is a search/navigation demonstration.\r\n");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument document(scene);
    document.open_project(root);
    const auto authored = scene.document();
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    const int width = int((compact ? 700 : 1180) * scale), height = int(850 * scale);
    test::ImGuiSoftwareCapture capture(width, height);
    ui::style(scale);
    ImGui::GetIO().ConfigInputTrickleEventQueue = false;
    test::observe_ui = true;
    ui::CppSourceEditor editor;
    editor.open(document, "Native/Components/Rotator.hpp");
    const auto original_widget_text = editor.active_text();
    const std::string draft = "#pragma once\n\nstruct Rotator {\n    float speed = 120.0f;\n};\n";
    editor.replace_text(draft);
    bool locked = false;
    const auto frame = [&] {
        test::ui_targets.clear();
        capture.new_frame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        if (auto* window = ImGui::FindWindowByName("###C++ Sources"))
            ImGui::SetWindowSize(window, {float(width), float(height)}, ImGuiCond_Always);
        editor.draw(document, context, locked);
        capture.render();
    };
    test::ImGuiInputDriver input(frame);
    frame();
    frame();
    check(editor.owns_keyboard(), "C++ document did not own keyboard focus");
    auto& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiMod_Shift, true);
    io.AddKeyEvent(ImGuiKey_F, true);
    frame();
    io.AddKeyEvent(ImGuiKey_F, false);
    io.AddKeyEvent(ImGuiMod_Shift, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    frame();
    check(test::ui_targets.contains("cpp:project-query"),
          "Ctrl+Shift+F did not open project search in the owning document");
    input.click("cpp:project-word");
    input.type("cpp:project-query", "speed"); // Enter starts the actual task.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!test::ui_targets.contains("cpp:project-open:1")) {
        check(std::chrono::steady_clock::now() < deadline,
              "Project search controls did not return results");
        frame();
        std::this_thread::yield();
    }
    check(editor.dirty() &&
              ui::read_cpp_source(root / "Native/Components/Rotator.hpp") == component,
          "Interactive search saved the dirty source");
    input.click("cpp:project-open:1");
    frame(); // The existing source tab bar adopts its queued selected tab next frame.
    check(editor.active_source() == "Native/Systems/RotationSystem.cpp" && editor.dirty(),
          "Interactive result did not open the other source or lost a draft");
    frame();
    if (!captures.empty()) {
        std::filesystem::create_directories(captures);
        capture.save(captures / ("cpp-project-search-" + std::to_string(int(scale * 100)) +
                                 (compact ? "-compact.png" : ".png")));
    }
    // An already-open clean tab must check changed disk bytes before its normal
    // automatic reload; its old widget digest alone is insufficient.
    atomic_write(root / "Native/Systems/RotationSystem.cpp", "// external edit\n");
    bool stale = false;
    try {
        editor.open_search_result(document, 1);
    } catch (const ui::CppSourceChanged&) {
        stale = true;
    }
    check(stale && editor.active_text().find("component speed") != std::string::npos,
          "Stale result silently reloaded an externally changed open tab");
    locked = true;
    frame();
    check(!test::ui_targets.at("cpp:project-open:0").enabled,
          "Search result navigation bypassed the existing source lock");
    locked = false;
    editor.open(document, "Native/Components/Rotator.hpp");
    check(editor.active_text() == draft && editor.can_undo(),
          "Result navigation lost source history");
    editor.undo(false);
    check(editor.active_text() == original_widget_text && scene.document() == authored,
          "Project search created a source Undo or altered authored Scene state");
    // A read-only client may start another search while the user holds a result
    // button. Its release must not activate a different job's replacement row.
    atomic_write(root / "Native/SearchTarget.cpp", "// new_target\n");
    editor.search_project(document, {"speed"}, false);
    const auto wait_results = [&] {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        do {
            frame();
            check(std::chrono::steady_clock::now() < end, "Replacement search did not settle");
            std::this_thread::yield();
        } while (!test::ui_targets.contains("cpp:project-open:0"));
    };
    wait_results();
    const auto held = test::ui_targets.at("cpp:project-open:0");
    io.AddMousePosEvent((held.minimum.x + held.maximum.x) / 2,
                        (held.minimum.y + held.maximum.y) / 2);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    editor.search_project(document, {"new_target"}, false);
    wait_results();
    io.AddMouseButtonEvent(0, false);
    frame();
    frame();
    check(editor.active_source() == "Native/Components/Rotator.hpp",
          "A held result button activated a different search job's row");
    // Retire an outstanding snapshot across a project switch, then destroy its
    // owner. The bounded worker must not borrow tabs or publish into the new root.
    editor.search_project(document, {"speed"});
    editor.project_changed(root / "DifferentProject");
    check(editor.active_source().empty() && !editor.is_open(),
          "Project switch retained source navigation");
    test::observe_ui = false;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2 || argc == 3, "Need scratch and optional captures directory");
        for (float scale : {1.f, 1.5f, 2.f})
            for (bool compact : {false, true})
                exercise(argv[1], argc == 3 ? argv[2] : "", scale, compact);
        std::cout
            << "Actual project-search controls, draft preservation, navigation, stale disk, locks "
               "and project retirement passed six layouts\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
