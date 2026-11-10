#include "asset_bytes.hpp"
#include "project_text_source.hpp"
#include "runtime_package.hpp"
#include "ui_markup_authoring.hpp"
#include "ui_test_renderer.hpp"
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi/Core/Elements/ElementFormControlTextArea.h>
#include <RmlUi/Core/Elements/ElementProgress.h>
#include <algorithm>
#include <cmath>
#include <forge/authoring.hpp>
#include <forge/runtime_ui.hpp>
#include <fstream>
#include <iostream>

using namespace forge;
namespace {
constexpr auto scene_path = "Scenes/210-ui-source-authoring.scene.json";
constexpr auto document_path = "Assets/UI/SourceAuthoring/hud.rml";
constexpr auto stylesheet_path = "Assets/UI/SourceAuthoring/hud.rcss";
constexpr auto layout_scene = "Scenes/211-ui-layout-preview.scene.json";
constexpr auto layout_document = "Assets/UI/Layout/layout.rml";
constexpr auto forms_scene = "Scenes/212-ui-form-controls.scene.json";
constexpr auto forms_document = "Assets/UI/Forms/forms.rml";
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
Json read_json(const std::filesystem::path& path) {
    Json value;
    std::ifstream(path) >> value;
    return value;
}
std::size_t by_id(const std::string& source, std::string_view id) {
    const auto nodes = inspect_ui_markup(source);
    for (std::size_t i = 0; i < nodes.size(); ++i)
        for (const auto& attribute : nodes[i].attributes)
            if (attribute.name == "id" &&
                std::string_view(source).substr(attribute.value.begin,
                                                attribute.value.end - attribute.value.begin) == id)
                return i;
    throw std::runtime_error("UI source example element missing");
}
RuntimeUiInspector inspector(const std::filesystem::path& root,
                             const std::vector<std::byte>& font) {
    return [=](AssetId id, std::stop_token) {
        UiTestRenderer renderer;
        UiPresenter presenter(renderer, root, font);
        return presenter.inspect_static({id});
    };
}
void author(const std::filesystem::path& root, const std::vector<std::byte>& font) {
    check(!std::filesystem::exists(root / scene_path) &&
              !std::filesystem::exists(root / document_path),
          "UI source example already exists");
    std::filesystem::create_directories((root / document_path).parent_path());
    std::string markup =
        "<!-- FORGE source authoring example -->\n<rml><head><link type=\"text/rcss\" "
        "href=\"hud.rcss\" /></head><body id=\"canvas\"><div id=\"panel\"></div></body></rml>\n";
    const auto append = [&](std::string_view tag, std::string id, std::string_view text) {
        markup = insert_ui_markup_child(markup, by_id(markup, "panel"), tag, text);
        const auto nodes = inspect_ui_markup(markup);
        // The appended direct child is last in source order; this example adds leaves only.
        markup = set_ui_markup_attribute(markup, nodes.size() - 1, "id", std::move(id));
    };
    append("p", "eyebrow", "FORGE / UI SOURCE AUTHORING");
    append("p", "heading", "Edit the source. Run the result.");
    append("p", "message", "Text & markup: <your own HUD>");
    append("p", "state", "Tick {{tick}} / paused {{paused}}");
    for (const auto& name : {"Pause", "Resume", "Step"}) {
        append("button", name, name);
        markup = set_ui_markup_attribute(markup, by_id(markup, name), "data-event-click",
                                         "command('" + std::string(name) + "')");
    }
    append("input", "name", {});
    markup = set_ui_markup_attribute(markup, by_id(markup, "name"), "type", "text");
    markup = set_ui_markup_attribute(markup, by_id(markup, "name"), "value", "Your name");
    append("p", "hint",
           "Stop Play. In Content, edit hud.rml with Source or Elements. Save, "
           "then start Play again. Edit hud.rcss to change layout and colors.");
    atomic_write(root / document_path, markup);
    atomic_write(
        root / stylesheet_path,
        R"rcss(body {width:100%;height:100%;font-family:Lato;font-size:18px;color:#edf3fa;pointer-events:none;}
body,div,p {display:block;} p {margin:10px 0;}
#panel {position:absolute;left:5%;top:5%;width:90%;max-width:660px;max-height:90%;box-sizing:border-box;padding:22px;overflow:auto;background-color:#162b3bf2;pointer-events:auto;}
#eyebrow {font-size:14px;color:#73d7bd;} #heading {font-size:28px;color:#ffffff;}
#state {padding:10px;background-color:#24485f;} #hint {font-size:15px;color:#bbcad6;}
button {display:inline-block;padding:10px 14px;margin:10px 7px 14px 0;background-color:#345a77;}
button:hover,button:focus {background-color:#487f9e;}
input {display:block;width:240px;height:32px;background-color:#0f202f;color:#ffffff;}
scrollbarvertical {width:12px;background-color:#101d29;} scrollbarvertical sliderbar {background-color:#487f9e;}
)rcss");
    const auto ui = register_ui_document(root, document_path);
    EngineContext engine;
    Scene scene(engine.world());
    auto source = duplicate_scene_asset(read_json(root / "Scenes/100-ui-animation.scene.json"),
                                        AssetId::generate(), scene.schema());
    for (auto& row : source["entities"])
        if (row["components"].contains("forge.ui_document")) {
            row["name"] = "HUD - edit Source or Elements";
            row["components"]["forge.ui_document"]["document"] = ui.id;
        }
    scene.reset(source);
    scene.save(root / scene_path);
    auto catalog = AssetCatalog::open_project(root);
    const auto record = catalog.add_scene(scene_path);
    catalog.save(AssetCatalog::project_index(root));
    ProjectLease lease(root);
    const std::array roots{record.id};
    prepare_runtime_content_catalog(lease, roots, inspector(root, font), scene.schema());
}
void exercise(const std::filesystem::path& root, const Json& source,
              const std::vector<std::byte>& font, bool external_conflict) {
    EngineContext engine(WorldRole::Runtime, false, {ui_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    const auto ui = std::static_pointer_cast<UiRuntime>(engine.services().ui());
    UiTestRenderer renderer;
    {
        UiPresenter presenter(renderer, root, font);
        presenter.reset("ui-source", 1);
        auto snapshot = ui->snapshot(scene, "ui-source", 1, 41, true);
        check(presenter.accept(snapshot), presenter.diagnostic().c_str());
        presenter.update(0, 800, 600);
        presenter.render();
        auto* document = Rml::GetContext(0)->GetDocument(0);
        check(document->GetElementById("message")->GetInnerRML().find("&lt;your own HUD&gt;") !=
                  std::string::npos,
              "Escaped source text was parsed as child markup");
        const auto initial_state = document->GetElementById("state")->GetInnerRML();
        if (initial_state != "Tick 41 / paused 1")
            throw std::runtime_error("Structured text binding result: " + initial_state);
        check(renderer.draws && !renderer.textures.empty() && presenter.asset_snapshot() &&
                  presenter.asset_snapshot()->sources.size() == 2,
              "UI source example lost text draw resources or linked stylesheet");
        check(document->GetElementById("heading")->GetProperty<float>("font-size") == 28,
              "Linked stylesheet layout was not applied");
        const auto position = document->GetElementById("Resume")->GetAbsoluteOffset();
        presenter.mouse_move(int(position.x + 5), int(position.y + 5), 0);
        presenter.mouse_button(0, true, 0);
        presenter.mouse_button(0, false, 0);
        const auto acknowledge = [&](const char* expected) {
            const auto command = presenter.pending_command();
            check(command.has_value(), "Native example button did not enqueue a command");
            std::string operation;
            ui->command(scene, *command, [&](const std::string& name) { operation = name; });
            check(operation == expected, "Native command did not reach the current UiRuntime");
            auto ack = *command;
            ack["ok"] = true;
            presenter.acknowledge(ack);
            check(!presenter.pending_command(), "Native button queued more than one command");
        };
        acknowledge("Resume");
        const auto instance = snapshot.at("documents")[0].at("instance").get<std::string>();
        const auto buttons = presenter.buttons(instance);
        check(buttons.at("buttons").size() == 4 && !buttons.at("truncated").get<bool>(),
              "Saved/exported buttons lost native discovery");
        auto* native = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(
            document->GetElementById("native-press"));
        check(native && native->GetValue() == "Step & <native>",
              "Saved/exported Input button lost native type or literal caption");
        presenter.press_button(instance, "native-press", presenter.presentation_revision());
        acknowledge("Step");
        presenter.widget_keyboard_action(
            instance, "native-press", presenter.presentation_revision(), "input-button", "focus");
        presenter.widget_keyboard_action(
            instance, "native-press", presenter.presentation_revision(), "input-button", "accept");
        acknowledge("Step");
        presenter.widget_keyboard_action(instance, "native-range",
                                         presenter.presentation_revision(), "range", "focus");
        presenter.widget_keyboard_action(instance, "native-range",
                                         presenter.presentation_revision(), "range", "right");
        acknowledge("Step");
        const auto ranges = presenter.range_controls(instance);
        check(ranges.at("ranges").size() == 1 && ranges.at("ranges")[0].at("value") == "2.000000",
              "Saved/exported slider lost its native value or range discovery");
        presenter.set_range_value(instance, "native-range", presenter.presentation_revision(), 7);
        acknowledge("Step");
        presenter.set_range_value(instance, "native-range", presenter.presentation_revision(), 200);
        acknowledge("Step");
        const auto repeated = presenter.set_range_value(instance, "native-range",
                                                        presenter.presentation_revision(), 200);
        check(repeated.at("value_text_changed") == false,
              "Native out-of-range repeat changed returned slider text");
        acknowledge("Step");
        presenter.widget_keyboard_action(instance, "Pause", presenter.presentation_revision(),
                                         "button", "focus");
        presenter.widget_keyboard_action(instance, "Pause", presenter.presentation_revision(),
                                         "button", "accept");
        check(!presenter.pending_command(),
              "Native generic button Return fabricated a command without tab-index:auto");
        presenter.press_button(instance, "Pause", presenter.presentation_revision());
        acknowledge("Pause");
        snapshot = ui->snapshot(scene, "ui-source", 1, 42, false);
        check(presenter.accept(snapshot), presenter.diagnostic().c_str());
        presenter.update(.02, 800, 600);
        check(document->GetElementById("state")->GetInnerRML() == "Tick 42 / paused 0",
              "Play model update did not reach the authored source");
        if (external_conflict) {
            const auto path = root / document_path;
            const auto saved = ProjectTextSource::read(path);
            check(saved.has_value(), "Saved source example disappeared");
            atomic_write(path, "<rml><body><script>forbidden</script></body></rml>");
            check(!presenter.reload() && presenter.document_count() == 1,
                  "Invalid external source discarded last-good UI");
            atomic_write(path, *saved);
            check(presenter.reload(), presenter.diagnostic().c_str());
            presenter.update(.04, 800, 600);
            presenter.render();
        }
        presenter.reset("ui-source-retired", 2);
        check(Rml::GetNumContexts() == 0, "Retired source UI retained a native context");
    }
    check(renderer.geometry.empty() && renderer.textures.empty(),
          "Source UI native resources survived presenter retirement");
    check(scene.snapshot() == source, "UI interaction changed authored Scene state");
}
void author_layout(const std::filesystem::path& root, const std::vector<std::byte>& font) {
    check(!std::filesystem::exists(root / layout_scene), "UI layout example already exists");
    const auto ui = register_ui_document(root, layout_document);
    EngineContext engine;
    Scene scene(engine.world());
    auto source = duplicate_scene_asset(read_json(root / "Scenes/100-ui-animation.scene.json"),
                                        AssetId::generate(), scene.schema());
    for (auto& row : source["entities"])
        if (row["components"].contains("forge.ui_document")) {
            row["name"] = "UI layout - open Preview";
            row["components"]["forge.ui_document"]["document"] = ui.id;
        }
    scene.reset(source);
    scene.save(root / layout_scene);
    auto catalog = AssetCatalog::open_project(root);
    const auto record = catalog.add_scene(layout_scene);
    catalog.save(AssetCatalog::project_index(root));
    ProjectLease lease(root);
    const std::array roots{record.id};
    prepare_runtime_content_catalog(lease, roots, inspector(root, font), scene.schema());
}
void exercise_layout(const std::filesystem::path& root, const Json& source,
                     const std::vector<std::byte>& font, bool edit_source) {
    AssetId asset;
    for (const auto& row : source.at("entities"))
        if (row.at("components").contains("forge.ui_document"))
            asset = row.at("components").at("forge.ui_document").at("document").get<AssetId>();
    check(bool(asset), "UI layout Scene has no assigned document");
    const auto file = ProjectTextSource::path(root, layout_document);
    auto text = ProjectTextSource::read(file);
    check(text.has_value(), "UI layout example source missing");
    EngineContext engine(WorldRole::Runtime, false, {ui_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    const auto ui = std::static_pointer_cast<UiRuntime>(engine.services().ui());
    UiTestRenderer renderer;
    {
        UiPresenter presenter(renderer, root, font);
        if (edit_source) {
            const auto initial =
                set_ui_markup_layout(*text, by_id(*text, "panel"), {150, 180, 250, 190});
            const std::array group{
                UiMarkupLayoutEdit{by_id(initial, "panel"), {175, 190, 250, 190}},
                UiMarkupLayoutEdit{by_id(initial, "layout-b"), {455, 150, 220, 140}},
                UiMarkupLayoutEdit{by_id(initial, "layout-c"), {745, 230, 220, 140}}};
            auto candidate = set_ui_markup_layouts(initial, group);
            const auto panel_index = by_id(candidate, "panel");
            const auto closing = inspect_ui_markup(candidate).at(panel_index).closing.begin;
            candidate.insert(closing, "<label id='clone-label' for='clone-entry' "
                                      "style='pointer-events:auto;'>Name</label>"
                                      "<input id='clone-entry' type='text' value='sample' "
                                      "style='pointer-events:auto;'/>");
            candidate = duplicate_ui_markup_element(candidate, panel_index, "copied");
            candidate = set_ui_markup_layout(candidate, by_id(candidate, "copied-panel"),
                                             {20, 330, 250, 190});
            const auto copied_closing =
                inspect_ui_markup(candidate).at(by_id(candidate, "copied-panel")).closing.begin;
            candidate.insert(copied_closing,
                             "<div id='field-group' style='position:absolute;left:8px;top:70px;"
                             "width:220px;height:96px;pointer-events:auto;"
                             "background-color:#14283c;'></div>");
            const std::array fields{by_id(candidate, "copied-clone-entry"),
                                    by_id(candidate, "copied-clone-label")};
            candidate =
                reparent_ui_markup_elements(candidate, fields, by_id(candidate, "field-group"));
            candidate = set_ui_markup_style(candidate, by_id(candidate, "field-group"),
                                            "background-color", "#253f59");
            candidate = set_ui_markup_style(candidate, by_id(candidate, "field-group"), "height",
                                            format_ui_style_number({50, UiStyleUnit::Percent}));
            const std::array styled_fields{by_id(candidate, "copied-clone-entry"),
                                           by_id(candidate, "copied-clone-label")};
            candidate = set_ui_markup_styles(
                candidate, styled_fields, "background-color",
                format_ui_style_color({51.f / 255, 68.f / 255, 85.f / 255, 128.f / 255}));
            candidate =
                set_ui_markup_styles(candidate, styled_fields, "width",
                                     format_ui_style_number({55, UiStyleUnit::DensityPixels}));
            candidate = set_ui_markup_classes(candidate, styled_fields, "shared-widget");
            const auto nodes = inspect_ui_markup(candidate);
            const auto body = static_cast<std::size_t>(
                std::ranges::find(nodes, "body", &UiMarkupElement::tag) - nodes.begin());
            auto controls = insert_ui_markup_control(candidate, body, "container");
            candidate = set_ui_markup_attribute(controls.source, controls.element, "id",
                                                "created-controls");
            candidate = set_ui_markup_attribute(candidate, controls.element, "style",
                                                "position:absolute;left:520px;top:20px;width:220px;"
                                                "height:250px;pointer-events:auto;");
            for (const auto& [kind, value, id] : std::array<std::array<const char*, 3>, 6>{
                     {{"checkbox", "yes", "created-check"},
                      {"radio", "red", "created-radio"},
                      {"textarea", "Two\nrows", "created-area"},
                      {"select", "First", "created-select"},
                      {"progress", "", "created-progress"},
                      {"label", "Native label", "created-label"}}}) {
                const auto control = insert_ui_markup_control(
                    candidate, by_id(candidate, "created-controls"), kind, value);
                candidate =
                    set_ui_markup_attribute(control.source, control.element, "id", std::string(id));
                candidate = set_ui_markup_attribute(
                    candidate, control.element, "style",
                    "display:block;width:150px;height:32px;pointer-events:auto;");
                if (std::string_view(kind) == "select") {
                    const auto option =
                        insert_ui_markup_control(candidate, control.element, "option", "Second");
                    candidate = option.source;
                }
            }
            candidate = set_ui_markup_attribute(candidate, by_id(candidate, "created-label"), "for",
                                                "created-check");
            check(presenter.preview_document({asset}, layout_document, candidate, 800, 600),
                  "Gallery draft preview did not load");
            const auto view = presenter.preview_layout();
            check(view.at("elements").at(0).at("editable").get<bool>() &&
                      view.at("elements").at(0).at("bounds") == Json::array({175, 190, 250, 190}),
                  "Gallery draft preview lost pixel layout eligibility");
            check(ProjectTextSource::read(file) == text && !presenter.asset_snapshot(),
                  "Gallery preview published source or catalog observations");
            const auto style_file = ProjectTextSource::path(root, "Assets/UI/Layout/layout.rcss");
            const auto saved_style = ProjectTextSource::read(style_file);
            check(saved_style.has_value(), "Gallery linked stylesheet missing");
            auto styled = insert_ui_stylesheet_rule(*saved_style, ".shared-widget");
            const auto rule = inspect_ui_stylesheet(styled).size() - 1;
            styled = set_ui_stylesheet_property(styled, rule, "color", "#6ac98a");
            styled = set_ui_stylesheet_property(styled, rule, "opacity",
                                                format_ui_style_number({.5, UiStyleUnit::Number}));
            styled = duplicate_ui_stylesheet_rule(styled, rule);
            styled = set_ui_stylesheet_property(styled, rule + 1, "color", "#aabbcc");
            {
                ProjectLease lease(root);
                ProjectTextSource::validate(file, candidate);
                ProjectTextSource::write_validated(file, text, candidate);
                ProjectTextSource::validate(style_file, styled);
                ProjectTextSource::write_validated(style_file, saved_style, styled);
            }
            presenter.reset("style-copy-cascade", 1);
            check(presenter.preview_document({asset}, layout_document, candidate, 800, 600),
                  "Copied class stylesheet preview did not load");
            for (const auto* id : {"copied-clone-entry", "copied-clone-label"}) {
                auto* node = Rml::GetContext(0)->GetDocument(0)->GetElementById(id);
                const auto* color = node ? node->GetProperty("color") : nullptr;
                check(color && color->Get<Rml::Colourb>() == Rml::Colourb(170, 187, 204, 255),
                      "Native copied rule did not win the shared class cascade");
            }
            const auto reordered = move_ui_stylesheet_rule(styled, rule + 1, -1);
            {
                ProjectLease lease(root);
                ProjectTextSource::validate(style_file, reordered);
                ProjectTextSource::write_validated(style_file, styled, reordered);
            }
            text = candidate;
        }
        presenter.reset("layout-runtime", 1);
        check(presenter.accept(ui->snapshot(scene, "layout-runtime", 1, 0, false)),
              "Saved layout example did not load in runtime UI");
        presenter.update(0, 800, 600);
        presenter.render();
        auto* panel = Rml::GetContext(0)->GetDocument(0)->GetElementById("panel");
        const auto position = panel->GetAbsoluteOffset(Rml::BoxArea::Border);
        const auto size = panel->GetBox().GetSize(Rml::BoxArea::Border);
        check(position.x == 175 && position.y == 190 && size.x == 250 && size.y == 190 &&
                  renderer.draws,
              "Saved/exported UI did not retain the previewed layout");
        for (const auto& [id, expected] :
             std::array{std::pair{"layout-b", std::array{455.f, 150.f}},
                        std::pair{"layout-c", std::array{745.f, 230.f}}}) {
            auto* sibling = Rml::GetContext(0)->GetDocument(0)->GetElementById(id);
            check(sibling != nullptr, "Saved/exported UI lost a grouped sibling");
            const auto offset = sibling->GetAbsoluteOffset(Rml::BoxArea::Border);
            check(offset.x == expected[0] && offset.y == expected[1],
                  "Saved/exported sibling did not retain the atomic group change");
        }
        auto* document = Rml::GetContext(0)->GetDocument(0);
        auto* original_input = document->GetElementById("clone-entry");
        auto* copied_input = document->GetElementById("copied-clone-entry");
        auto* copied_label = document->GetElementById("copied-clone-label");
        check(original_input && copied_input && copied_label &&
                  copied_label->GetAttribute<Rml::String>("for", "") == "copied-clone-entry",
              "Saved/exported clone lost original/new IDs or its internal label target");
        auto* field_group = document->GetElementById("field-group");
        check(field_group && copied_input->GetParentNode() == field_group &&
                  copied_label->GetParentNode() == field_group &&
                  original_input->GetParentNode() == document->GetElementById("panel"),
              "Saved/exported source hierarchy lost moved fields or changed the original parent");
        const auto* background = field_group->GetProperty("background-color");
        check(background && background->Get<Rml::Colourb>() == Rml::Colourb(37, 63, 89, 255),
              "Saved/exported inline style lost its authored native color");
        const auto* height = field_group->GetProperty("height");
        check(height && height->unit == Rml::Unit::PERCENT && height->Get<float>() == 50,
              "Saved/exported typed percentage lost its native value or unit");
        const auto* label_color = copied_label->GetProperty("color");
        check(label_color && label_color->Get<Rml::Colourb>() == Rml::Colourb(106, 201, 138, 255),
              "Saved/exported linked stylesheet lost its authored selector/declaration color");
        for (auto* node : {copied_input, copied_label}) {
            check(node->GetAttribute<Rml::String>("class", "") == "shared-widget",
                  "Saved/exported group class assignment lost its literal class list");
            const auto* fill = node->GetProperty("background-color");
            const auto* color = node->GetProperty("color");
            check(fill && fill->Get<Rml::Colourb>() == Rml::Colourb(51, 68, 85, 128) && color &&
                      color->Get<Rml::Colourb>() == Rml::Colourb(106, 201, 138, 255),
                  "Saved/exported group styles or reordered class cascade changed native values");
            const auto* width = node->GetProperty("width");
            const auto* opacity = node->GetProperty("opacity");
            check(width && width->unit == Rml::Unit::DP && width->Get<float>() == 55 && opacity &&
                      opacity->unit == Rml::Unit::NUMBER && opacity->Get<float>() == .5f,
                  "Saved/exported typed density-pixel or unitless values changed native units");
        }
        auto* check_input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(
            document->GetElementById("created-check"));
        auto* radio = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(
            document->GetElementById("created-radio"));
        auto* area = rmlui_dynamic_cast<Rml::ElementFormControlTextArea*>(
            document->GetElementById("created-area"));
        auto* dropdown = rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(
            document->GetElementById("created-select"));
        auto* progress =
            rmlui_dynamic_cast<Rml::ElementProgress*>(document->GetElementById("created-progress"));
        check(check_input && check_input->GetAttribute<Rml::String>("type", "") == "checkbox" &&
                  check_input->GetValue() == "yes" && !check_input->HasAttribute("checked") &&
                  radio && radio->GetAttribute<Rml::String>("type", "") == "radio" &&
                  radio->GetValue() == "red" && !radio->HasAttribute("checked") && area &&
                  area->GetValue() == "Two\nrows" && dropdown && dropdown->GetNumOptions() == 2 &&
                  dropdown->GetSelection() == 0 && dropdown->GetValue() == "First" && progress &&
                  progress->GetValue() == 0 && progress->GetMax() == 1,
              "Saved/exported authored controls lost native type, initial value, selected option "
              "or progress");
        auto* new_label = document->GetElementById("created-label");
        check(new_label && new_label->GetAttribute<Rml::String>("for", "") == "created-check",
              "Created label lost its explicit target");
        const auto new_label_position = new_label->GetAbsoluteOffset(Rml::BoxArea::Border);
        const auto new_label_size = new_label->GetBox().GetSize(Rml::BoxArea::Border);
        check(new_label_size.x > 0 && new_label_size.y > 0, "Created label has no native hit box");
        presenter.mouse_move(int(new_label_position.x + new_label_size.x / 2),
                             int(new_label_position.y + new_label_size.y / 2), 0);
        presenter.mouse_button(0, true, 0);
        presenter.mouse_button(0, false, 0);
        check(check_input->HasAttribute("checked"),
              "Native label input did not toggle its created checkbox");
        const auto label_position = copied_label->GetAbsoluteOffset(Rml::BoxArea::Border);
        const auto label_size = copied_label->GetBox().GetSize(Rml::BoxArea::Border);
        check(label_size.x > 0 && label_size.y > 0, "Cloned runtime label has no native hit box");
        presenter.mouse_move(int(label_position.x + label_size.x / 2),
                             int(label_position.y + label_size.y / 2), 0);
        presenter.mouse_button(0, true, 0);
        presenter.mouse_button(0, false, 0);
        check(Rml::GetContext(0)->GetFocusElement() == copied_input,
              "Actual runtime label input did not focus its copied target");
    }
    check(scene.snapshot() == source && renderer.geometry.empty() && renderer.textures.empty(),
          "Layout workflow changed Scene data or leaked native resources");
}
} // namespace
namespace {
void author_forms(const std::filesystem::path& root, const std::vector<std::byte>& font) {
    check(!std::filesystem::exists(root / forms_scene), "Form example already exists");
    const auto document = register_ui_document(root, forms_document);
    EngineContext engine;
    Scene scene(engine.world());
    auto source =
        duplicate_scene_asset(read_json(root / layout_scene), AssetId::generate(), scene.schema());
    for (auto& row : source["entities"])
        if (row["components"].contains("forge.ui_document")) {
            row["name"] = "Form controls - capture Game input";
            row["components"]["forge.ui_document"]["document"] = document.id;
        }
    scene.reset(source);
    scene.save(root / forms_scene);
    auto catalog = AssetCatalog::open_project(root);
    const auto record = catalog.add_scene(forms_scene);
    catalog.save(AssetCatalog::project_index(root));
    ProjectLease lease(root);
    const std::array roots{record.id};
    prepare_runtime_content_catalog(lease, roots, inspector(root, font), scene.schema());
}
void exercise_forms(const std::filesystem::path& root, const Json& source,
                    const std::vector<std::byte>& font) {
    EngineContext engine(WorldRole::Runtime, false, {ui_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    const auto authored = scene.snapshot();
    const auto ui = std::static_pointer_cast<UiRuntime>(engine.services().ui());
    UiTestRenderer renderer;
    {
        UiPresenter presenter(renderer, root, font);
        presenter.reset("form-gallery", 1);
        auto state = ui->snapshot(scene, "form-gallery", 1, 1, true);
        check(presenter.accept(state), presenter.diagnostic().c_str());
        presenter.update(0, 800, 900);
        const auto instance = state.at("documents")[0].at("instance").get<std::string>();
        const auto initial = presenter.form_controls(instance);
        const auto revision = initial.at("presentation_revision").get<std::uint64_t>();
        const auto control = [&](const Json& observation, const char* id) {
            for (const auto& row : observation.at("controls"))
                if (row.at("id") == id)
                    return row;
            throw std::runtime_error("Gallery form control missing");
        };
        check(initial.at("controls").size() == 6 && !initial.at("truncated").get<bool>() &&
                  control(initial, "music").at("checked") == true &&
                  control(initial, "casual").at("checked") == true &&
                  control(initial, "quality").at("value") == "balanced" &&
                  control(initial, "download").at("read_only") == true &&
                  std::abs(control(initial, "download").at("value").get<double>() - .35) < .0001 &&
                  control(initial, "locked").at("editable") == false,
              "Gallery form defaults differ from saved native controls");
        presenter.form_control_action(instance, "music", revision, "check", false);
        presenter.form_control_action(instance, "expert", revision, "choose");
        const auto page = presenter.form_options(instance, "quality");
        check(page.at("options").size() == 4 && page.at("options")[3].at("enabled") == false,
              "Gallery dropdown lost saved options or disabled state");
        presenter.form_control_action(instance, "quality", revision, "select", false, 1,
                                      page.at("options")[1].at("state"));
        const auto changed = presenter.form_controls(instance);
        check(control(changed, "music").at("checked") == false &&
                  control(changed, "casual").at("checked") == false &&
                  control(changed, "expert").at("checked") == true &&
                  control(changed, "quality").at("value") == "detailed",
              "Gallery forms did not retain native checkbox/radio/dropdown actions");
        presenter.render();
        check(renderer.draws > 0 && scene.snapshot() == authored,
              "Gallery form presentation changed authored Scene or drew nothing");
    }
    check(renderer.geometry.empty(), "Gallery forms retained retired geometry");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4 || (argc == 5 && (std::string_view(argv[4]) == "--layout-only" ||
                                          std::string_view(argv[4]) == "--forms-only")),
              "Need Gallery, scratch root and font; or --author/--author-layout Gallery font");
        const auto font = asset_detail::read_bytes(argv[3], 4 * 1024 * 1024);
        if (std::string_view(argv[1]) == "--author") {
            author(argv[2], font);
            return 0;
        }
        if (std::string_view(argv[1]) == "--author-layout") {
            author_layout(argv[2], font);
            return 0;
        }
        if (std::string_view(argv[1]) == "--author-forms") {
            author_forms(argv[2], font);
            return 0;
        }
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(scratch);
        const auto root = scratch / "source";
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const bool forms_only = argc == 5 && std::string_view(argv[4]) == "--forms-only";
        const bool layout_only = argc == 5 && !forms_only;
        const auto source = read_json(root / (forms_only    ? forms_scene
                                              : layout_only ? layout_scene
                                                            : scene_path));
        if (!forms_only && !layout_only) {
            const auto saved = ProjectTextSource::read(root / document_path);
            check(saved.has_value(), "Source example document missing");
            auto created = insert_ui_markup_control(*saved, by_id(*saved, "panel"), "input-button",
                                                    "Step & <native>");
            auto text =
                set_ui_markup_attribute(created.source, created.element, "id", "native-press");
            text = set_ui_markup_attribute(text, created.element, "data-event-click",
                                           "command('Step')");
            created = insert_ui_markup_control(text, by_id(text, "panel"), "range", "1");
            text = set_ui_markup_attribute(created.source, created.element, "id", "native-range");
            text = set_ui_markup_attribute(text, created.element, "data-event-change",
                                           "command('Step')");
            atomic_write(root / document_path, text);
            const auto published = register_ui_document(root, document_path);
            for (const auto& row : source.at("entities"))
                if (row.at("components").contains("forge.ui_document"))
                    check(row.at("components")
                                  .at("forge.ui_document")
                                  .at("document")
                                  .get<AssetId>() == published.id,
                          "Cloned source Input button changed the existing document AssetId");
        }
        if (forms_only)
            exercise_forms(root, source, font);
        else if (layout_only)
            exercise_layout(root, source, font, true);
        else
            exercise(root, source, font, true);
        const std::array roots{source.at("asset_id").get<AssetId>()};
        {
            ProjectLease lease(root);
            prepare_runtime_content_catalog(lease, roots, inspector(root, font));
        }
        package_runtime_content(root, scratch / "export", roots, {"windows", "d3d12"}, {}, {}, {},
                                inspector(root, font));
        const auto moved = scratch / "relocated";
        std::filesystem::rename(scratch / "export", moved);
        const auto catalog = open_runtime_content(moved, {"windows", "d3d12"});
        check(catalog.records().size() == 3, "UI source package lost/duplicated its closure");
        std::filesystem::rename(root, scratch / "source-offline");
        const auto exported = read_json(moved / catalog.records().at(roots[0]).source);
        if (forms_only)
            exercise_forms(moved, exported, font);
        else if (layout_only)
            exercise_layout(moved, exported, font, false);
        else
            exercise(moved, exported, font, false);
        std::cout << "UI source structured authoring, native bindings/input/last-good, linked "
                     "resources and source-hidden relocated content passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
