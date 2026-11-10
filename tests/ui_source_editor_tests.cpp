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
enum class SourceControls {
    All,
    TextProperties,
    Typography,
    ImageProperties,
    ResponsiveLayout,
    BoxStyling,
    CreationSetup
};
void exercise(const std::filesystem::path& output, int width,
              SourceControls mode = SourceControls::All) {
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
    std::shared_ptr<const AssetCatalog> resource_catalog;
    editor.resource_catalog = [&] { return resource_catalog; };
    bool show_content = false;
    AssetId dragged_image;
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
        if (show_content) {
            ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({200, 90}, ImGuiCond_Always);
            ImGui::Begin("Content image fixture");
            ImGui::Button("Project image source");
            FORGE_UI_PROBE("test:image-source");
            if (ImGui::BeginDragDropSource()) {
                const auto payload = dragged_image.str();
                ImGui::SetDragDropPayload("FORGE_ASSET", payload.c_str(), payload.size() + 1);
                ImGui::TextUnformatted("Project image source");
                ImGui::EndDragDropSource();
            }
            ImGui::End();
        }
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
    if (mode == SourceControls::CreationSetup) {
        input.click("ui-source:elements-tab");
        input.click("ui-source:element:1");
        input.click("ui-source:child-kind");
        input.select("ui-source:child-choice:button");
        input.type("ui-source:child-text", "Run & test");
        input.click("ui-source:child-pixel-layout");
        frame();
        check(!test::ui_targets.at("ui-source:child-add").enabled,
              "Pixel creation button enabled without a unique source ID");
        input.type("ui-source:child-id", "play-button");
        const std::array values{"24", "36", "160", "40"};
        for (std::size_t i = 0; i < values.size(); ++i)
            input.type(("ui-source:child-pixels:" + std::to_string(i)).c_str(), values[i]);
        const auto staged = editor.state();
        frame();
        capture.save(output / ("ui-source-create-pixels-" + std::to_string(width) + ".png"));
        input.click("ui-source:child-add");
        const auto created = editor.state();
        check(created.at("selection") == Json::array({3}) &&
                  read_ui_markup_layout(created.at("text").get<std::string>(), 3) ==
                      UiPixelLayout{24, 36, 160, 40} &&
                  created.at("text").get<std::string>().find("play-button") != std::string::npos &&
                  asset_storage::read(file) == source,
              "Native configured creation lost its selected root, geometry or explicit Save");
        frame();
        capture.save(output / ("ui-source-created-control-" + std::to_string(width) + ".png"));
        input.click("button:Undo");
        check(editor.state().at("text") == staged.at("text"),
              "Configured control creation needed multiple Undo steps");
        input.click("button:Redo");
        check(editor.state().at("text") == created.at("text"), "Configured control Redo differs");
        input.click("ui-source:element:1");
        const auto duplicate = editor.state();
        input.click("ui-source:child-add");
        check(editor.state().at("text") == duplicate.at("text") &&
                  editor.state().at("revision") == duplicate.at("revision"),
              "Duplicate creation ID partially changed the native source");
        input.type("ui-source:child-id", "second-button");
        input.type("ui-source:child-pixels:2", "0");
        input.click("ui-source:child-add");
        check(editor.state().at("text") == duplicate.at("text"),
              "Invalid creation geometry partially changed the source");
        editor.save(project);
        check(!editor.dirty() && scene.snapshot() == authored && scene.revision() == revision &&
                  asset_storage::read(root / "forge.assets.json") == catalog,
              "Configured creation Save changed Scene/catalog ownership");
        editor.request_close();
        return;
    }
    if (mode == SourceControls::ImageProperties) {
        std::string tga(21, '\0');
        tga[2] = 2;
        tga[12] = tga[14] = 1;
        tga[16] = 24;
        tga[17] = 32;
        const auto image_path = std::filesystem::u8path("Assets/Badge & 'résumé'.tga");
        asset_storage::replace(root / image_path, tga);
        const auto registered = register_ui_source(*project.writer_guard(), image_path);
        dragged_image = registered.id;
        resource_catalog = std::make_shared<AssetCatalog>(AssetCatalog::open_project(root));
        const auto image_catalog = asset_storage::read(root / "forge.assets.json");
        editor.control(1, "image", "");
        frame();
        input.click("ui-source:elements-tab");
        input.click("ui-source:element:3");
        const auto before = editor.state();
        input.click("asset-picker:texture:Project image");
        input.select(("picker-option:" + registered.id.str()).c_str());
        check(editor.state() == before && asset_storage::read(file) == source,
              "Image picker wrote source before Apply");
        frame();
        capture.save(output / ("ui-source-image-resource-" + std::to_string(width) + ".png"));
        input.click("ui-source:control-apply");
        const auto picked = editor.state();
        check(editor.control_properties(3).properties.at(0).value ==
                  UiControlPropertyValue(std::string("Badge & 'résumé'.tga")),
              "Image picker did not stage a document-relative decoded reference");
        input.click("button:Undo");
        check(editor.state().at("text") == before.at("text"),
              "Image picker needed multiple Undo steps");
        input.click("button:Redo");
        check(editor.state().at("text") == picked.at("text"),
              "Image picker Redo changed the reference");
        input.click("button:Undo");
        input.click("ui-source:element:3");
        // Bring the existing picker target into view, then drive the Content UUID payload.
        input.click("asset-picker:texture:Project image");
        io.AddKeyEvent(ImGuiKey_Escape, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Escape, false);
        show_content = true;
        frame();
        const auto start = test::ui_targets.at("test:image-source");
        const auto target = test::ui_targets.at("asset-picker:texture:Project image");
        io.AddMousePosEvent((start.minimum.x + start.maximum.x) / 2,
                            (start.minimum.y + start.maximum.y) / 2);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMousePosEvent((start.minimum.x + start.maximum.x) / 2 + 20,
                            (start.minimum.y + start.maximum.y) / 2 + 10);
        frame();
        io.AddMousePosEvent((target.minimum.x + target.maximum.x) / 2,
                            (target.minimum.y + target.maximum.y) / 2);
        frame();
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        show_content = false;
        check(editor.state().at("text") == before.at("text"),
              "Image drop wrote source before Apply");
        input.click("ui-source:control-apply");
        check(editor.state().at("text") == picked.at("text"),
              "Image drop differs from picker assignment");
        input.click("asset-picker:texture:Project image");
        input.select("asset-clear:texture:Project image");
        input.click("ui-source:control-apply");
        check(!editor.control_properties(3).properties.at(0).authored,
              "Image Clear did not remove the src override");
        input.click("button:Undo");
        input.click("button:Undo");
        input.click("ui-source:element:3");
        const auto before_properties = editor.state();
        input.type("ui-source:control-property:src", "Badge.tga");
        input.type("ui-source:control-property:rect", "0 0 2 1");
        input.type("ui-source:control-property:width", "32.5");
        input.type("ui-source:control-property:height", "16.25");
        check(editor.state() == before_properties && asset_storage::read(file) == source,
              "Image property staging changed source/history/disk");
        frame();
        capture.save(output / ("ui-source-image-properties-" + std::to_string(width) + ".png"));
        input.click("ui-source:control-apply");
        const auto applied = editor.state();
        check(applied.at("selection") == Json::array({3}) &&
                  applied.at("text") != before.at("text"),
              "Image properties did not retain their selected source root");
        input.click("ui-source:style-open");
        input.click("ui-source:style-common");
        input.select("ui-source:style-choice:image-color");
        const auto before_tint = editor.state();
        input.click("ui-source:style-new-color");
        input.type("ui-source:style-color", "80FF40FF");
        check(editor.state() == before_tint, "Image tint wrote source before Set property");
        frame();
        capture.save(output / ("ui-source-image-tint-" + std::to_string(width) + ".png"));
        input.click("ui-source:style-set");
        const auto tinted = editor.state();
        check(editor.read_style(3, "image-color") == "#80ff40" &&
                  asset_storage::read(file) == source,
              "Image tint did not share the color/source owner or saved implicitly");
        input.click("button:Undo");
        check(editor.state().at("text") == applied.at("text"),
              "Image tint needed multiple Undo steps");
        input.click("button:Undo");
        check(editor.state().at("text") == before.at("text"),
              "Image property batch needed multiple Undo steps");
        input.click("button:Redo");
        input.click("button:Redo");
        check(editor.state().at("text") == tinted.at("text"), "Image property/tint Redo differs");
        editor.save(project);
        check(!editor.dirty() && scene.snapshot() == authored && scene.revision() == revision &&
                  asset_storage::read(root / "forge.assets.json") == image_catalog,
              "Image source Save changed Scene/catalog ownership");
        editor.request_close();
        return;
    }
    if (mode == SourceControls::Typography || mode == SourceControls::ResponsiveLayout ||
        mode == SourceControls::BoxStyling) {
        const bool box = mode == SourceControls::BoxStyling;
        const bool responsive = mode == SourceControls::ResponsiveLayout;
        input.click("ui-source:elements-tab");
        input.click("ui-source:element:2");
        const auto open_style = [&] {
            frame();
            if (!test::ui_targets.contains("ui-source:style-common"))
                input.click("ui-source:style-open");
        };
        const auto choose_property = [&](const char* name) {
            open_style();
            input.click("ui-source:style-common");
            input.select(("ui-source:style-choice:" + std::string(name)).c_str());
        };
        const auto preset = [&](const char* name, const char* keyword, const char* label) {
            choose_property(name);
            const auto before = editor.state();
            const auto disk = asset_storage::read(file);
            input.click("ui-source:style-preset");
            input.select(("ui-source:style-preset:" + std::string(keyword)).c_str());
            check(editor.state() == before && asset_storage::read(file) == disk,
                  "Typography preset wrote source before Set property");
            frame();
            capture.save(output / ("ui-source-" + std::string(label) + "-" + std::to_string(width) +
                                   ".png"));
            input.click("ui-source:style-set");
            const auto after = editor.state();
            check(after.at("text") != before.at("text") && asset_storage::read(file) == disk,
                  "Typography preset did not publish or saved implicitly");
            input.click("button:Undo");
            check(editor.state().at("text") == before.at("text"),
                  "Typography preset needed multiple Undo steps");
            input.click("button:Redo");
            check(editor.state().at("text") == after.at("text"), "Typography preset Redo differs");
            // Native source Undo/Redo invalidates transient outline selections.
            if (before.at("path").get<std::string>().ends_with(".rcss")) {
                input.click(("ui-source:rule:" +
                             std::to_string(before.at("rule_selection").get<std::size_t>()))
                                .c_str());
            } else {
                const auto selected = before.at("selection").get<std::vector<std::size_t>>();
                for (std::size_t i = 0; i < selected.size(); ++i) {
                    io.AddKeyEvent(ImGuiMod_Ctrl, i != 0);
                    frame();
                    input.click(("ui-source:element:" + std::to_string(selected[i])).c_str());
                }
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
                frame();
            }
        };
        if (box) {
            open_style();
            const auto before_search = editor.state();
            input.click("ui-source:style-common");
            input.type("ui-source:style-filter", "no-such-property");
            check(!test::ui_targets.contains("ui-source:style-choice:border-color") &&
                      editor.state() == before_search,
                  "Property search retained unrelated choices or changed source");
            input.type("ui-source:style-filter", "BoRdEr");
            frame();
            capture.save(output / ("ui-source-box-search-" + std::to_string(width) + ".png"));
            input.select("ui-source:style-choice:border-color");
            input.click("ui-source:style-new-color");
            input.type("ui-source:style-color", "CC8844FF");
            frame();
            capture.save(output / ("ui-source-box-color-" + std::to_string(width) + ".png"));
            check(editor.state() == before_search, "Box color published before Set property");
            input.click("ui-source:style-set");
            const auto colored = editor.state();
            check(editor.read_style(2, "border-color") == "#cc8844",
                  "Border color did not use the shared color owner");
            input.click("button:Undo");
            check(editor.state().at("text") == before_search.at("text"),
                  "Border color needed multiple Undo steps");
            input.click("button:Redo");
            check(editor.state().at("text") == colored.at("text"), "Border color Redo differs");
            input.click("ui-source:element:2");
            preset("position", "relative", "box-position");
            preset("box-sizing", "border-box", "box-sizing");
        } else if (responsive) {
            preset("display", "flex", "layout-display");
            preset("flex-direction", "column", "layout-direction");
        } else {
            preset("font-weight", "bold", "typography-weight");
            preset("font-style", "italic", "typography-style");
        }
        editor.control(1, "paragraph", "Second");
        frame();
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        frame();
        input.click("ui-source:element:2");
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        frame();
        check(editor.state().at("selection").size() == 2, "Typography group selection missing");
        const auto* group_property = box ? "overflow-x" : responsive ? "align-self" : "font-style";
        const auto* group_value = box ? "hidden" : responsive ? "center" : "italic";
        preset(group_property, group_value,
               box          ? "box-group"
               : responsive ? "layout-group"
                            : "typography-group");
        const std::array<std::size_t, 2> members{2, 3};
        const auto values = editor.read_styles(members, group_property);
        check(values.size() == 2 && values[0] == group_value && values[1] == group_value,
              "Group preset did not use the shared complete source candidate");
        editor.save(project);
        const auto sheet = root / "Assets/Typography.rcss";
        const std::string sheet_source = "/* retain */\r\n.caption { color:red; }\r\n";
        asset_storage::replace(sheet, sheet_source);
        editor.open(project, "Assets/Typography.rcss");
        frame();
        frame();
        input.click("ui-source:elements-tab");
        input.click("ui-source:rule:0");
        const auto* rule_property = box          ? "overflow-y"
                                    : responsive ? "justify-content"
                                                 : "text-align";
        preset(rule_property, box ? "auto" : "center",
               box          ? "box-rule"
               : responsive ? "layout-rule"
                            : "typography-rule");
        check(editor.read_rule_property(0, rule_property) == (box ? "auto" : "center"),
              "Rule preset missing authored alignment");
        const auto* numeric_property = box          ? "padding-right"
                                       : responsive ? "column-gap"
                                                    : "line-height";
        choose_property(numeric_property);
        const auto before_spacing = editor.state();
        input.click("ui-source:style-new-number");
        input.type("ui-source:style-number", (box || responsive) ? "5" : "120");
        input.click("ui-source:style-unit");
        input.select("ui-source:style-unit:3");
        check(editor.state() == before_spacing,
              "Line spacing numeric controls published before Set property");
        input.click("ui-source:style-set");
        check(editor.read_rule_property(0, numeric_property) ==
                  ((box || responsive) ? "5%" : "120%"),
              "Rule spacing staged the wrong unit/value");
        editor.save(project);
        check(scene.snapshot() == authored && scene.revision() == revision &&
                  asset_storage::read(root / "forge.assets.json") == catalog,
              "Typography source edits changed Scene/catalog ownership");
        editor.request_close();
        return;
    }
    if (mode == SourceControls::TextProperties) {
        input.click("ui-source:elements-tab");
        for (const auto* kind : {"text-input", "password-input", "textarea", "label"}) {
            editor.control(1, kind, "Initial & <source>");
            frame();
            input.click("ui-source:element:3");
            const auto before = editor.state();
            const auto baseline_bytes = asset_storage::read(file);
            const bool label = std::string_view(kind) == "label";
            const bool area = std::string_view(kind) == "textarea";
            if (label)
                input.type("ui-source:control-property:for", "player-name");
            else {
                input.type("ui-source:control-property:placeholder", "A & <hint>");
                input.type("ui-source:control-property:maxlength", "80");
                if (area) {
                    input.type("ui-source:control-property:rows", "5");
                    input.type("ui-source:control-property:cols", "32");
                    input.click("ui-source:control-property:wrap");
                    input.select("ui-source:control-choice:wrap:nowrap");
                } else {
                    input.type("ui-source:control-property:value", "Authored & <value>");
                    input.type("ui-source:control-property:size", "16");
                }
            }
            check(editor.state() == before && asset_storage::read(file) == baseline_bytes,
                  "Text source staging changed history or disk");
            const char* capture_name = label ? "label" : area ? "textarea" : kind;
            frame();
            capture.save(output / ("ui-source-" + std::string(capture_name) + "-properties-" +
                                   std::to_string(width) + ".png"));
            input.click("ui-source:control-apply");
            const auto after = editor.state();
            check(after.at("selection") == Json::array({3}) &&
                      after.at("text") != before.at("text") &&
                      asset_storage::read(file) == baseline_bytes,
                  "Text source properties lost selected root or implicitly saved");
            const auto properties = editor.control_properties(3);
            const auto value = [&](const char* name) {
                for (const auto& row : properties.properties)
                    if (row.name == name)
                        return row.value;
                throw std::runtime_error("Missing text source property");
            };
            check(label ? value("for") == UiControlPropertyValue(std::string("player-name"))
                        : value("maxlength") == UiControlPropertyValue(80.0),
                  "Native text source property published the wrong value");
            if (area)
                check(value("wrap") == UiControlPropertyValue(std::string("nowrap")) &&
                          after.at("text").get<std::string>().find(
                              ">Initial &amp; &lt;source&gt;</textarea>") != std::string::npos,
                      "Textarea properties replaced body text or wrapping");
            input.click("button:Undo");
            check(editor.state().at("text") == before.at("text"),
                  "Text property batch needed multiple Undo steps");
            input.click("button:Redo");
            check(editor.state().at("text") == after.at("text"), "Text property Redo differs");
            editor.save(project);
            const auto stored = asset_storage::read(file);
            check(!editor.dirty() && stored &&
                      stored->find(label ? "player-name" : "maxlength=\"80\"") != std::string::npos,
                  "Explicit text property Save omitted source settings");
            editor.undo(false);
            editor.undo(false);
            editor.save(project);
        }
        check(scene.snapshot() == authored && scene.revision() == revision &&
                  asset_storage::read(root / "forge.assets.json") == catalog,
              "Text source property controls changed Scene or catalog ownership");
        editor.request_close();
        return;
    }
    const auto start_drag = [&](const char* from, const char* to, bool before = false) {
        frame();
        const auto a = test::ui_targets.at(from), b = test::ui_targets.at(to);
        const ImVec2 source_point{(a.minimum.x + a.maximum.x) / 2, (a.minimum.y + a.maximum.y) / 2};
        const ImVec2 target_point{(b.minimum.x + b.maximum.x) / 2, (b.minimum.y + b.maximum.y) / 2};
        io.AddMousePosEvent(source_point.x, source_point.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMousePosEvent(source_point.x + 12, source_point.y);
        frame();
        frame();
        check(editor.gesture_active(), "Actual outline dragging did not start a guarded gesture");
        io.AddKeyEvent(ImGuiMod_Shift, before);
        io.AddMousePosEvent(target_point.x, target_point.y);
        frame();
        frame();
    };
    const auto release_drag = [&] {
        io.AddMouseButtonEvent(0, false);
        frame();
        io.AddKeyEvent(ImGuiMod_Shift, false);
        frame();
        frame();
        check(!editor.gesture_active(), "Outline drop retained an active source gesture");
    };
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
    const std::string hierarchy =
        "<rml><body><p id='a'>One</p><button id='b'>Two</button>"
        "<div id='target'><span id='anchor'>Keep</span></div></body></rml>\n";
    editor.replace_text(hierarchy);
    const auto hierarchy_baseline = editor.state().at("text");
    input.click("ui-source:element:2");
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    frame();
    input.click("ui-source:element:3");
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    frame();
    input.click("ui-source:hierarchy-open");
    input.click("ui-source:hierarchy-parent");
    input.select("ui-source:parent:4");
    input.click("ui-source:hierarchy-before");
    input.select("ui-source:before:5");
    frame();
    capture.save(output / ("ui-source-hierarchy-" + std::to_string(width) + ".png"));
    input.click("ui-source:hierarchy-apply");
    auto hierarchy_expected = hierarchy_baseline.get<std::string>();
    const std::string siblings = "<p id='a'>One</p><button id='b'>Two</button>";
    hierarchy_expected.erase(hierarchy_expected.find(siblings), siblings.size());
    hierarchy_expected.insert(hierarchy_expected.find("<span id='anchor'>"), siblings);
    check(editor.state().at("text") == hierarchy_expected && editor.state().at("selection").empty(),
          "Native container/placement controls did not move the complete sibling selection");
    input.click("button:Undo");
    check(editor.state().at("text") == hierarchy_baseline,
          "Native hierarchy movement needed more than one Undo");
    input.click("button:Redo");
    check(editor.state().at("text") == hierarchy_expected, "Hierarchy Redo lost moved source");
    input.click("button:Undo");
    input.click("ui-source:element:2");
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    frame();
    input.click("ui-source:element:3");
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    frame();
    start_drag("ui-source:element:2", "ui-source:element:4");
    check(editor.state().at("text") == hierarchy_baseline && !editor.can_undo(),
          "Outline hover published source or left history enabled during a gesture");
    rejects([&] { editor.save(project); });
    release_drag();
    auto appended = hierarchy_baseline.get<std::string>();
    appended.erase(appended.find(siblings), siblings.size());
    appended.insert(appended.find("</div>"), siblings);
    check(editor.state().at("text") == appended,
          "Native outline drop lost a selected sibling, source order or its destination");
    input.click("button:Undo");
    check(editor.state().at("text") == hierarchy_baseline,
          "Outline group drop needed more than one Undo");
    input.click("ui-source:element:3");
    start_drag("ui-source:element:3", "ui-source:element:2", true);
    release_drag();
    auto reordered = hierarchy_baseline.get<std::string>();
    reordered.replace(reordered.find(siblings), siblings.size(),
                      "<button id='b'>Two</button><p id='a'>One</p>");
    check(editor.state().at("text") == reordered,
          "Shift outline drop did not place before the row");
    input.click("button:Undo");
    check(editor.state().at("text") == hierarchy_baseline,
          "Shift outline placement needed more than one Undo");
    input.click("ui-source:element:2");
    start_drag("ui-source:element:2", "ui-source:element:4");
    io.AddKeyEvent(ImGuiKey_Escape, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Escape, false);
    release_drag();
    check(editor.state().at("text") == hierarchy_baseline,
          "Escaping an outline drag changed source");
    start_drag("ui-source:element:2", "ui-source:element:4");
    locked = true;
    frame();
    locked = false;
    frame();
    release_drag();
    check(editor.state().at("text") == hierarchy_baseline,
          "A task lock failed to cancel the native outline payload");
    start_drag("ui-source:element:2", "ui-source:element:4");
    io.AddFocusEvent(false);
    frame();
    io.AddFocusEvent(true);
    frame();
    release_drag();
    check(editor.state().at("text") == hierarchy_baseline,
          "Application focus loss committed an outline drop");
    start_drag("ui-source:element:2", "ui-source:element:4");
    const auto tab = test::ui_targets.at("ui-source:source-tab");
    io.AddMousePosEvent((tab.minimum.x + tab.maximum.x) / 2, (tab.minimum.y + tab.maximum.y) / 2);
    for (int i = 0; i < 60; ++i)
        frame();
    check(!test::ui_targets.contains("ui-source:element:2") && !editor.gesture_active(),
          "Native tab hover did not cancel the outline source gesture");
    release_drag();
    check(editor.state().at("text") == hierarchy_baseline,
          "Changing source views during a drag changed source");
    input.click("ui-source:elements-tab");
    input.click("ui-source:element:2");
    start_drag("ui-source:element:2", "ui-source:element:4");
    editor.attribute(2, "title", "Changed during drag");
    const auto changed_during_drag = editor.state().at("text");
    frame();
    release_drag();
    check(editor.state().at("text") == changed_during_drag,
          "A stale native payload authorized a source move");
    input.click("button:Undo");
    check(editor.state().at("text") == hierarchy_baseline,
          "Rejected stale drop added a source history record");
    input.click("button:Undo");
    check(editor.state().at("text") == original,
          "Hierarchy controls did not restore the original source history");
    input.click("ui-source:element:2");
    input.click("ui-source:hierarchy-open");
    input.click("ui-source:style-open");
    input.type("ui-source:style-name", "color");
    input.type("ui-source:style-value", "#77BDF7FF");
    input.click("ui-source:style-set");
    auto color_expected = original.get<std::string>();
    color_expected.replace(color_expected.find("<p>"), 3, "<p style=\"color:#77BDF7FF;\">");
    check(editor.state().at("text") == color_expected &&
              editor.state().at("selection") == Json::array({2}),
          "Native inline property controls lost the declaration or edited selection");
    input.click("ui-source:element:2");
    const auto colored_state = editor.state();
    input.type("ui-source:style-value", "scratch");
    input.click("ui-source:style-read");
    input.click("ui-source:style-set");
    check(editor.state() == colored_state, "Read inline or exact property no-op changed history");
    frame();
    capture.save(output / ("ui-source-inline-style-" + std::to_string(width) + ".png"));
    input.click("button:Undo");
    check(editor.state().at("text") == original, "Inline property set needed more than one Undo");
    input.click("ui-source:element:2");
    input.click("ui-source:style-common");
    input.select("ui-source:style-choice:width");
    input.type("ui-source:style-value", "24px");
    const auto before_number = editor.state();
    input.type("ui-source:style-number", "70000");
    check(editor.state() == before_number && !test::ui_targets.at("ui-source:style-set").enabled,
          "An out-of-range staged number published source or enabled Set property");
    input.type("ui-source:style-number", "27.5");
    check(test::ui_targets.at("ui-source:style-set").enabled,
          "A valid numeric replacement did not recover property publication");
    input.click("ui-source:style-unit");
    input.select("ui-source:style-unit:2");
    check(editor.state() == before_number && asset_storage::read(file) == source,
          "Numeric/unit staging published source or saved the document");
    input.click("ui-source:style-set");
    const auto width_source = editor.state().at("text");
    check(editor.read_style(2, "width") == "27.5dp",
          "Actual numeric/unit controls did not publish the staged density-pixel value");
    frame();
    capture.save(output / ("ui-source-style-number-" + std::to_string(width) + ".png"));
    input.click("ui-source:element:2");
    input.click("ui-source:style-remove");
    check(editor.state().at("text").get<std::string>().find("style=\"\"") != std::string::npos,
          "Native property removal changed unrelated attribute bytes");
    input.click("button:Undo");
    check(editor.state().at("text") == width_source, "Inline removal needed more than one Undo");
    input.click("button:Undo");
    check(editor.state().at("text") == original,
          "Common-property history lost the original source");
    input.click("ui-source:element:2");
    input.click("ui-source:style-common");
    input.select("ui-source:style-choice:color");
    const auto before_color = editor.state();
    check(!test::ui_targets.contains("ui-source:style-color") &&
              test::ui_targets.contains("ui-source:style-new-color"),
          "An absent authored color displayed guessed channels");
    input.click("ui-source:style-new-color");
    input.type("ui-source:style-color", "12345680");
    check(editor.state() == before_color, "Actual color control published a staged value");
    input.click("ui-source:style-set");
    check(editor.read_style(2, "color") == "#12345680" && asset_storage::read(file) == source,
          "Actual RGBA color control lost channel order, alpha or explicit Save isolation");
    frame();
    capture.save(output / ("ui-source-style-color-" + std::to_string(width) + ".png"));
    input.click("button:Undo");
    check(editor.state().at("text") == original,
          "Color staging added more than one source Undo step");
    input.click("ui-source:element:2");
    input.click("ui-source:style-open");
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
    const auto control_baseline = editor.state().at("text");
    for (const auto& [kind, markup] : std::array<std::pair<const char*, const char*>, 6>{
             {{"checkbox", "<input type=\"checkbox\" value=\"Choice &amp; &lt;one&gt;\"/>"},
              {"input-button", "<input type=\"button\" value=\"Choice &amp; &lt;one&gt;\"/>"},
              {"range", "<input type=\"range\" value=\"12.5\"/>"},
              {"radio", "<input type=\"radio\" value=\"Choice &amp; &lt;one&gt;\"/>"},
              {"textarea", "<textarea>Choice &amp; &lt;one&gt;</textarea>"},
              {"select", "<select><option value=\"Choice &amp; &lt;one&gt;\" "
                         "selected=\"selected\">Choice &amp; &lt;one&gt;</option></select>"}}}) {
        input.click("ui-source:element:1");
        const auto before_creation = editor.state();
        input.click("ui-source:child-kind");
        input.select(("ui-source:child-choice:" + std::string(kind)).c_str());
        input.type("ui-source:child-text",
                   std::string_view(kind) == "range" ? "12.5" : "Choice & <one>");
        check(editor.state() == before_creation && asset_storage::read(file) == saved,
              "Control kind/initial-value staging published source or saved the file");
        if (std::string_view(kind) == "input-button") {
            frame();
            capture.save(output / ("ui-source-input-button-" + std::to_string(width) + ".png"));
        }
        if (std::string_view(kind) == "range") {
            frame();
            capture.save(output / ("ui-source-slider-" + std::to_string(width) + ".png"));
        }
        if (std::string_view(kind) == "select") {
            frame();
            capture.save(output / ("ui-source-create-control-" + std::to_string(width) + ".png"));
        }
        input.click("ui-source:child-add");
        auto expected = control_baseline.get<std::string>();
        expected.insert(expected.find("</body>"), markup);
        check(editor.state().at("text") == expected &&
                  editor.state().at("selection") == Json::array({3}) &&
                  asset_storage::read(file) == saved,
              "Native control creation lost initial source, new-root selection or explicit Save");
        if (std::string_view(kind) == "range") {
            frame();
            capture.save(output / ("ui-source-slider-created-" + std::to_string(width) + ".png"));
        }
        if (std::string_view(kind) == "range" || std::string_view(kind) == "checkbox" ||
            std::string_view(kind) == "radio" || std::string_view(kind) == "input-button") {
            const auto before_properties = editor.state();
            if (std::string_view(kind) == "range") {
                input.type("ui-source:control-property:min", "-1");
                input.click("ui-source:control-discard");
                check(editor.state() == before_properties,
                      "Discarding staged control properties changed source/history");
                input.type("ui-source:control-property:min", "-2");
                input.type("ui-source:control-property:max", "8");
                input.type("ui-source:control-property:step", "3");
                input.click("ui-source:control-reset:step");
                input.click("ui-source:control-reset:step");
                check(editor.state() == before_properties,
                      "Reset/Keep override staging changed source/history");
                input.type("ui-source:control-property:step", "2");
                input.click("ui-source:control-property:orientation");
                input.select("ui-source:control-choice:orientation:vertical");
                input.click("ui-source:control-property:disabled");
            } else if (std::string_view(kind) == "input-button") {
                input.type("ui-source:control-property:value", "Go & <safe>");
            } else {
                if (std::string_view(kind) == "radio")
                    input.type("ui-source:control-property:name", "team & quality");
                input.click("ui-source:control-property:checked");
            }
            check(editor.state() == before_properties && asset_storage::read(file) == saved,
                  "Staging native control properties wrote source/history/disk");
            if (std::string_view(kind) == "range") {
                input.click("ui-source:control-property:value");
                frame();
                capture.save(output /
                             ("ui-source-control-properties-" + std::to_string(width) + ".png"));
            } else if (std::string_view(kind) == "checkbox") {
                input.click("ui-source:control-property:value");
                frame();
                capture.save(output /
                             ("ui-source-control-flags-" + std::to_string(width) + ".png"));
            }
            input.click("ui-source:control-apply");
            const auto applied = editor.state();
            check(applied.at("selection") == Json::array({3}) && applied.at("text") != expected &&
                      asset_storage::read(file) == saved,
                  "Control properties did not select their root or implicitly saved source");
            const auto source_properties = editor.control_properties(3);
            const auto value = [&](const char* name) {
                for (const auto& row : source_properties.properties)
                    if (row.name == name)
                        return row.value;
                throw std::runtime_error("Missing authored control property");
            };
            if (std::string_view(kind) == "range") {
                check(value("min") == UiControlPropertyValue(-2.0) &&
                          value("step") == UiControlPropertyValue(2.0) &&
                          value("disabled") == UiControlPropertyValue(true),
                      "Native range property panel published different values");
            } else if (std::string_view(kind) == "input-button") {
                check(value("value") == UiControlPropertyValue(std::string("Go & <safe>")),
                      "Native caption property changed escaping");
            } else
                check(value("checked") == UiControlPropertyValue(true),
                      "Native checkbox/radio panel did not use flag presence");
            if (std::string_view(kind) == "range") {
                input.click("ui-source:control-reset:min");
                check(editor.state() == applied, "Reset property published before Apply");
                input.click("ui-source:control-apply");
                const auto reset_properties = editor.control_properties(3);
                bool removed_minimum = false;
                for (const auto& row : reset_properties.properties)
                    if (row.name == "min")
                        removed_minimum = !row.authored && row.value == UiControlPropertyValue(0.0);
                check(removed_minimum && asset_storage::read(file) == saved,
                      "Applied reset did not remove the override or saved implicitly");
                input.click("button:Undo");
                check(editor.state().at("text") == applied.at("text"),
                      "Property reset needed more than one Undo");
            }
            input.click("button:Undo");
            check(editor.state().at("text") == expected,
                  "Control property batch needed more than one Undo");
            input.click("button:Redo");
            check(editor.state().at("text") == applied.at("text"),
                  "Control property batch Redo differs");
            input.click("button:Undo");
        }
        if (std::string_view(kind) == "select") {
            input.click("ui-source:child-kind");
            input.select("ui-source:child-choice:option");
            input.type("ui-source:child-text", "Next");
            input.click("ui-source:child-add");
            check(editor.outline().size() == 6 &&
                      editor.state().at("selection") == Json::array({5}),
                  "Native dropdown option creation lost its new selected root");
            input.click("button:Undo");
            check(editor.state().at("text") == expected,
                  "Dropdown option needed more than one Undo");
        }
        input.click("button:Undo");
        check(editor.state().at("text") == control_baseline,
              "Common control creation needed more than one Undo");
        input.click("button:Redo");
        check(editor.state().at("text") == expected, "Common control creation Redo lost source");
        input.click("button:Undo");
    }
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
void exercise_stylesheet(const std::filesystem::path& output, int width) {
    using namespace forge;
    const auto root = output / AssetId::generate().str();
    SceneDocument::create_project(root, "Stylesheet source");
    std::filesystem::create_directories(root / "Assets");
    const auto file = root / "Assets/Hud.rcss";
    const std::string source = "/* retained */\r\n.accent { color:red; }\r\n"
                               "@media (min-width:20px) { .nested {color:blue;} }\r\n.tail{}\r\n";
    asset_storage::replace(file, source);
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    const auto scene_before = scene.snapshot();
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
    test::ImGuiInputDriver input(frame);
    editor.open(project, "Assets/Hud.rcss");
    frame();
    frame();
    const auto draft = editor.state().at("text").get<std::string>();
    std::string expected_draft;
    for (const auto c : source)
        if (c != '\r')
            expected_draft += c;
    expected_draft += '\n';
    check(draft == expected_draft, "Stylesheet draft lost the existing widget newline contract");
    input.click("ui-source:elements-tab");
    input.click("ui-source:rule:0");
    input.click("ui-source:style-open");
    const auto before_rule_color = editor.state();
    check(!test::ui_targets.contains("ui-source:style-color"),
          "A named rule color displayed guessed hex channels");
    input.click("ui-source:style-new-color");
    input.type("ui-source:style-color", "01020304");
    check(editor.state() == before_rule_color && asset_storage::read(file) == source,
          "Rule color controls wrote source before Set property");
    input.type("ui-source:style-value", "#77bdf7");
    input.click("ui-source:style-set");
    auto styled = draft;
    styled.replace(styled.find("color:red"), 9, "color:#77bdf7");
    check(editor.state().at("text") == styled && editor.state().at("rule_selection") == 0 &&
              editor.read_rule_property(0, "color") == "#77bdf7" &&
              asset_storage::read(file) == source,
          "Native rule property controls lost source, selected rule or explicit Save isolation");
    const auto styled_state = editor.state();
    input.type("ui-source:style-value", "scratch");
    input.click("ui-source:style-read");
    input.click("ui-source:style-set");
    check(editor.state() == styled_state, "Stylesheet read/no-op changed source/history");
    frame();
    capture.save(output / ("ui-source-rules-" + std::to_string(width) + ".png"));
    input.click("ui-source:rule-copy");
    const std::string fragment = ".accent { color:#77bdf7; }";
    auto copied = styled;
    const auto insertion = copied.find(fragment) + fragment.size();
    copied.insert(insertion, "\n" + fragment);
    check(editor.state().at("text") == copied && editor.state().at("rule_selection") == 1,
          "Native rule copy changed source or failed to select the new block");
    const auto copied_state = editor.state();
    input.click("ui-source:rule-up");
    check(editor.state() == copied_state, "Identical native rule move changed history/selection");
    check(test::ui_targets.contains("ui-source:rule-down"), "Native rule-down control absent");
    check(!test::ui_targets.at("ui-source:rule-down").enabled,
          "Native rule movement exposed an at-rule barrier");
    input.type("ui-source:style-value", "#112233");
    input.click("ui-source:style-set");
    auto edited_copy = copied;
    edited_copy.replace(edited_copy.find("#77bdf7", insertion), 7, "#112233");
    input.click("ui-source:rule-up");
    auto moved = styled;
    moved.replace(moved.find(fragment), fragment.size(), ".accent { color:#112233; }\n" + fragment);
    check(editor.state().at("text") == moved && editor.state().at("rule_selection") == 0,
          "Native rule order lost the moved source or selected index");
    input.click("button:Undo");
    check(editor.state().at("text") == edited_copy, "Rule movement needed more than one Undo");
    input.click("button:Undo");
    check(editor.state().at("text") == copied, "Copied rule editing needed more than one Undo");
    input.click("button:Undo");
    check(editor.state().at("text") == styled, "Rule copy needed more than one Undo");
    input.click("button:Undo");
    check(editor.state().at("text") == draft, "Stylesheet property needed more than one Undo");
    input.click("button:Redo");
    input.click("ui-source:rule:0");
    input.type("ui-source:rule-selector", ".renamed > button");
    input.click("ui-source:rule-rename");
    auto renamed = styled;
    renamed.replace(renamed.find(".accent "), 8, ".renamed > button");
    check(editor.state().at("text") == renamed, "Selector control changed the body or other rules");
    input.click("button:Undo");
    input.click("ui-source:rule:0");
    input.click("ui-source:style-remove");
    check(editor.state().at("text").get<std::string>().find(".accent {  }") != std::string::npos,
          "Native rule property removal lost surrounding whitespace");
    input.click("button:Undo");
    input.click("ui-source:rule:0");
    input.click("ui-source:rule-remove");
    check(editor.rules().size() == 2 && editor.state().at("rule_selection").is_null(),
          "Native rule removal retained stale selection or lost an adjacent block");
    input.click("button:Undo");
    check(editor.state().at("text") == styled, "Rule removal needed more than one Undo");
    input.type("ui-source:rule-selector", ".added");
    input.click("ui-source:rule-add");
    check(editor.state().at("text") == styled + ".added {}\n" &&
              editor.state().at("rule_selection") == 3,
          "Native Add rule changed source order or failed to select the new block");
    input.click("button:Undo");
    check(editor.state().at("text") == styled, "Rule insertion needed more than one Undo");
    input.click("ui-source:rule:1");
    check(!test::ui_targets.contains("ui-source:rule-remove") &&
              !test::ui_targets.contains("ui-source:style-set"),
          "An at-rule exposed flat-rule property/removal controls");
    const auto before_reject = editor.state();
    rejects([&] { editor.rule_property(1, "color", "red"); });
    rejects([&] { editor.rule_selector(1, ".bad"); });
    rejects([&] { editor.remove_rule(1); });
    check(editor.state() == before_reject, "Rejected at-rule edit changed history/selection");
    locked = true;
    frame();
    rejects([&] { editor.add_rule(".locked"); });
    locked = false;
    frame();
    editor.save(project);
    auto stored = source;
    stored.replace(stored.find("color:red"), 9, "color:#77bdf7");
    stored += "\r\n";
    check(asset_storage::read(file) == stored && !editor.dirty() &&
              scene.snapshot() == scene_before &&
              asset_storage::read(root / "forge.assets.json") == catalog,
          "Stylesheet Save normalized bytes or changed Scene/registry ownership");
    editor.replace_text(".incomplete {");
    check(!editor.state().at("outline_error").get<std::string>().empty(),
          "Incomplete stylesheet did not retain an editable Source with a clear outline error");
    rejects([&] { editor.save(project); });
    check(asset_storage::read(file) == stored, "Invalid stylesheet replaced the saved resource");
    editor.reload(project, true);
    editor.replace_text(".large{color:" + std::string(9000, 'a') + ";}");
    frame();
    input.click("ui-source:rule:0");
    check(test::ui_targets.contains("ui-source:style-set"), "Oversized-value style control absent");
    check(!test::ui_targets.at("ui-source:style-set").enabled,
          "An oversized CSS value exposed a silently truncated native replacement");
    editor.reload(project, true);
    editor.request_close();
    check(!editor.is_open() && editor.rules().empty(), "Close retained stylesheet ranges");
    const auto group_file = root / "Assets/Group.rml";
    asset_storage::replace(
        group_file, "<rml><body data-model='demo'><p id='a' class='first' "
                    "data-class-hot='hot' style='color:red;'>A</p><button id='b' class='second' "
                    "style='color:blue;'>B</button></body></rml>\n");
    editor.open(project, "Assets/Group.rml");
    frame();
    frame();
    input.click("ui-source:elements-tab");
    const auto group_original = editor.state().at("text").get<std::string>();
    const auto choose_group = [&] {
        input.click("ui-source:element:2");
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        frame();
        input.click("ui-source:element:3");
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        frame();
    };
    choose_group();
    input.click("ui-source:style-open");
    check(test::ui_targets.contains("ui-source:style-mixed"),
          "Native group property controls silently chose one mixed value");
    check(!test::ui_targets.contains("ui-source:style-color"),
          "Mixed authored group colors exposed one member's channels");
    const auto mixed = editor.state();
    input.click("ui-source:style-set");
    check(editor.state() == mixed, "An empty mixed property partially overwrote the group");
    input.click("ui-source:style-new-color");
    input.type("ui-source:style-color", "aabbcc80");
    check(editor.state() == mixed, "Group color staging partially published to source");
    input.type("ui-source:style-value", "#77bdf7");
    input.click("ui-source:style-set");
    auto uniform = group_original;
    uniform.replace(uniform.find("color:red"), 9, "color:#77bdf7");
    uniform.replace(uniform.find("color:blue"), 10, "color:#77bdf7");
    frame();
    check(editor.state().at("text") == uniform &&
              editor.state().at("selection") == Json::array({2, 3}) &&
              !test::ui_targets.contains("ui-source:style-mixed"),
          "Native group style edit lost source, retained selection or uniform feedback");
    const auto uniform_state = editor.state();
    input.type("ui-source:style-value", "scratch");
    input.click("ui-source:style-read");
    input.click("ui-source:style-set");
    check(editor.state() == uniform_state, "Native group read/no-op changed history");
    frame();
    capture.save(output / ("ui-source-group-styling-" + std::to_string(width) + ".png"));
    input.click("ui-source:style-open");
    input.click("ui-source:classes-open");
    check(test::ui_targets.contains("ui-source:classes-mixed") &&
              !test::ui_targets.at("ui-source:classes-set").enabled,
          "Native class controls silently chose one mixed class list");
    input.type("ui-source:classes-value", "shared Active");
    input.click("ui-source:classes-set");
    auto assigned = uniform;
    assigned.replace(assigned.find("class='first'"), 13, "class=\"shared Active\"");
    assigned.replace(assigned.find("class='second'"), 14, "class=\"shared Active\"");
    frame();
    check(editor.state().at("text") == assigned &&
              editor.state().at("selection") == Json::array({2, 3}) &&
              !test::ui_targets.contains("ui-source:classes-mixed") &&
              assigned.find("data-class-hot='hot'") != std::string::npos,
          "Native class assignment lost group source, selection or runtime binding");
    input.type("ui-source:classes-value", "scratch");
    input.click("ui-source:classes-read");
    input.click("ui-source:classes-set");
    check(editor.state().at("text") == assigned, "Native class read changed the authored list");
    frame();
    capture.save(output / ("ui-source-classes-" + std::to_string(width) + ".png"));
    input.click("ui-source:classes-clear");
    check(editor.state().at("text").get<std::string>().find("class=\"shared Active\"") ==
                  std::string::npos &&
              editor.state().at("text").get<std::string>().find("data-class-hot='hot'") !=
                  std::string::npos,
          "Native class clearing removed runtime bindings or retained authored lists");
    input.click("button:Undo");
    check(editor.state().at("text") == assigned, "Class clearing needed more than one Undo");
    input.click("button:Undo");
    check(editor.state().at("text") == uniform, "Class assignment/no-op needed more than one Undo");
    input.click("button:Undo");
    check(editor.state().at("text") == group_original, "Group property needed more than one Undo");
    check(scene.snapshot() == scene_before &&
              asset_storage::read(root / "forge.assets.json") == catalog,
          "Reusable source styling changed Scene or asset ownership");
    editor.reload(project, true);
    editor.request_close();
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2 || argc == 3, "Need capture/scratch directory and optional control mode");
        auto mode = SourceControls::All;
        if (argc == 3) {
            const auto flag = std::string_view(argv[2]);
            if (flag == "--creation-setup-only")
                mode = SourceControls::CreationSetup;
            else if (flag == "--box-styling-only")
                mode = SourceControls::BoxStyling;
            else if (flag == "--responsive-layout-only")
                mode = SourceControls::ResponsiveLayout;
            else if (flag == "--typography-only")
                mode = SourceControls::Typography;
            else if (flag == "--text-properties-only")
                mode = SourceControls::TextProperties;
            else if (flag == "--image-properties-only")
                mode = SourceControls::ImageProperties;
            else
                throw std::runtime_error("Unknown source control mode");
        }
        const auto output = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(output);
        for (const auto width : {1180, 700, 400}) {
            exercise(output, width, mode);
            if (mode == SourceControls::All)
                exercise_stylesheet(output, width);
        }
        std::cout << "Native UI source controls/history/storage at1180/700/400 passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
