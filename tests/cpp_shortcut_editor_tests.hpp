#pragma once
#include "cpp_source_editor.hpp"
#include "shortcuts.hpp"
#include <imgui_internal.h>
void require(bool condition, const char* message);
inline void test_cpp_shortcut_editor(const std::filesystem::path& scratch) {
    using namespace forge;
    using namespace forge::ui;
    const auto root = scratch / "shortcut-project";
    SceneDocument::create_project(root, "C++ shortcut consumer");
    atomic_write(root / "Native/Test.cpp", "// marker\nint value = 1;\n");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument document(scene);
    document.open_project(root);
    const auto authored = scene.document();
    ImGui::CreateContext();
    style(1);
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1200, 850};
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    EditorUiContext context;
    ContextScope context_scope(context);
    CppSourceEditor editor;
    editor.open(document, "Native/Test.cpp");
    const auto initial_source_revision = editor.source_revision;
    EditorActions actions;
    int global_saves = 0;
    actions.entries.push_back({"save",
                               "Save source",
                               "",
                               "Save the active source",
                               true,
                               [&] { ++global_saves; },
                               {},
                               true,
                               {ImGuiMod_Ctrl | ImGuiKey_S, 0}});
    EditorShortcuts shortcuts;
    require(
        shortcuts.assign(actions, "save", {ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_K, 0}, [] {}),
        "Could not configure the C++ Save binding");
    bool locked = false, save_locked = false;
    test::observe_ui = true;
    const auto frame = [&] {
        test::ui_targets.clear();
        ImGui::NewFrame();
        const auto keys = shortcuts.unique_keys(actions, "save");
        editor.save_shortcut = [keys] {
            return std::any_of(keys.begin(), keys.end(), EditorShortcuts::pressed);
        };
        // Same existing task ownership boundary as main: text keys stay in code.
        shortcuts.dispatch(actions, !editor.owns_keyboard());
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
        editor.draw(document, context, locked, save_locked);
        ImGui::Render();
    };
    const auto key = [&](ImGuiKey value, bool alt = false) {
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiMod_Alt, alt);
        io.AddKeyEvent(value, true);
        frame();
        io.AddKeyEvent(value, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        io.AddKeyEvent(ImGuiMod_Alt, false);
        frame();
    };
    frame();
    frame();
    const auto box = test::ui_targets.at("cpp:editor");
    const ImVec2 point{box.minimum.x + 70, box.minimum.y + 10};
    io.AddMousePosEvent(point.x, point.y);
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    io.AddInputCharactersUTF8(" edited ");
    frame();
    require(editor.dirty(), "Actual C++ typing did not create a draft");
    const auto changed = editor.active_text();
    const auto disk = read_cpp_source(root / "Native/Test.cpp");
    key(ImGuiKey_S);
    require(editor.dirty() && read_cpp_source(root / "Native/Test.cpp") == disk,
            "Old C++ Save binding remained active");
    key(ImGuiKey_K, true);
    require(!editor.dirty() && read_cpp_source(root / "Native/Test.cpp") == changed &&
                editor.source_revision == initial_source_revision + 1 && global_saves == 0,
            "Configured C++ Save failed, edited text, or dispatched twice");
    io.AddInputCharactersUTF8(" another edit ");
    frame();
    require(editor.dirty(), "Second C++ edit did not create a draft");
    save_locked = true;
    key(ImGuiKey_K, true);
    require(editor.dirty() && editor.source_revision == initial_source_revision + 1 &&
                read_cpp_source(root / "Native/Test.cpp") == changed,
            "Configured Save bypassed C++ build/file locking");
    save_locked = false;
    atomic_write(root / "Native/Test.cpp", "// changed externally\n");
    key(ImGuiKey_K, true);
    require(editor.dirty() && editor.source_revision == initial_source_revision + 1 &&
                read_cpp_source(root / "Native/Test.cpp") == "// changed externally\n" &&
                scene.document() == authored,
            "Configured Save overwrote external code or changed authored Scene state");
    test::observe_ui = false;
    ImGui::DestroyContext();
}
