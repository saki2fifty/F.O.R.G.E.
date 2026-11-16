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
    bool locked = false, save_locked = false;
    const auto frame = [&] {
        test::ui_targets.clear();
        capture.new_frame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        if (auto* window = ImGui::FindWindowByName("###C++ Sources"))
            ImGui::SetWindowSize(window, {float(width), 850}, ImGuiCond_Always);
        editor.draw(document, context, locked, save_locked);
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
        // Popup actions run after the assistance panel; discard its previous-frame probes.
        frame();
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
    frame();
    std::string diagnostic_cursor;
    for (const auto& [key, target] : test::ui_targets) {
        (void)target;
        if (key.starts_with("cpp:cursor:"))
            diagnostic_cursor = key;
    }
    check(!diagnostic_cursor.empty(), "Diagnostic use did not expose its native cursor");
    input.click("cpp:analysis:diagnostic:next");
    input.click("cpp:analysis:diagnostic:previous");
    frame();
    check(test::ui_targets.contains(diagnostic_cursor) && editor.active_text() == call_draft,
          "Native next/previous diagnostic controls lost the source location or changed text");
    locked = true;
    frame();
    check(!test::ui_targets.at("cpp:analysis:diagnostic:next").enabled &&
              !test::ui_targets.at("cpp:analysis:diagnostic:previous").enabled,
          "Native diagnostic navigation bypassed the source lock");
    locked = false;
    frame();
    reveal_source();
    if (!captures.empty())
        capture.save(captures / ("cpp-diagnostics-" + std::to_string(width) + ".png"));
    editor.undo(false);
    frame();
    check(!test::ui_targets.at("cpp:analysis:diagnostic:next").enabled &&
              !test::ui_targets.at("cpp:analysis:diagnostic:previous").enabled,
          "Native stale diagnostic navigation remained enabled");
    check(editor.active_text() == completed, "Diagnostic navigation added a source Undo record");
    const auto symbol_draft =
        completed + "\nnamespace SymbolProbe { struct Pivot { float outline_member; }; }\n"
                    "void symbol_use() { SymbolProbe::Pivot p; (void)p.outline_member; }\n"
                    "void navigation_body();\nvoid navigation_body() {}\n"
                    "struct NavigationBase { virtual void tick() = 0; };\n"
                    "struct NavigationDerived : NavigationBase { void tick() override {} };\n";
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
    for (const auto* kind : {"references", "typeDefinition", "definition", "implementation"}) {
        const std::string_view selected(kind);
        const auto query_offset =
            selected == "references"       ? symbol_draft.rfind("p.outline_member") + 2
            : selected == "typeDefinition" ? symbol_draft.rfind("p.outline_member")
            : selected == "definition"     ? symbol_draft.find("navigation_body")
                                           : symbol_draft.find("virtual void tick") + 13;
        const auto query_position = ui::cpp_source_coordinate(symbol_draft, query_offset);
        editor.open(document, "Native/gameplay.cpp", query_position.first + 1,
                    query_position.second + 1);
        frame();
        input.click((std::string("cpp:analysis:") + kind).c_str());
        wait_result();
        check(editor.active_text() == symbol_draft,
              "Native symbol navigation query edited its draft");
        input.select("cpp:analysis:use:0");
        frame();
        const auto target_offset =
            selected == "references"       ? symbol_draft.rfind("p.outline_member") + 2
            : selected == "typeDefinition" ? symbol_draft.find("struct Pivot") + 7
            : selected == "definition"     ? symbol_draft.rfind("navigation_body")
                                           : symbol_draft.find("tick() override");
        const auto target = ui::cpp_source_coordinate(symbol_draft, target_offset);
        check(test::ui_targets.contains(cursor_key(target)) && editor.active_text() == symbol_draft,
              "Native symbol navigation chose the wrong source position");
        reveal_source();
        if (!captures.empty())
            capture.save(captures /
                         (std::string("cpp-") + kind + "-" + std::to_string(width) + ".png"));
    }
    editor.open(document, "Native/gameplay.cpp", symbol_position.first + 1,
                symbol_position.second + 1);
    frame();
    input.click("cpp:analysis:selection");
    wait_result();
    check(editor.active_text() == symbol_draft && !test::ui_targets.contains("cpp:selection"),
          "Syntax range query edited source or changed the selection");
    input.select("cpp:analysis:use:0");
    frame();
    check(editor.active_text() == symbol_draft && test::ui_targets.contains("cpp:selection") &&
              ui::read_cpp_source(root / "Native/gameplay.cpp") == saved &&
              scene.document() == authored,
          "Syntax range use did not select actual source or changed saved/Scene bytes");
    reveal_source();
    if (!captures.empty())
        capture.save(captures / ("cpp-selection-" + std::to_string(width) + ".png"));
    editor.undo(false);
    check(editor.active_text() == completed, "Syntax selection added source Undo");
    editor.undo(true);
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
    const auto format_path = root / "Native/FormatProbe.hpp";
    const std::string format_disk =
        "#pragma once\r\n// café: formatting proposal\r\n"
        "struct FormatProbe{float speed=90.0f;void tick(){speed+=1;}};\r\n";
    if (!std::filesystem::exists(format_path))
        atomic_write(format_path, format_disk);
    check(ui::read_cpp_source(format_path) == format_disk,
          "Formatting fixture would overwrite an unrelated source file");
    editor.open(document, "Native/FormatProbe.hpp");
    const auto format_before = editor.active_text();
    const auto format_cursor =
        ui::cpp_source_coordinate(format_before, format_before.rfind("speed"));
    editor.open(document, "Native/FormatProbe.hpp", format_cursor.first + 1,
                format_cursor.second + 1);
    frame();
    input.click("cpp:analysis:format");
    wait_result();
    check(editor.active_text() == format_before && !editor.can_undo(),
          "Native formatting query changed the source or its Undo");
    reveal_source();
    if (!captures.empty())
        capture.save(captures / ("cpp-format-" + std::to_string(width) + ".png"));
    input.select("cpp:analysis:use:0");
    frame();
    const auto format_after = editor.active_text();
    const auto format_target = ui::cpp_source_coordinate(format_after, format_after.rfind("speed"));
    check(format_after != format_before && format_after.find('\r') == format_after.npos &&
              test::ui_targets.contains(cursor_key(format_target)) &&
              ui::read_cpp_source(format_path) == format_disk,
          "Native format did not follow its current caret/LF/disk contract");
    editor.undo(false);
    check(editor.active_text() == format_before && !editor.can_undo(),
          "Native format did not share exactly one source Undo record");
    editor.undo(true);
    check(editor.active_text() == format_after, "Native formatting Redo lost its proposal");
    editor.undo(false);
    input.type("cpp:find:query", "void tick(){speed+=1;}", false);
    input.click("cpp:find:next");
    frame();
    check(test::ui_targets.contains("cpp:selection"),
          "Native Find did not create the actual selection for range formatting");
    input.click("cpp:analysis:formatSelection");
    wait_result();
    check(editor.active_text() == format_before && !editor.can_undo(),
          "Native selected-format query changed its draft/history before review");
    reveal_source();
    if (!captures.empty())
        capture.save(captures / ("cpp-format-selection-" + std::to_string(width) + ".png"));
    input.select("cpp:analysis:use:0");
    frame();
    const auto range_after = editor.active_text();
    check(range_after != format_before && ui::read_cpp_source(format_path) == format_disk,
          "Native selected format lost its reviewed source/disk boundary");
    editor.undo(false);
    check(editor.active_text() == format_before && !editor.can_undo(),
          "Native selected format required more than one widget Undo");
    editor.undo(true);
    check(editor.active_text() == range_after, "Native selected-format Redo lost the candidate");
    editor.undo(false);
    editor.open(document, "Native/FormatProbe.hpp", 1, 1);
    frame();
    check(test::ui_targets.contains("cpp:analysis:formatSelection") &&
              !test::ui_targets.at("cpp:analysis:formatSelection").enabled,
          "Native selection formatting stayed enabled without an active selection");
    const std::string rename_saved = "#pragma once\n// café\ninline float eval(float rename_rate) "
                                     "{ return rename_rate + rename_rate; }";
    const auto rename_path = root / "Native/RenameProbe.hpp";
    if (!std::filesystem::exists(rename_path))
        atomic_write(rename_path, rename_saved);
    check(ui::read_cpp_source(rename_path) == rename_saved,
          "Rename fixture would overwrite an existing header");
    editor.open(document, "Native/RenameProbe.hpp");
    const auto rename_before = editor.active_text();
    const std::string rename_after = "#pragma once\n// café\ninline float eval(float "
                                     "angular_speed) { return angular_speed + angular_speed; }\n";
    const auto rename_position =
        ui::cpp_source_coordinate(rename_before, rename_before.find("rename_rate") + 2);
    editor.open(document, "Native/RenameProbe.hpp", rename_position.first + 1,
                rename_position.second + 1);
    frame();
    input.click("cpp:analysis:rename");
    input.type("cpp:rename:name", "angular_speed", false);
    const auto require_rename_popup = [&](const char* stage) {
        if (!test::ui_targets.contains("cpp:rename:preview")) {
            if (!captures.empty())
                capture.save(captures / ("cpp-rename-missing-" + std::to_string(width) + ".png"));
            std::cerr << "Rename popup missing " << stage
                      << "; active source: " << editor.active_source() << '\n';
            for (const auto& [key, target] : test::ui_targets)
                if (key.starts_with("cpp:rename:") || key == "cpp:analysis:cancel")
                    std::cerr << key << '\n';
            throw std::runtime_error("Rename popup context was lost");
        }
    };
    require_rename_popup("after typing");
    editor.replace_text(rename_before + "// Changed while the rename dialog is open\n");
    frame();
    require_rename_popup("after source change");
    check(test::ui_targets.contains("cpp:rename:stale") &&
              !test::ui_targets.at("cpp:rename:preview").enabled,
          "Rename popup followed changed source instead of refusing its stale target");
    input.click("cpp:rename:cancel");
    editor.undo(false);
    check(editor.active_text() == rename_before, "Cancelling stale rename lost source history");
    editor.open(document, "Native/RenameProbe.hpp", rename_position.first + 1,
                rename_position.second + 1);
    frame();
    input.click("cpp:analysis:rename");
    input.type("cpp:rename:name", "angular_speed", false);
    frame();
    if (!captures.empty())
        capture.save(captures / ("cpp-rename-dialog-" + std::to_string(width) + ".png"));
    input.click("cpp:rename:preview");
    wait_result();
    check(editor.active_text() == rename_before && ui::read_cpp_source(rename_path) == rename_saved,
          "Native rename preview changed source or saved bytes");
    reveal_source();
    if (!captures.empty())
        capture.save(captures / ("cpp-rename-" + std::to_string(width) + ".png"));
    const auto moved_caret =
        ui::cpp_source_coordinate(rename_before, rename_before.rfind("rename_rate") + 2);
    editor.open(document, "Native/RenameProbe.hpp", moved_caret.first + 1, moved_caret.second + 1);
    input.select("cpp:analysis:use:0");
    frame();
    const auto renamed_caret =
        ui::cpp_source_coordinate(rename_after, rename_after.rfind("angular_speed") +
                                                    std::string_view("angular_speed").size());
    check(editor.active_text() == rename_after &&
              test::ui_targets.contains(cursor_key(renamed_caret)),
          "Native rename lost its complete proposal or current-caret mapping");
    editor.undo(false);
    check(editor.active_text() == rename_before, "Native rename was not one source Undo");
    editor.undo(true);
    check(editor.active_text() == rename_after, "Native rename Redo lost its proposal");
    editor.undo(false);
    check(ui::read_cpp_source(rename_path) == rename_saved && scene.document() == authored,
          "Native rename changed saved source or Scene data");
    const auto fix_path = root / "Native/QuickFixProbe.hpp";
    const std::string fix_saved =
        "#pragma once\n// café\nint fixes_probe() { int value = 1 return value; }\n";
    if (!std::filesystem::exists(fix_path))
        atomic_write(fix_path, fix_saved);
    check(ui::read_cpp_source(fix_path) == fix_saved,
          "Quick-fix fixture would overwrite an unrelated source file");
    editor.open(document, "Native/QuickFixProbe.hpp");
    const auto fix_before = editor.active_text();
    const auto fix_position =
        ui::cpp_source_coordinate(fix_before, fix_before.find("1 return") + 2);
    editor.open(document, "Native/QuickFixProbe.hpp", fix_position.first + 1,
                fix_position.second + 1);
    frame();
    input.click("cpp:analysis:fixes");
    wait_result();
    check(editor.active_text() == fix_before && !editor.can_undo(),
          "Native quick-fix query changed source/history");
    reveal_source();
    if (!captures.empty())
        capture.save(captures / ("cpp-quick-fixes-" + std::to_string(width) + ".png"));
    const auto fix_cursor = ui::cpp_source_coordinate(fix_before, fix_before.rfind("value"));
    editor.open(document, "Native/QuickFixProbe.hpp", fix_cursor.first + 1, fix_cursor.second + 1);
    input.select("cpp:analysis:use:0");
    frame();
    auto fix_after = fix_before;
    fix_after.insert(fix_after.find("1 return") + 1, ";");
    const auto fix_mapped = ui::cpp_source_coordinate(fix_after, fix_after.rfind("value"));
    check(editor.active_text() == fix_after && test::ui_targets.contains(cursor_key(fix_mapped)),
          "Native quick fix lost its complete proposal/current-caret mapping");
    editor.undo(false);
    check(editor.active_text() == fix_before && !editor.can_undo(),
          "Native quick fix needed more than one source Undo");
    editor.undo(true);
    check(editor.active_text() == fix_after, "Native quick-fix Redo lost its source edit");
    editor.undo(false);
    check(ui::read_cpp_source(fix_path) == fix_saved && scene.document() == authored,
          "Native quick fix changed saved source or Scene bytes");
    editor.open(document, "Native/gameplay.cpp", declaration_position.first + 1,
                declaration_position.second + 1);
    editor.undo(false);
    check(editor.active_text() == completed, "File outline navigation added a source Undo record");
    locked = true;
    frame();
    check(!test::ui_targets.at("cpp:analysis:fixes").enabled,
          "Native quick fixes bypassed the source lock");
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
    check(!test::ui_targets.at("cpp:analysis:definition").enabled &&
              !test::ui_targets.at("cpp:analysis:implementation").enabled,
          "Native definition/implementation controls bypassed the source lock");
    check(!test::ui_targets.at("cpp:analysis:rename").enabled,
          "Native rename bypassed the source lock");
    check(!test::ui_targets.at("cpp:analysis:selection").enabled,
          "Syntax selection query bypassed the source lock");
    check(!test::ui_targets.at("cpp:analysis:format").enabled,
          "Native formatting control bypassed the source lock");
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
    input.click("cpp:assistance");
    const std::string literal_source = "// café\n\tfloat speed = 1; // speed\n";
    editor.replace_text(literal_source);
    editor.open(document, "Native/gameplay.cpp", 1, 1);
    frame();
    check(!test::ui_targets.contains("cpp:find:replacement"),
          "Literal replacement controls were not hidden initially");
    input.type("cpp:find:query", "speed", false);
    input.click("cpp:find:next");
    frame();
    check(test::ui_targets.contains("cpp:selection"), "Native literal Find did not select a match");
    const auto first_cursor = cursor_key(ui::cpp_source_coordinate(
        literal_source, literal_source.find("speed") + std::string("speed").size()));
    const auto last_cursor = cursor_key(ui::cpp_source_coordinate(
        literal_source, literal_source.rfind("speed") + std::string("speed").size()));
    check(test::ui_targets.contains(first_cursor), "Native Find selected the wrong first match");
    input.click("cpp:find:previous");
    frame();
    check(test::ui_targets.contains(last_cursor), "Native Previous did not wrap to the last match");
    input.click("cpp:find:next");
    input.click("cpp:find:expand");
    input.type("cpp:find:replacement", "rate", false);
    input.click("cpp:find:replace_next");
    check(editor.active_text() == "// café\n\tfloat rate = 1; // speed\n",
          "Native Replace next skipped its selected tab/UTF8 match");
    editor.undo(false);
    check(editor.active_text() == literal_source, "Native Replace next was not one source Undo");
    input.type("cpp:find:replacement", "vitesse_é", false);
    input.click("cpp:find:replace_all");
    check(editor.active_text() == "// café\n\tfloat vitesse_é = 1; // vitesse_é\n",
          "Native Replace all did not replace every literal match");
    editor.undo(false);
    check(editor.active_text() == literal_source, "Native Replace all was not one source Undo");
    editor.undo(true);
    frame();
    reveal_source();
    if (!captures.empty())
        capture.save(captures / ("cpp-literal-find-replace-" + std::to_string(width) + ".png"));
    locked = true;
    frame();
    check(!test::ui_targets.at("cpp:find:replace_next").enabled &&
              !test::ui_targets.at("cpp:find:replace_all").enabled,
          "Native literal replacement bypassed the source lock");
    bool readonly_rejected = false;
    try {
        editor.replace_literal(document, "vitesse_é", "rate", true);
    } catch (const std::exception&) {
        readonly_rejected = true;
    }
    check(readonly_rejected, "Literal source owner accepted a read-only widget");
    locked = false;
    save_locked = true;
    frame();
    check(!test::ui_targets.at("cpp:find:replace_next").enabled &&
              !test::ui_targets.at("cpp:find:replace_all").enabled,
          "Native literal replacement bypassed the build/file lock");
    save_locked = false;
    frame();
    editor.undo(false);
    editor.undo(false);
    editor.replace_text("gogo\n");
    frame();
    input.click("cpp:editor");
    auto& literal_io = ImGui::GetIO();
    const auto literal_key = [&](ImGuiKey key, bool ctrl = false, bool shift = false) {
        literal_io.AddKeyEvent(ImGuiMod_Ctrl, ctrl);
        literal_io.AddKeyEvent(ImGuiMod_Shift, shift);
        literal_io.AddKeyEvent(key, true);
        frame();
        literal_io.AddKeyEvent(key, false);
        literal_io.AddKeyEvent(ImGuiMod_Ctrl, false);
        literal_io.AddKeyEvent(ImGuiMod_Shift, false);
        frame();
    };
    literal_key(ImGuiKey_Home, true);
    for (int i = 0; i < 4; ++i)
        literal_key(ImGuiKey_RightArrow);
    literal_key(ImGuiKey_LeftArrow, false, true);
    literal_key(ImGuiKey_LeftArrow, false, true);
    input.type("cpp:find:query", "go", false);
    input.type("cpp:find:replacement", "rate", false);
    input.click("cpp:find:replace_next");
    check(editor.active_text() == "gorate\n",
          "Native replacement lost the actual reversed selection between adjacent equal matches");
    editor.undo(false);
    check(editor.active_text() == "gogo\n", "Reversed selected replacement was not one Undo");
    editor.undo(false);
    check(editor.active_text() == symbol_draft &&
              ui::read_cpp_source(root / "Native/gameplay.cpp") == saved &&
              scene.document() == authored,
          "Native literal editing changed preceding source history, saved source or Scene");
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
