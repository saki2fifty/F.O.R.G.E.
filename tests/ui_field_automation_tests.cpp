#include "asset_bytes.hpp"
#include "ui_control_access.hpp"
#include "ui_test_renderer.hpp"
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <source_location>

namespace {
using namespace forge;
using Json = nlohmann::json;
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F>
void rejects(F operation, std::source_location where = std::source_location::current()) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception&) {
        rejected = true;
    }
    if (!rejected)
        throw std::runtime_error("Invalid native field action was admitted at line " +
                                 std::to_string(where.line()));
}
Json field(const Json& state, const char* id, const char* collection = "fields") {
    for (const auto& row : state.at(collection))
        if (row.at("id") == id)
            return row;
    throw std::runtime_error(std::string("Missing native field observation: ") + id);
}
struct RemoveKey final : Rml::EventListener {
    Rml::Element* element = nullptr;
    void ProcessEvent(Rml::Event& event) override {
        event.StopImmediatePropagation();
        auto retired = element->GetParentNode()->RemoveChild(element);
    }
};
struct CountChanges final : Rml::EventListener {
    Rml::ObserverPtr<Rml::Element> element;
    int count = 0;
    explicit CountChanges(Rml::Element& source) : element(source.GetObserverPtr()) {
        source.AddEventListener(Rml::EventId::Change, this);
    }
    ~CountChanges() override {
        if (element)
            element->RemoveEventListener(Rml::EventId::Change, this);
    }
    void ProcessEvent(Rml::Event&) override { ++count; }
};
struct NativeMutation final : Rml::EventListener {
    Rml::ObserverPtr<Rml::Element> element;
    std::function<void()> mutation;
    Rml::EventId event_id;
    NativeMutation(Rml::Element& source, std::function<void()> callback,
                   Rml::EventId event = Rml::EventId::Focus)
        : element(source.GetObserverPtr()), mutation(std::move(callback)), event_id(event) {
        source.AddEventListener(event_id, this);
    }
    ~NativeMutation() override {
        if (element)
            element->RemoveEventListener(event_id, this);
    }
    void ProcessEvent(Rml::Event&) override { mutation(); }
};
void exercise(const std::filesystem::path& root, const std::vector<std::byte>& font, bool shaping) {
    std::filesystem::create_directories(root);
    std::ofstream(root / "fields.rml") << R"rml(<rml><head><style>
body {font-family:Lato;font-size:20px;} input,textarea {display:block;width:300px;height:35px;}
#hidden {visibility:hidden;}
</style></head><body><input id="text" type="text" value="old"/>
<input id="password" type="password" value="native-secret"/>
<input id="limited" type="text" value="ab" maxlength="3"/>
<textarea id="area">row</textarea><input id="disabled" type="text" value="Locked" disabled="disabled"/>
<input id="hidden" type="text" value="hidden"/>
<input id="duplicate" type="text" value="first"/><p id="duplicate">Other element</p>
<input type="text" value="anonymous"/><input id="number" type="number" value="3"/>
</body></rml>)rml";
    const auto source = asset_detail::read_bytes(root / "fields.rml", 65536);
    const auto asset = register_ui_document(root, "fields.rml");
    const auto entity = EntityId::generate();
    Json state{{"version", 1},
               {"session", "native-fields"},
               {"generation", 1u},
               {"revision", 1u},
               {"documents", Json::array({{{"entity", entity},
                                           {"asset", asset.id},
                                           {"instance", "fields"},
                                           {"visible", true},
                                           {"layer", 0u},
                                           {"model", Json::object()},
                                           {"commands", Json::array()}}})}};
    UiTestRenderer renderer;
    {
        UiPresenter presenter(renderer, root, font, {}, nullptr, shaping, true);
        presenter.reset("native-fields", 1);
        check(presenter.accept(state), presenter.diagnostic().c_str());
        presenter.update(0, 800, 700);
        const auto initial = presenter.text_fields("fields");
        const auto revision = initial.at("presentation_revision").get<std::uint64_t>();
        check(initial.at("fields").size() == 8 && !initial.at("truncated").get<bool>() &&
                  field(initial, "text").at("value") == "old" && initial.at("keys").size() == 32,
              "Native field discovery differs from the actual controls");
        check(initial.dump().find("native-secret") == std::string::npos &&
                  !field(initial, "password").at("value_available").get<bool>(),
              "Native password plaintext entered field observations");
        check(!field(initial, "duplicate").at("addressable").get<bool>() &&
                  !field(initial, "disabled").at("editable").get<bool>() &&
                  !field(initial, "hidden").at("editable").get<bool>(),
              "Ambiguous/disabled/hidden native fields were advertised as editable targets");
        const auto act = [&](const char* id, const char* action, const std::string& text = {}) {
            return presenter.text_field_action("fields", id, revision, action, text);
        };
        rejects([&] { presenter.text_fields("stale-instance"); });
        rejects([&] { presenter.text_field_action("fields", "text", revision + 1, "focus"); });
        for (const auto* id : {"missing", "number", "duplicate", "disabled", "hidden", ""})
            rejects([&] { act(id, "focus"); });
        rejects([&] { act("text", "text", "unfocused"); });
        act("text", "focus");
        rejects([&] { act("text", "invalid"); });
        rejects([&] { act("text", "left", "unexpected payload"); });
        rejects([&] { act("text", "text", std::string(ui_protocol::max_string + 1, 'x')); });
        rejects([&] { act("text", "text", "\xc0\xaf"); });
        check(field(presenter.text_fields("fields"), "text").at("value") == "old",
              "Rejected actions changed the native field");
        check(initial.at("limits").at("text_bytes") == ui_protocol::max_string &&
                  ui_protocol::max_string == 1024,
              "Native text admission/discovery differs from the selected owner");
        act("text", "select_all");
        act("text", "text",
            "a\xcc\x81"
            "b");
        act("text", "home");
        auto selected = field(act("text", "select_right"), "text");
        check(selected.at("selection") == Json{{"begin", 0}, {"end", 2}} && selected.at("value") ==
                                                                                "a\xcc\x81"
                                                                                "b",
              "Scoped Shift key split a whole character or changed its value");
        selected = field(act("text", "select_right"), "text");
        check(selected.at("selection") == Json{{"begin", 0}, {"end", 3}},
              "Scoped repeated Shift key lost its native anchor");
        selected = field(act("text", "select_left"), "text");
        check(selected.at("selection") == Json{{"begin", 0}, {"end", 2}},
              "Scoped reversed Shift key changed its native anchor");
        check(field(act("text", "backspace"), "text").at("value") == "b",
              "Scoped deletion did not consume the native whole-character selection");
        act("text", "select_all");
        act("text", "text",
            ".\xcc\x81"
            "a next");
        act("text", "home");
        check(field(act("text", "word_right"), "text").at("selection") ==
                  Json{{"begin", 2}, {"end", 2}},
              "Scoped word movement stopped inside a character");
        check(field(act("text", "word_right"), "text").at("selection") ==
                  Json{{"begin", 4}, {"end", 4}},
              "Scoped word movement replaced native grouping");
        act("text", "word_left");
        check(field(act("text", "select_word_right"), "text").at("selection") ==
                  Json{{"begin", 0}, {"end", 2}},
              "Scoped word selection split a character");
        act("text", "end");
        act("text", "select_word_left");
        check(field(act("text", "select_word_left"), "text").at("selection") ==
                  Json{{"begin", 0}, {"end", 8}},
              "Scoped reverse word selection lost its native anchor");
        check(field(act("text", "select_word_right"), "text").at("selection") ==
                  Json{{"begin", 2}, {"end", 8}},
              "Scoped reversed word selection split its moving end");
        act("text", "select_all");
        const auto maximum = std::string(ui_protocol::max_string, 'x');
        check(field(act("text", "text", maximum), "text").at("value") == maximum,
              "Native text action rejected the admitted maximum");
        act("text", "select_all");
        act("text", "text",
            "a\xcc\x81"
            "b");
        act("text", "home");
        auto moved = act("text", "right");
        check(field(moved, "text").at("selection") == Json{{"begin", 2}, {"end", 2}},
              "Named native arrow split a combining character");
        check(field(act("text", "delete"), "text").at("value") == "a\xcc\x81",
              "Named native Delete did not use the current field");
        act("text", "end");
        check(field(act("text", "backspace"), "text").at("value") == "",
              "Named native Backspace split the remaining character");
        act("text", "text", "abc");
        act("text", "left");
        presenter.key(Rml::Input::KI_LEFT, true, 0);
        rejects([&] { act("text", "text", "busy"); });
        presenter.key(Rml::Input::KI_LEFT, false, 0);
        act("limited", "focus");
        rejects([&] { act("text", "text", "wrong focus"); });
        act("limited", "end");
        check(field(act("limited", "text", "cd"), "limited").at("value") == "abc",
              "Native field tool bypassed maximum length");
        act("password", "focus");
        act("password", "select_all");
        const auto secret = act("password", "text", "replacement-secret");
        check(secret.dump().find("replacement-secret") == std::string::npos &&
                  !field(secret, "password").contains("value"),
              "Native password action echoed plaintext");
        act("area", "focus");
        act("area", "select_all");
        check(field(act("area", "text", "first\nsecond"), "area").at("value") == "first\nsecond",
              "Native textarea did not receive multiline text");
        act("area", "accept");
        act("area", "next");
        rejects([&] { act("area", "text", "focus changed"); });
        act("password", "focus");
        check(field(act("password", "previous"), "text").at("focused"),
              "Named previous navigation did not retain native focus traversal");
        auto* document = Rml::GetContext(0)->GetDocument(0);
        auto* text =
            rmlui_dynamic_cast<Rml::ElementFormControlInput*>(document->GetElementById("text"));
        check(text != nullptr, "Native test field disappeared");
        text->SetValue(std::string(ui_protocol::max_string + 1, 'x'));
        const auto large = field(presenter.text_fields("fields"), "text");
        check(!large.at("value_available").get<bool>() &&
                  large.at("value_bytes") == ui_protocol::max_string + 1 &&
                  !large.contains("value"),
              "Oversized native observation leaked or truncated field text");
        act("text", "focus");
        RemoveKey listener;
        listener.element = text;
        document->AddEventListener(Rml::EventId::Keydown, &listener, true);
        const auto removed = act("text", "word_backspace");
        document->RemoveEventListener(Rml::EventId::Keydown, &listener, true);
        check(removed.at("delivered") && !document->GetElementById("text"),
              "Native callback removal was not observed safely");
        check(presenter.reload(), presenter.diagnostic().c_str());
        presenter.update(.1, 800, 700);
        rejects([&] { act("text", "focus"); });
        const auto fresh = presenter.text_fields("fields");
        check(fresh.at("presentation_revision") != revision &&
                  field(fresh, "text").at("value") == "old",
              "Reload failed to replace the native field revision/value");
        document = Rml::GetContext(0)->GetDocument(0);
        std::string many;
        for (int i = 0; i < 65; ++i)
            many += "<input type=\"text\" id=\"field" + std::to_string(i) + "\"/>";
        document->SetInnerRML(many);
        check(presenter.text_fields("fields").at("truncated"), "Native field count was unbounded");
        rejects([&] {
            presenter.text_field_action("fields", "field0", fresh.at("presentation_revision"),
                                        "focus");
        });
        many = "<input type=\"text\" id=\"field0\"/>";
        for (int i = 0; i < 4100; ++i)
            many += "<span/>";
        document->SetInnerRML(many);
        const auto nodes = presenter.text_fields("fields");
        check(nodes.at("truncated") && nodes.at("visited_nodes") == 4096,
              "Native DOM observations exceeded their node bound");
        document->SetInnerRML(R"rml(<input id="box" type="checkbox" value="yes"/>
<input id="radio" type="radio" value="red"/><input id="private" type="password" value="secret"/>
<textarea id="area">row</textarea><select id="list"><option id="option-first" value="a" selected="selected">Choice A</option><option id="box" value="b">Choice B</option></select>
<progress id="bar" value="0.5"></progress>)rml");
        presenter.update(.2, 800, 700);
        const auto forms = scan_ui_form_controls(*document);
        const auto texts = scan_ui_text_fields(*document);
        check(forms.fields.size() == 4 && !forms.truncated && texts.fields.size() == 2 &&
                  !texts.truncated && forms.ids.at("box") == 2 &&
                  forms.ids.at("option-first") == 1 && !texts.ids.contains("option-first"),
              "Shared native traversal lost moved options/ID ambiguity or changed text-only scope");
        for (const auto& form : forms.fields) {
            const auto kind = ui_form_control_kind(form);
            check(kind == "checkbox" || kind == "radio" || kind == "select" || kind == "progress",
                  "Text/password controls entered native form observations");
            check(ui_form_control_editable(form) == (kind != "progress"),
                  "Native form helper lost enabled visibility or exposed a progress input action");
        }
        auto* private_input =
            rmlui_dynamic_cast<Rml::ElementFormControlInput*>(document->GetElementById("private"));
        check(private_input && !ui_form_control(private_input) &&
                  ui_form_control_kind(UiFormControl{private_input}) == "unsupported" &&
                  !ui_form_control_editable(UiFormControl{private_input}),
              "A changed/native password type bypassed form classification");
        auto* dropdown =
            rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(document->GetElementById("list"));
        check(dropdown && dropdown->GetNumOptions() == 2 &&
                  ui_form_option_text(*dropdown->GetOption(0), 1024) == "Choice A" &&
                  !ui_form_option_text(*dropdown->GetOption(0), 2),
              "Bounded native option text changed label bytes or silently truncated a value");
        {
            CountChanges changes(*dropdown);
            auto* context = Rml::GetContext(0);
            const auto* focus_before = context->GetFocusElement();
            const auto observed = presenter.form_controls("fields");
            const auto choices = presenter.form_options("fields", "list", 0, 1);
            const auto second_page = presenter.form_options("fields", "list", 1, 1);
            check(observed.at("controls").size() == 4 &&
                      !field(observed, "box", "controls").at("addressable").get<bool>() &&
                      !field(observed, "box", "controls").at("checked").get<bool>() &&
                      field(observed, "bar", "controls").at("value") == .5 &&
                      field(observed, "bar", "controls").at("max") == 1 &&
                      field(observed, "bar", "controls").at("read_only") &&
                      observed.dump().find("secret") == std::string::npos,
                  "Native form observations lost type/value/ID limits or exposed password text");
            check(choices.at("ready") && choices.at("total") == 2 &&
                      choices.at("next_offset") == 1 && choices.at("options").size() == 1 &&
                      choices.at("options")[0].at("value") == "a" &&
                      choices.at("options")[0].at("text") == "Choice A" &&
                      choices.at("options")[0].at("state").get<std::string>().size() == 64 &&
                      second_page.at("options")[0].at("index") == 1 &&
                      second_page.at("options")[0].at("value") == "b" &&
                      second_page.at("next_offset").is_null() && changes.count == 0 &&
                      context->GetFocusElement() == focus_before,
                  "Readonly native option pages changed focus/state or lost labels/cursors");
            rejects([&] { presenter.form_controls("other-instance"); });
            rejects([&] { presenter.form_options("fields", "private"); });
            rejects([&] { presenter.form_options("fields", "box"); });
            rejects([&] { presenter.form_options("fields", "bar"); });
            rejects([&] { presenter.form_options("fields", "list", 3, 1); });
            rejects([&] { presenter.form_options("fields", "list", 0, 0); });
            rejects([&] { presenter.form_options("fields", "list", 0, 33); });
            dropdown->SetSelection(1);
            check(changes.count == 1 && dropdown->GetSelection() == 1 &&
                      presenter.form_options("fields", "list", 0, 1).at("options")[0].at("state") ==
                          choices.at("options")[0].at("state"),
                  "Native selection did not own one Change event or option identity tracked "
                  "selection");
            auto pending = document->CreateElement("option");
            pending->SetAttribute("value", "pending");
            pending->SetAttribute("selected", "selected");
            pending->SetInnerRML("Pending choice");
            dropdown->AppendChild(std::move(pending));
            const auto pending_count = dropdown->GetNumChildren();
            const auto waiting = presenter.form_options("fields", "list");
            const auto waiting_controls = presenter.form_controls("fields");
            check(
                pending_count > 0 && dropdown->GetNumChildren() == pending_count &&
                    !waiting.at("ready").get<bool>() && waiting.at("total").is_null() &&
                    waiting.at("options").empty() && changes.count == 1 &&
                    !field(waiting_controls, "list", "controls").at("options_ready").get<bool>() &&
                    context->GetFocusElement() == focus_before,
                "Readonly native queries flushed pending dropdown children or callbacks");
            presenter.update(.3, 800, 700);
            const auto ready = presenter.form_options("fields", "list");
            check(
                ready.at("ready") && ready.at("total") == 3 && changes.count == 2 &&
                    field(presenter.form_controls("fields"), "list", "controls")
                            .at("selected_index") == 2 &&
                    changes.count == 2,
                "Native Update did not own pending selection or readonly queries repeated Change");
        }
        auto* option = dropdown->GetOption(0);
        option->SetInnerRML("<span>Fresh &amp; </span><b>safe</b>");
        check(ui_form_option_text(*option, 1024) == "Fresh & safe",
              "Native option text serialized markup instead of reading text leaves");
        option->SetInnerRML("<span>Before <b>nested</b></span> after");
        check(ui_form_option_text(*option, 1024) == "Before nested after",
              "Native option text lost the document order of mixed-depth leaves");
        option->SetInnerRML("<input type='password' value='hidden-secret'/>");
        check(!ui_form_option_text(*option, 1024),
              "Nested native password/control content entered option text observations");
        std::string dense;
        for (int i = 0; i < 257; ++i)
            dense += "<span/>";
        option->SetInnerRML(dense);
        check(!ui_form_option_text(*option, 1024),
              "Native option label traversal exceeded its node budget");
        many = "<textarea id='kept'>safe</textarea>";
        for (int i = 0; i < 65; ++i)
            many += "<input type='checkbox' id='check" + std::to_string(i) + "'/>";
        document->SetInnerRML(many);
        check(scan_ui_form_controls(*document).truncated &&
                  scan_ui_text_fields(*document).fields.size() == 1 &&
                  !presenter.text_fields("fields").at("truncated").get<bool>(),
              "The form count budget changed the existing text-input admission policy");
        document->SetInnerRML(R"rml(<input id="toggle" type="checkbox" value="yes"/>
<input id="radio-a" type="radio" name="quality" value="a"/><input id="radio-b" type="radio" name="quality" value="b"/>
<select id="choice"><option value="a" selected="selected">A</option><option value="disabled" disabled="disabled">Disabled</option><option value="hidden" style="visibility:hidden;">Hidden</option><option value="d">D</option></select>
<progress id="meter" value="0.5"></progress><input id="private" type="password" value="form-secret"/>
<input id="locked-check" type="checkbox" disabled="disabled"/><input id="hidden-check" type="checkbox" style="visibility:hidden;"/>)rml");
        presenter.update(.4, 800, 700);
        const auto forms_revision =
            presenter.form_controls("fields").at("presentation_revision").get<std::uint64_t>();
        const auto form = [&](const char* id, const char* action, bool checked = false,
                              std::size_t index = 0, const std::string& state = std::string{}) {
            return presenter.form_control_action("fields", id, forms_revision, action, checked,
                                                 index, state);
        };
        auto* toggle = document->GetElementById("toggle");
        auto* choice =
            rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(document->GetElementById("choice"));
        check(toggle && choice, "Native semantic test controls missing");
        {
            CountChanges checked_events(*toggle), selected_events(*choice);
            const auto* focus = Rml::GetContext(0)->GetFocusElement();
            const auto noop = form("toggle", "check");
            check(!noop.at("delivered").get<bool>() && !noop.at("changed").get<bool>() &&
                      checked_events.count == 0 && Rml::GetContext(0)->GetFocusElement() == focus,
                  "Idempotent native check changed state, focus or events");
            const auto on = form("toggle", "check", true);
            check(on.at("delivered") && on.at("changed") &&
                      field(on, "toggle", "controls").at("checked") && checked_events.count == 1,
                  "Native check did not use one click/change and report actual state");
            check(!form("toggle", "check", true).at("delivered").get<bool>() &&
                      checked_events.count == 1,
                  "Repeated native check toggled or redispatched change");
            form("toggle", "check");
            check(!toggle->HasAttribute("checked") && checked_events.count == 2,
                  "Native uncheck failed to use the existing control state");
            form("radio-a", "choose");
            const auto radio = form("radio-b", "choose");
            check(!field(radio, "radio-a", "controls").at("checked").get<bool>() &&
                      field(radio, "radio-b", "controls").at("checked"),
                  "Native radio group semantics were bypassed");
            const auto options = presenter.form_options("fields", "choice");
            const auto first = options.at("options")[0].at("state").get<std::string>();
            const auto last = options.at("options")[3].at("state").get<std::string>();
            check(!form("choice", "select", false, 0, first).at("delivered").get<bool>() &&
                      selected_events.count == 0,
                  "Idempotent native selection emitted an event");
            const auto selected = form("choice", "select", false, 3, last);
            check(selected.at("delivered") && selected.at("changed") &&
                      selected_events.count == 1 &&
                      field(selected, "choice", "controls").at("selected_index") == 3 &&
                      field(selected, "choice", "controls").at("value") == "d",
                  "Native semantic selection lost its index/value or duplicated change");
            check(!form("choice", "select", false, 3, last).at("delivered").get<bool>() &&
                      selected_events.count == 1,
                  "Repeated semantic selection redispatched change");
            for (const auto index : {1u, 2u})
                rejects([&] {
                    form("choice", "select", false, index,
                         options.at("options")[index].at("state"));
                });
            rejects([&] { form("choice", "select", false, 10, first); });
            choice->GetOption(0)->SetAttribute("value", "changed");
            rejects([&] { form("choice", "select", false, 0, first); });
            check(choice->GetSelection() == 3 && selected_events.count == 1,
                  "A changed option index/value silently selected another choice");
            choice->GetOption(0)->SetAttribute("value", "a");
            for (const auto* id : {"private", "meter", "locked-check", "hidden-check"})
                rejects([&] { form(id, "check", true); });
            rejects([&] { form("radio-a", "check", true); });
            rejects([&] { form("toggle", "choose"); });
            rejects([&] { form("toggle", "unknown"); });
            rejects([&] {
                presenter.form_control_action("fields", "toggle", forms_revision + 1, "check",
                                              true);
            });
            presenter.key(Rml::Input::KI_LEFT, true, 0);
            rejects([&] { form("toggle", "check", true); });
            presenter.key(Rml::Input::KI_LEFT, false, 0);
            auto* pressed = document->GetElementById("radio-b");
            const auto press_position = pressed->GetAbsoluteOffset(Rml::BoxArea::Border);
            const auto press_size = pressed->GetBox().GetSize(Rml::BoxArea::Border);
            check(press_size.x > 0 && press_size.y > 0,
                  "Busy-pointer fixture has no native hit box");
            presenter.mouse_move(int(press_position.x + press_size.x / 2),
                                 int(press_position.y + press_size.y / 2), 0);
            check(presenter.mouse_button(0, true, 0),
                  "Busy-pointer fixture did not retain a native UI press");
            rejects([&] { form("toggle", "check", true); });
            presenter.mouse_button(0, false, 0);
            check(!toggle->HasAttribute("checked"),
                  "Busy-pointer fixture changed the checkbox baseline");
        }
        toggle->Blur();
        {
            NativeMutation change_type(*toggle, [&] {
                toggle->SetAttribute("type", "password");
                toggle->SetAttribute("value", "callback-secret");
            });
            rejects([&] { form("toggle", "check", true); });
            check(!toggle->HasAttribute("checked") && presenter.form_controls("fields").dump().find(
                                                          "callback-secret") == std::string::npos,
                  "A focus callback changed input type and bypassed native form admission");
        }
        toggle->SetAttribute("type", "checkbox");
        toggle->SetAttribute("value", "yes");
        toggle->Blur();
        Rml::Element* duplicate = nullptr;
        {
            NativeMutation ambiguous(*toggle, [&] {
                auto added = document->CreateElement("p");
                added->SetAttribute("id", "toggle");
                duplicate = document->AppendChild(std::move(added));
            });
            rejects([&] { form("toggle", "check", true); });
            check(duplicate && !toggle->HasAttribute("checked"),
                  "Native focus introduced an ambiguous ID before control activation");
        }
        auto removed_duplicate = document->RemoveChild(duplicate);
        choice->Blur();
        {
            const auto first = presenter.form_options("fields", "choice")
                                   .at("options")[0]
                                   .at("state")
                                   .get<std::string>();
            const auto selected_before = choice->GetSelection();
            NativeMutation replace_option(
                *choice, [&] { choice->GetOption(0)->SetAttribute("value", "focus-changed"); });
            rejects([&] { form("choice", "select", false, 0, first); });
            check(choice->GetSelection() == selected_before,
                  "Native focus changed the option after admission and selected stale data");
        }
        auto retiring = document->CreateElement("input");
        retiring->SetAttribute("id", "retire-on-focus");
        retiring->SetAttribute("type", "checkbox");
        auto* retiring_control = document->AppendChild(std::move(retiring));
        presenter.update(.5, 800, 700);
        Rml::ElementPtr removed_on_focus;
        {
            const auto current_revision =
                presenter.form_controls("fields").at("presentation_revision").get<std::uint64_t>();
            NativeMutation retirement(*retiring_control, [&] {
                removed_on_focus = document->RemoveChild(retiring_control);
            });
            rejects([&] {
                presenter.form_control_action("fields", "retire-on-focus", current_revision,
                                              "check", true);
            });
            check(removed_on_focus && !document->GetElementById("retire-on-focus"),
                  "A native focus callback did not retire the target safely");
        }
        // Native focus dispatch retains its requested pointer after callbacks.
        // Restore attached focus before releasing the caller-owned removed element.
        check(document->Focus() && Rml::GetContext(0)->GetFocusElement() == document,
              "Native callback cleanup did not restore attached focus");
        removed_on_focus.reset();
        for (const bool dropdown : {false, true}) {
            auto added = document->CreateElement(dropdown ? "select" : "input");
            const auto id = dropdown ? "select-retire-on-change" : "check-retire-on-change";
            added->SetAttribute("id", id);
            if (dropdown)
                added->SetInnerRML("<option value=\"a\" selected=\"selected\">A</option>"
                                   "<option value=\"b\">B</option>");
            else
                added->SetAttribute("type", "checkbox");
            auto* activated = document->AppendChild(std::move(added));
            presenter.update(.6, 800, 700);
            const auto current_revision = presenter.presentation_revision();
            const auto option_state = dropdown ? presenter.form_options("fields", id)
                                                     .at("options")[1]
                                                     .at("state")
                                                     .get<std::string>()
                                               : std::string{};
            // Keep the native removed owner alive until dispatch unwinds.
            // Immediate dropdown destruction during Change is unsafe at this pin.
            Rml::ElementPtr removed_by_change;
            NativeMutation retirement(
                *activated, [&] { removed_by_change = document->RemoveChild(activated); },
                Rml::EventId::Change);
            const auto result = presenter.form_control_action(
                "fields", id, current_revision, dropdown ? "select" : "check", !dropdown,
                dropdown ? 1 : 0, option_state);
            check(removed_by_change && !document->GetElementById(id) &&
                      result.at("delivered") == true && result.at("changed").is_null(),
                  "Native Change retirement did not report delivery without a retained target");
            removed_by_change.reset();
        }
        presenter.reset("retired-fields", 2);
        rejects([&] { presenter.text_fields("fields"); });
        check(Rml::GetNumContexts() == 0, "Native field observations retained a context");
    }
    check(renderer.geometry.empty() && renderer.textures.empty(), "Native field resources leaked");
    check(asset_detail::read_bytes(root / "fields.rml", 65536) == source,
          "Runtime field actions wrote authored UI source");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need scratch and default font");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        const auto font = asset_detail::read_bytes(argv[2], 4 * 1024 * 1024);
        exercise(root / "native-font", font, false);
        exercise(root / "shaped-font", font, true);
        std::filesystem::remove_all(root);
        std::cout << "Bounded native field observations/focus/text/keys, restrictions, scope and "
                     "lifetime passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
