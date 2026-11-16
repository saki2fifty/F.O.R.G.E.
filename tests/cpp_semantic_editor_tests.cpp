#define FORGE_UI_FIXTURE 1
#include "cpp_source_editor.hpp"
#include "imgui_input_driver.hpp"
#include "imgui_software_capture.hpp"
#include <iostream>
#include <thread>

namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void exercise(const std::filesystem::path& base, const std::string& provider,
              const std::filesystem::path& captures, int width,
              const std::filesystem::path& existing_context) {
    using namespace forge;
    const bool existing = !existing_context.empty();
    const auto root = existing ? std::filesystem::absolute(base)
                               : std::filesystem::absolute(base) / AssetId::generate().str();
    if (!existing) {
        std::filesystem::create_directories(root.parent_path());
        SceneDocument::create_project(root, "C++ completion");
        atomic_write(root / "Native/gameplay.cpp", "// Saved project source\n");
        atomic_write(root / "Native/forge.registration.hpp", "#pragma once\n");
        atomic_write(root / ".forge/analysis-fixture/compile_commands.json", "[]");
    }
    const auto compiler_context = existing ? existing_context : root / ".forge/analysis-fixture";
    const auto saved = ui::read_cpp_source(root / "Native/gameplay.cpp");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument document(scene);
    document.open_project(root);
    const auto authored = scene.document();
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    test::ImGuiSoftwareCapture capture(width, 850);
    ui::style(1);
    ImGui::GetIO().ConfigInputTrickleEventQueue = false;
    test::observe_ui = true;
    ui::CppSourceEditor editor;
    editor.semantic_tool = [&] { return provider; };
    editor.semantic_generation = [] { return std::string_view("gui-fixture-context"); };
    editor.semantic_context = [&] {
        return CppSemanticContext{root, compiler_context, "gui-fixture-context", nullptr};
    };
    editor.open(document, "Native/gameplay.cpp");
    const auto draft = saved +
                       "\n// café current draft\nstruct Spinner { float degrees_per_second; };\n"
                       "void semantic_tick() { Spinner s; s.de }\n";
    editor.replace_text(draft);
    const auto offset = draft.rfind("s.de") + 4;
    const auto position = ui::cpp_source_coordinate(draft, offset);
    editor.open(document, "Native/gameplay.cpp", position.first + 1, position.second + 1);
    bool locked = false;
    const auto frame = [&] {
        test::ui_targets.clear();
        capture.new_frame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        if (auto* window = ImGui::FindWindowByName("###C++ Sources"))
            ImGui::SetWindowSize(window, {float(width), 850}, ImGuiCond_Always);
        editor.draw(document, context, locked);
        capture.render();
    };
    test::ImGuiInputDriver input(frame);
    frame();
    input.click("cpp:assistance");
    // Position after the first-use window and expanded panel have their real sizes.
    editor.open(document, "Native/gameplay.cpp", position.first + 1, position.second + 1);
    frame();
    input.click("cpp:analysis:completion");
    const auto wait_result = [&] {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(40);
        while (!test::ui_targets.contains("cpp:analysis:use:0")) {
            check(std::chrono::steady_clock::now() < end,
                  "Native analysis controls did not receive a result");
            frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    wait_result();
    const auto reveal_source = [&] {
        const auto source_area = test::ui_targets.at("cpp:editor");
        ImGui::GetIO().AddMousePosEvent((source_area.minimum.x + source_area.maximum.x) / 2,
                                        (source_area.minimum.y + source_area.maximum.y) / 2);
        frame();
        ImGui::GetIO().AddMouseWheelEvent(0, -30);
        frame();
        frame();
        ImGui::GetIO().AddMousePosEvent(float(width - 40), 12);
        frame();
        frame();
    };
    reveal_source();
    if (!captures.empty()) {
        std::filesystem::create_directories(captures);
        capture.save(captures / ("cpp-completion-" + std::to_string(width) + ".png"));
    }
    input.select("cpp:analysis:use:0");
    auto completed = draft;
    completed.replace(draft.rfind("s.de") + 2, 2, "degrees_per_second");
    check(editor.active_text() == completed, "Native completion inserted the wrong range");
    editor.undo(false);
    check(editor.active_text() == draft, "Native completion did not keep one Undo record");
    editor.undo(true);
    editor.open(document, "Native/gameplay.cpp", position.first + 1, position.second + 1);
    input.click("cpp:analysis:declaration");
    // The new query removes the previous result before its first response.
    frame();
    wait_result();
    input.select("cpp:analysis:use:0");
    check(editor.active_text() == completed && scene.document() == authored &&
              ui::read_cpp_source(root / "Native/gameplay.cpp") == saved,
          "Native declaration changed saved source, draft or Scene");
    locked = true;
    frame();
    check(!test::ui_targets.at("cpp:analysis:completion").enabled &&
              !test::ui_targets.at("cpp:analysis:use:0").enabled,
          "Native code assistance bypassed the source lock");
    locked = false;
    frame();
    const auto call_draft =
        completed + "\n// Adjust the rotation rate.\nvoid throttle(float speed, float rate);\n"
                    "void call_signature() { throttle(1, ); }\n";
    editor.replace_text(call_draft);
    const auto call_position = ui::cpp_source_coordinate(call_draft, call_draft.rfind("1, ") + 3);
    editor.open(document, "Native/gameplay.cpp", call_position.first + 1, call_position.second + 1);
    frame();
    input.click("cpp:analysis:signature");
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(40);
    while (!test::ui_targets.contains("cpp:analysis:signature:0")) {
        check(std::chrono::steady_clock::now() < end, "Native call signature did not arrive");
        frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(!test::ui_targets.contains("cpp:analysis:use:0") && editor.active_text() == call_draft &&
              ui::read_cpp_source(root / "Native/gameplay.cpp") == saved &&
              scene.document() == authored,
          "Native signature became applicable or changed source or Scene");
    reveal_source();
    if (!captures.empty())
        capture.save(captures / ("cpp-signature-" + std::to_string(width) + ".png"));
    editor.undo(false);
    check(editor.active_text() == completed, "Read-only signature added a source Undo record");
    editor.undo(true);
    frame();
    input.click("cpp:analysis:diagnostics");
    wait_result();
    check(editor.active_text() == call_draft &&
              ui::read_cpp_source(root / "Native/gameplay.cpp") == saved &&
              scene.document() == authored,
          "Diagnostic query changed the source or Scene");
    input.select("cpp:analysis:use:0");
    reveal_source();
    if (!captures.empty())
        capture.save(captures / ("cpp-diagnostics-" + std::to_string(width) + ".png"));
    editor.undo(false);
    check(editor.active_text() == completed, "Diagnostic navigation added a source Undo record");
    const auto symbol_draft =
        completed + "\nnamespace SymbolProbe { struct Pivot { float outline_member; }; }\n"
                    "void symbol_use() { SymbolProbe::Pivot p; (void)p.outline_member; }\n";
    editor.replace_text(symbol_draft);
    const auto symbol_position =
        ui::cpp_source_coordinate(symbol_draft, symbol_draft.rfind("p.outline_member") + 2);
    editor.open(document, "Native/gameplay.cpp", symbol_position.first + 1,
                symbol_position.second + 1);
    frame();
    input.click("cpp:analysis:hover");
    const auto info_end = std::chrono::steady_clock::now() + std::chrono::seconds(40);
    while (!test::ui_targets.contains("cpp:analysis:info:0")) {
        check(std::chrono::steady_clock::now() < info_end,
              "Native symbol information did not arrive");
        frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(editor.active_text() == symbol_draft && !test::ui_targets.contains("cpp:analysis:use:0"),
          "Native symbol information became a source edit");
    reveal_source();
    if (!captures.empty())
        capture.save(captures / ("cpp-symbol-info-" + std::to_string(width) + ".png"));
    editor.undo(false);
    check(editor.active_text() == completed, "Symbol information added a source Undo record");
    editor.undo(true);
    editor.open(document, "Native/gameplay.cpp", symbol_position.first + 1,
                symbol_position.second + 1);
    frame();
    input.click("cpp:analysis:symbols");
    const auto symbols_end = std::chrono::steady_clock::now() + std::chrono::seconds(40);
    while (!test::ui_targets.contains("cpp:analysis:symbol:outline_member")) {
        check(std::chrono::steady_clock::now() < symbols_end, "Native file outline did not arrive");
        frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    input.select("cpp:analysis:symbol:outline_member");
    const auto declaration_position =
        ui::cpp_source_coordinate(symbol_draft, symbol_draft.find("float outline_member") + 6);
    const auto cursor_key = [](const auto& position) {
        return "cpp:cursor:" + std::to_string(position.first + 1) + ":" +
               std::to_string(position.second + 1);
    };
    input.click("cpp:navigation:back");
    frame();
    check(test::ui_targets.contains(cursor_key(symbol_position)),
          "Native Back did not restore the source use location");
    input.click("cpp:navigation:forward");
    frame();
    check(test::ui_targets.contains(cursor_key(declaration_position)),
          "Native Forward did not restore the declaration location");
    reveal_source();
    if (!captures.empty())
        capture.save(captures / ("cpp-file-symbols-" + std::to_string(width) + ".png"));
    for (const auto* kind : {"references", "typeDefinition"}) {
        const bool references = std::string_view(kind) == "references";
        const auto query_position =
            references
                ? symbol_position
                : ui::cpp_source_coordinate(symbol_draft, symbol_draft.rfind("p.outline_member"));
        editor.open(document, "Native/gameplay.cpp", query_position.first + 1,
                    query_position.second + 1);
        frame();
        input.click((std::string("cpp:analysis:") + kind).c_str());
        wait_result();
        check(editor.active_text() == symbol_draft, "Native reference/type query edited its draft");
        input.select("cpp:analysis:use:0");
        frame();
        const auto target =
            references
                ? symbol_position
                : ui::cpp_source_coordinate(symbol_draft, symbol_draft.find("struct Pivot") + 7);
        check(test::ui_targets.contains(cursor_key(target)) && editor.active_text() == symbol_draft,
              "Native reference/type action chose the wrong source position");
        reveal_source();
        if (!captures.empty())
            capture.save(captures /
                         (std::string("cpp-") + kind + "-" + std::to_string(width) + ".png"));
    }
    editor.open(document, "Native/gameplay.cpp", declaration_position.first + 1,
                declaration_position.second + 1);
    editor.open(document, "Native/forge.registration.hpp");
    editor.open(document, "Native/gameplay.cpp", declaration_position.first + 1,
                declaration_position.second + 1);
    frame();
    input.click("cpp:navigation:back");
    frame();
    check(editor.active_source() == "Native/forge.registration.hpp" &&
              test::ui_targets.contains("cpp:analysis:scope:Native/gameplay.cpp"),
          "Rapid tab opens polluted Back history or hid the semantic result's file scope");
    input.click("cpp:navigation:forward");
    frame();
    check(editor.active_source() == "Native/gameplay.cpp" &&
              test::ui_targets.contains(cursor_key(declaration_position)),
          "Forward returned to a transient fallback tab");
    input.click("cpp:navigation:clear");
    frame();
    check(!test::ui_targets.at("cpp:navigation:back").enabled &&
              !test::ui_targets.at("cpp:navigation:forward").enabled,
          "Clear navigation retained reachable Back/Forward locations");
    check(editor.active_text() == symbol_draft &&
              ui::read_cpp_source(root / "Native/gameplay.cpp") == saved &&
              scene.document() == authored,
          "Native file outline changed source or Scene");
    editor.undo(false);
    check(editor.active_text() == completed, "File outline navigation added a source Undo record");
    locked = true;
    frame();
    check(!test::ui_targets.at("cpp:analysis:signature").enabled,
          "Native signature query bypassed the source lock");
    check(!test::ui_targets.at("cpp:analysis:diagnostics").enabled,
          "Native diagnostic query bypassed the source lock");
    check(!test::ui_targets.at("cpp:analysis:hover").enabled &&
              !test::ui_targets.at("cpp:analysis:symbols").enabled,
          "Native symbol controls bypassed the source lock");
    check(!test::ui_targets.at("cpp:navigation:back").enabled &&
              !test::ui_targets.at("cpp:navigation:clear").enabled,
          "Native navigation bypassed the source lock");
    check(!test::ui_targets.at("cpp:analysis:references").enabled &&
              !test::ui_targets.at("cpp:analysis:typeDefinition").enabled,
          "Native reference/type controls bypassed the source lock");
    locked = false;
    frame();
    editor.undo(true);
    editor.open(document, "Native/gameplay.cpp", symbol_position.first + 1,
                symbol_position.second + 1);
    editor.search_project(document, {"outline_member", true, false});
    const auto search_end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!test::ui_targets.contains("cpp:project-open:0")) {
        check(std::chrono::steady_clock::now() < search_end, "Native source search did not finish");
        frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    editor.open_search_result(document, 0);
    frame();
    check(test::ui_targets.contains("cpp:selection"),
          "Search match did not select its source text");
    editor.open(document, "Native/gameplay.cpp", symbol_position.first + 1,
                symbol_position.second + 1);
    frame();
    check(!test::ui_targets.contains("cpp:selection"),
          "Explicit source jump retained an old selection");
    editor.open_search_result(document, 0);
    frame();
    check(test::ui_targets.contains("cpp:selection"), "Repeated search lost its source selection");
    input.click("cpp:navigation:back");
    frame();
    check(!test::ui_targets.contains("cpp:selection") && editor.active_text() == symbol_draft &&
              test::ui_targets.contains(cursor_key(symbol_position)),
          "Back retained an old search selection or changed source");
    editor.project_changed(root / "DifferentProject");
    test::observe_ui = false;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4 || argc == 5,
              "Need project/scratch, provider, captures and optional existing compiler context");
        const auto existing_context =
            argc == 5 ? std::filesystem::u8path(argv[4]) : std::filesystem::path{};
        for (const int width : {1180, 700, 400})
            exercise(std::filesystem::u8path(argv[1]), argv[2], std::filesystem::u8path(argv[3]),
                     width, existing_context);
        std::cout
            << "Native completion/declaration/signature controls, one-step source Undo, locks and "
               "project retirement passed at three widths\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
