#include "asset_bytes.hpp"
#include "project_text_source.hpp"
#include "runtime_package.hpp"
#include "ui_markup_authoring.hpp"
#include "ui_test_renderer.hpp"
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi/Core/Elements/ElementFormControlTextArea.h>
#include <RmlUi/Core/Elements/ElementProgress.h>
#include <RmlUi/Core/StyleTypes.h>
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
        if (!presenter.accept(snapshot))
            throw std::runtime_error(presenter.diagnostic());
        presenter.update(0, 800, 600);
        presenter.render();
        auto* document = Rml::GetContext(0)->GetDocument(0);
        check(document->GetElementById("message")->GetInnerRML().find("&lt;your own HUD&gt;") !=
                  std::string::npos,
              "Escaped source text was parsed as child markup");
        auto* typography = document->GetElementById("native-typography");
        check(typography && typography->GetProperty<int>("font-weight") == 700 &&
                  typography->GetProperty<Rml::Style::FontStyle>("font-style") ==
                      Rml::Style::FontStyle::Italic &&
                  typography->GetProperty<Rml::Style::TextAlign>("text-align") ==
                      Rml::Style::TextAlign::Center &&
                  typography->GetProperty<Rml::Style::TextDecoration>("text-decoration") ==
                      Rml::Style::TextDecoration::Underline &&
                  typography->GetProperty<Rml::Style::TextTransform>("text-transform") ==
                      Rml::Style::TextTransform::Uppercase &&
                  typography->GetProperty<Rml::Style::WhiteSpace>("white-space") ==
                      Rml::Style::WhiteSpace::Prewrap &&
                  typography->GetProperty<float>("line-height") == 120 &&
                  typography->GetProperty<float>("letter-spacing") == .5f,
              "Saved/source-hidden typography literals lost actual native property values");
        for (const auto& [id, size] : std::array<std::pair<const char*, Rml::Vector2f>, 4>{
                 {{"native-image", {32, 32}},
                  {"native-sprite", {32, 32}},
                  {"native-image-css", {24, 12}},
                  {"native-image-intrinsic", {2, 1}}}}) {
            auto* image = document->GetElementById(id);
            check(image && image->GetBox().GetSize(Rml::BoxArea::Content) == size &&
                      image->GetProperty<Rml::Colourb>("image-color") ==
                          Rml::Colourb(128, 255, 64, 255),
                  "Saved/source-hidden image lost native crop/sprite precedence/aspect/CSS "
                  "sizing/tint");
        }
        const auto size = [&](const char* id) {
            auto* element = document->GetElementById(id);
            check(element, "Native flex fixture missing");
            return element->GetBox().GetSize(Rml::BoxArea::Content);
        };
        const auto relative = [&](const char* child, const char* parent) {
            return document->GetElementById(child)->GetAbsoluteOffset(Rml::BoxArea::Content) -
                   document->GetElementById(parent)->GetAbsoluteOffset(Rml::BoxArea::Content);
        };
        check(size("native-flex-row-0") == Rml::Vector2f(120, 20) &&
                  size("native-flex-row-1") == Rml::Vector2f(170, 20) &&
                  relative("native-flex-row-1", "native-flex-row") == Rml::Vector2f(130, 40),
              "Native flex growth, min/max clamp, gap or cross-axis alignment differs");
        check(size("native-flex-column-0") == Rml::Vector2f(100, 60) &&
                  size("native-flex-column-1") == Rml::Vector2f(100, 60) &&
                  relative("native-flex-column-1", "native-flex-column") == Rml::Vector2f(0, 80),
              "Native column basis/growth/row gap differs");
        const auto wrap_size = size("native-flex-wrap");
        const auto wrap_second = relative("native-flex-wrap-1", "native-flex-wrap");
        const auto wrap_third = relative("native-flex-wrap-2", "native-flex-wrap");
        // Leave room above the exact two-child threshold at native percentage precision.
        check(wrap_size == Rml::Vector2f(220, 100) && wrap_second == Rml::Vector2f(110, 0) &&
                  wrap_third == Rml::Vector2f(0, 30),
              "Native flex wrap/line spacing differs");
        // Native percentage arithmetic carries float rounding below a thousandth of a pixel.
        const auto near = [](Rml::Vector2f actual, Rml::Vector2f expected) {
            return std::abs(actual.x - expected.x) < .001f &&
                   std::abs(actual.y - expected.y) < .001f;
        };
        for (const auto* id : {"native-box-border", "native-box-content"}) {
            auto* element = document->GetElementById(id);
            check(element, "Native box style fixture missing");
            const bool border = std::string_view(id) == "native-box-border";
            const auto border_size = element->GetBox().GetSize(Rml::BoxArea::Border);
            const auto content_size = size(id);
            check(
                near(border_size, border ? Rml::Vector2f(200, 100) : Rml::Vector2f(254, 114)) &&
                    near(content_size, border ? Rml::Vector2f(146, 86) : Rml::Vector2f(200, 100)) &&
                    std::abs(element->GetBox().GetEdge(Rml::BoxArea::Padding, Rml::BoxEdge::Right) -
                             40) < .001f &&
                    element->GetBox().GetEdge(Rml::BoxArea::Margin, Rml::BoxEdge::Left) == -3 &&
                    element->GetProperty<Rml::Colourb>("border-top-color") ==
                        Rml::Colourb(204, 136, 68, 255) &&
                    element->GetProperty<float>("border-top-left-radius") == 4 &&
                    element->GetProperty<Rml::Style::Overflow>("overflow-x") ==
                        (border ? Rml::Style::Overflow::Hidden : Rml::Style::Overflow::Visible),
                "Native box sizing, percentage padding, negative margin, border or overflow "
                "differs");
        }
        presenter.update(0, 400, 600);
        check(size("native-flex-wrap") == Rml::Vector2f(110, 100) &&
                  relative("native-flex-wrap-1", "native-flex-wrap") == Rml::Vector2f(0, 30) &&
                  relative("native-flex-wrap-2", "native-flex-wrap") == Rml::Vector2f(0, 60),
              "Percentage flex container did not reflow at a narrower viewport");
        check(near(size("native-box-border"), Rml::Vector2f(166, 86)) &&
                  std::abs(document->GetElementById("native-box-border")
                               ->GetBox()
                               .GetEdge(Rml::BoxArea::Padding, Rml::BoxEdge::Right) -
                           20) < .001f,
              "Native percentage padding did not follow the narrower containing width");
        presenter.update(0, 800, 600);
        const auto initial_state = document->GetElementById("state")->GetInnerRML();
        if (initial_state != "Tick 41 / paused 1")
            throw std::runtime_error("Structured text binding result: " + initial_state);
        check(renderer.draws && !renderer.textures.empty() && presenter.asset_snapshot() &&
                  presenter.asset_snapshot()->sources.size() == 4,
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
        auto* authored_text = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(
            document->GetElementById("native-text"));
        auto* authored_area = rmlui_dynamic_cast<Rml::ElementFormControlTextArea*>(
            document->GetElementById("native-area"));
        auto* attribute_area = rmlui_dynamic_cast<Rml::ElementFormControlTextArea*>(
            document->GetElementById("native-attribute-area"));
        check(authored_text && authored_area, "Authored native text fields missing");
        check(attribute_area && attribute_area->GetValue() == "Initial & <area>",
              "Empty-body textarea initial value attribute did not decode literal text");
        const Json text_facts{
            {"text", authored_text->GetValue()},
            {"size", authored_text->GetAttribute<int>("size", 0)},
            {"length", authored_text->GetAttribute<int>("maxlength", 0)},
            {"placeholder", authored_text->GetAttribute<Rml::String>("placeholder", "")},
            {"area", authored_area->GetValue()},
            {"rows", authored_area->GetNumRows()},
            {"cols", authored_area->GetNumColumns()},
            {"area_length", authored_area->GetMaxLength()},
            {"wrap", authored_area->GetWordWrap()}};
        const Json expected_text{{"text", "Authored & <value>"},
                                 {"size", 12},
                                 {"length", 80},
                                 {"placeholder", "Name & <hint>"},
                                 {"area", "Initial &amp; area"},
                                 {"rows", 4},
                                 {"cols", 24},
                                 {"area_length", 80},
                                 {"wrap", false}};
        if (text_facts != expected_text)
            throw std::runtime_error("Saved/exported native text facts: " + text_facts.dump());
        document->GetElementById("native-label")->Click();
        check(document->GetContext()->GetFocusElement() == authored_text,
              "Authored label target did not focus its native text field");
        bool password_redacted = false;
        const auto authored_fields = presenter.text_fields(instance);
        for (const auto& field : authored_fields.at("fields"))
            if (field.at("id") == "native-password")
                password_redacted = field.at("kind") == "password" &&
                                    field.at("value_available") == false &&
                                    !field.contains("value");
        check(password_redacted, "Source password settings widened native password observations");
        const auto forms = presenter.form_controls(instance);
        Json authored_flag, authored_progress;
        for (const auto& row : forms.at("controls")) {
            if (row.at("id") == "native-check")
                authored_flag = row;
            if (row.at("id") == "native-progress")
                authored_progress = row;
        }
        check(authored_flag.is_object() && authored_flag.at("checked") == true &&
                  authored_flag.at("enabled") == false && authored_flag.at("editable") == false &&
                  authored_progress.is_object() && authored_progress.at("value") == 1 &&
                  authored_progress.at("max") == 1 && authored_progress.at("read_only") == true,
              "Saved/exported authored properties lost native flag/clamp/fallback behavior");
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
        check(ranges.at("ranges").size() == 1 && ranges.at("ranges")[0].at("value") == "4.000000",
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
        if (!presenter.accept(snapshot))
            throw std::runtime_error(presenter.diagnostic());
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
            if (!presenter.reload())
                throw std::runtime_error(presenter.diagnostic());
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
        if (!presenter.accept(state))
            throw std::runtime_error(presenter.diagnostic());
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
            // Exact pinned dependency font data retains its original Lato ownership/OFL notice.
            // Use the real face through ordinary project resources, not a synthetic style alias.
            const auto face_path =
                std::filesystem::path("Assets/UI/SourceAuthoring/Typography.ttf");
            std::filesystem::copy_file(FORGE_TEST_UI_BOLD_ITALIC_FONT, root / face_path,
                                       std::filesystem::copy_options::overwrite_existing);
            const auto style = ProjectTextSource::read(root / stylesheet_path);
            check(style.has_value(), "Source typography stylesheet missing");
            atomic_write(root / stylesheet_path,
                         "@font-face {font-family:Lato;src:\"" + path_utf8(face_path) +
                             "\";font-style:italic;font-weight:700;}\n" + *style);
            {
                // Original two-pixel white TGA fixture; ordinary import owns decoding/resources.
                std::array<unsigned char, 24> image{};
                image[2] = 2;
                image[12] = 2;
                image[14] = 1;
                image[16] = 24;
                image[17] = 32;
                std::fill(image.begin() + 18, image.end(), 255);
                std::ofstream pixels(root / "Assets/UI/SourceAuthoring/Badge.tga",
                                     std::ios::binary);
                pixels.write(reinterpret_cast<const char*>(image.data()), image.size());
                check(bool(pixels), "Image fixture write failed");
            }
            const auto with_face = ProjectTextSource::read(root / stylesheet_path);
            check(with_face.has_value(), "Image fixture stylesheet missing");
            atomic_write(root / stylesheet_path,
                         "@spritesheet source-images {src:Badge.tga;source-dot:0px 0px 1px "
                         "1px;resolution:1x;}\n" +
                             *with_face);
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
            text = set_ui_markup_control_properties(text, created.element,
                                                    {{"min", -2.0}, {"max", 8.0}, {"step", 2.0}});
            created = insert_ui_markup_control(text, by_id(text, "panel"), "checkbox", "yes");
            text = set_ui_markup_attribute(created.source, created.element, "id", "native-check");
            text = set_ui_markup_control_properties(text, created.element,
                                                    {{"checked", true}, {"disabled", true}});
            created = insert_ui_markup_control(text, by_id(text, "panel"), "progress");
            text =
                set_ui_markup_attribute(created.source, created.element, "id", "native-progress");
            text = set_ui_markup_control_properties(text, created.element,
                                                    {{"value", 20.0}, {"max", 0.0}});
            created = insert_ui_markup_control(text, by_id(text, "panel"), "text-input", "Initial");
            text = set_ui_markup_attribute(created.source, created.element, "id", "native-text");
            text = set_ui_markup_control_properties(text, created.element,
                                                    {{"value", std::string("Authored & <value>")},
                                                     {"placeholder", std::string("Name & <hint>")},
                                                     {"maxlength", 80.0},
                                                     {"size", 12.0}});
            created =
                insert_ui_markup_control(text, by_id(text, "panel"), "textarea", "Initial & area");
            text = set_ui_markup_attribute(created.source, created.element, "id", "native-area");
            text = set_ui_markup_control_properties(text, created.element,
                                                    {{"rows", 4.0},
                                                     {"cols", 24.0},
                                                     {"wrap", std::string("nowrap")},
                                                     {"maxlength", 80.0}});
            created = insert_ui_markup_control(text, by_id(text, "panel"), "label", "Name");
            text = set_ui_markup_attribute(created.source, created.element, "id", "native-label");
            text = set_ui_markup_control_properties(text, created.element,
                                                    {{"for", std::string("native-text")}});
            created = insert_ui_markup_control(text, by_id(text, "panel"), "password-input",
                                               "Source only");
            text =
                set_ui_markup_attribute(created.source, created.element, "id", "native-password");
            text = set_ui_markup_control_properties(text, created.element,
                                                    {{"maxlength", 80.0}, {"size", 12.0}});
            created = insert_ui_markup_control(text, by_id(text, "panel"), "textarea");
            text = set_ui_markup_attribute(created.source, created.element, "id",
                                           "native-attribute-area");
            text = set_ui_markup_attribute(text, created.element, "value", "Initial & <area>");
            created = insert_ui_markup_control(text, by_id(text, "panel"), "paragraph",
                                               "Styled & native");
            text =
                set_ui_markup_attribute(created.source, created.element, "id", "native-typography");
            for (const auto& [name, value] : std::array<std::pair<const char*, const char*>, 6>{
                     {{"font-weight", "bold"},
                      {"font-style", "italic"},
                      {"text-align", "center"},
                      {"text-decoration", "underline"},
                      {"text-transform", "uppercase"},
                      {"white-space", "pre-wrap"}}})
                text = set_ui_markup_style(text, created.element, name, value);
            text = set_ui_markup_style(
                text, created.element, "line-height",
                format_ui_style_literal_number("line-height", {120, UiStyleUnit::Percent}));
            text = set_ui_markup_style(
                text, created.element, "letter-spacing",
                format_ui_style_literal_number("letter-spacing", {.5, UiStyleUnit::DensityPixels}));
            for (const auto* id :
                 {"native-image", "native-sprite", "native-image-css", "native-image-intrinsic"}) {
                created = insert_ui_markup_control(text, by_id(text, "panel"), "image");
                text = set_ui_markup_attribute(created.source, created.element, "id", id);
                text = set_ui_markup_control_properties(text, created.element,
                                                        {{"src", std::string("Badge.tga")}});
                if (std::string_view(id) != "native-image-intrinsic") {
                    const auto crop =
                        std::string_view(id) == "native-image" ? "0 0 1 1" : "0 0 2 1";
                    text = set_ui_markup_control_properties(
                        text, created.element, {{"rect", std::string(crop)}, {"width", 32.0}});
                }
                if (std::string_view(id) == "native-sprite")
                    text = set_ui_markup_control_properties(
                        text, created.element, {{"sprite", std::string("source-dot")}});
                if (std::string_view(id) == "native-image-css") {
                    text =
                        set_ui_markup_control_properties(text, created.element, {{"height", 16.0}});
                    text = set_ui_markup_style(text, created.element, "width", "24px");
                    text = set_ui_markup_style(text, created.element, "height", "12px");
                }
                text = set_ui_markup_style(text, created.element, "image-color", "#80ff40ff");
            }
            for (const auto* layout : {"row", "column", "wrap"}) {
                const auto id = "native-flex-" + std::string(layout);
                const bool column = std::string_view(layout) == "column";
                const bool wrap = std::string_view(layout) == "wrap";
                created = insert_ui_markup_control(text, by_id(text, "canvas"), "container");
                text = set_ui_markup_attribute(created.source, created.element, "id", id);
                for (const auto& [name, value] :
                     std::initializer_list<std::pair<const char*, const char*>>{
                         {"display", "flex"},
                         {"position", "absolute"},
                         {"left", "0px"},
                         {"top", "0px"},
                         {"width", column ? "100px"
                                   : wrap ? "27.5%"
                                          : "300px"},
                         {"height", column ? "140px" : "100px"},
                         {"flex-direction", column ? "column" : "row"},
                         {"flex-wrap", wrap ? "wrap" : "nowrap"},
                         {"justify-content", "flex-start"},
                         {"align-content", "flex-start"},
                         {"align-items", wrap || column ? "flex-start" : "center"},
                         {"row-gap", column ? "20px" : "10px"},
                         {"column-gap", "10px"}})
                    text = set_ui_markup_style(text, by_id(text, id), name, value);
                for (int i = 0; i < (wrap ? 3 : 2); ++i) {
                    created = insert_ui_markup_control(text, by_id(text, id), "container");
                    text = set_ui_markup_attribute(created.source, created.element, "id",
                                                   id + "-" + std::to_string(i));
                    for (const auto& [name, value] :
                         std::initializer_list<std::pair<const char*, const char*>>{
                             {"width", "100px"},
                             {"height", "20px"},
                             {"flex-basis", column ? "40px" : "100px"},
                             {"flex-grow", wrap               ? "0"
                                           : column || i == 0 ? "1"
                                                              : "2"},
                             {"flex-shrink", "0"},
                             {"align-self", "auto"},
                             {"min-width", wrap || column ? "0px" : "100px"},
                             {"max-width", !wrap && !column && i == 0 ? "120px" : "none"},
                             {"min-height", "0px"},
                             {"max-height", "none"}})
                        text = set_ui_markup_style(text, created.element, name, value);
                }
            }
            for (const auto* id : {"native-box-border", "native-box-content"}) {
                const bool border = std::string_view(id) == "native-box-border";
                created = insert_ui_markup_control(text, by_id(text, "canvas"), "container");
                text = set_ui_markup_attribute(created.source, created.element, "id", id);
                for (const auto& [name, value] :
                     std::initializer_list<std::pair<const char*, const char*>>{
                         {"position", "absolute"},
                         {"left", "400px"},
                         {"top", "200px"},
                         {"width", "200px"},
                         {"height", "100px"},
                         {"box-sizing", border ? "border-box" : "content-box"},
                         {"padding-top", "4px"},
                         {"padding-right", "5%"},
                         {"padding-bottom", "6px"},
                         {"padding-left", "10px"},
                         {"margin-left", "-3px"},
                         {"border-width", "2px"},
                         {"border-radius", "4dp"},
                         {"border-color", "#cc8844"},
                         {"overflow-x", border ? "hidden" : "visible"},
                         {"overflow-y", "hidden"}})
                    text = set_ui_markup_style(text, by_id(text, id), name, value);
            }
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
        check(catalog.records().size() == (forms_only || layout_only ? 3 : 5),
              "UI source package lost/duplicated its closure, including the typography font");
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
