#include "asset_bytes.hpp"
#include "ui_test_renderer.hpp"
#include "ui_text_boundaries.hpp"
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Elements/ElementFormControlTextArea.h>
#include <RmlUi/Core/TextInputContext.h>
#include <RmlUi/Core/TextInputHandler.h>
#include <array>
#include <fstream>
#include <iostream>

namespace {
using namespace forge;
using Json = nlohmann::json;
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
std::string utf8(std::u8string_view text) {
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
struct Handler final : Rml::TextInputHandler {
    Rml::TextInputContext* active = nullptr;
    std::set<Rml::TextInputContext*> live;
    unsigned destroyed = 0;
    bool proxies = false;
    void OnActivate(Rml::TextInputContext* context) override {
        active = context;
        live.insert(context);
    }
    void OnDeactivate(Rml::TextInputContext* context) override {
        if (active == context)
            active = nullptr;
    }
    void OnDestroy(Rml::TextInputContext* context) override {
        check(live.erase(context) == 1, "Native delegate context destroyed more than once");
        if (proxies) {
            int begin = -1, end = -1;
            context->GetSelectionRange(begin, end);
            Rml::Rectanglef bounds;
            check(begin == 0 && end == 0 && !context->GetBoundingBox(bounds),
                  "Retiring delegate still exposed a destroyed native field");
        }
        if (active == context)
            active = nullptr;
        ++destroyed;
    }
};
struct KeyListener final : Rml::EventListener {
    std::function<void(Rml::Event&)> action;
    Rml::ObserverPtr<Rml::Element> observed;
    Rml::EventId event_id = Rml::EventId::Keydown;
    bool capture = true;
    KeyListener() = default;
    KeyListener(Rml::Element& field, Rml::EventId id, bool capturing = true)
        : observed(field.GetObserverPtr()), event_id(id), capture(capturing) {
        observed->AddEventListener(event_id, this, capture);
    }
    KeyListener(const KeyListener&) = delete;
    KeyListener& operator=(const KeyListener&) = delete;
    ~KeyListener() override {
        if (observed)
            observed->RemoveEventListener(event_id, this, capture);
    }
    void ProcessEvent(Rml::Event& event) override { action(event); }
};
template <class T> UiTextSelection selection(T& field) {
    UiTextSelection result;
    field.GetSelection(&result.begin, &result.end, nullptr);
    return result;
}
std::string exercise(const std::filesystem::path& root, const std::vector<std::byte>& font,
                     bool shaping, bool editing) {
    std::filesystem::create_directories(root);
    std::ofstream(root / "input.rml") << R"rml(<rml><head><style>
body {font-family:Lato;font-size:24px;} input,textarea {display:block;width:400px;height:45px;}
</style></head><body><input id="text" type="text" value="abc"/>
<input id="password" type="password" value="abc"/>
<input id="limited" type="text" maxlength="3" value="ab"/>
<input id="disabled" type="text" disabled="disabled" value="Locked"/>
<input id="removable" type="text" value="abc"/>
<input id="shift-removable" type="text" value="abc"/>
<textarea id="area">abc</textarea>
<input id="pointer-other" type="text" value="abc"/>
<input id="pointer-removable" type="text" value="abc"/></body></rml>)rml";
    const auto asset = register_ui_document(root, "input.rml");
    const auto entity = EntityId::generate();
    Json state{{"version", 1},
               {"session", "native-input"},
               {"generation", 1u},
               {"revision", 1u},
               {"documents", Json::array({{{"entity", entity},
                                           {"asset", asset.id},
                                           {"instance", "input"},
                                           {"visible", true},
                                           {"layer", 0u},
                                           {"model", {{"tick", 0u}, {"paused", true}}},
                                           {"commands", {"Pause"}}}})}};
    Handler handler;
    handler.proxies = editing;
    UiTestRenderer renderer;
    std::string custom_composition;
    {
        UiPresenter presenter(renderer, root, font, {}, &handler, shaping, editing);
        presenter.reset("native-input", 1);
        check(presenter.accept(state), presenter.diagnostic().c_str());
        presenter.update(0, 800, 600);
        auto* context = Rml::GetContext(0);
        auto* document = context->GetDocument(0);
        const auto input = [&](const char* id) {
            auto* result =
                rmlui_dynamic_cast<Rml::ElementFormControlInput*>(document->GetElementById(id));
            check(result != nullptr, "Missing native input field");
            return result;
        };
        const auto press = [&](int key, int modifiers = 0) {
            presenter.key(key, true, modifiers);
            presenter.key(key, false, modifiers);
        };
        auto* text = input("text");
        check(text->Focus() && handler.active, "Native text context was not delegated");
        double pointer_time = 0;
        unsigned pointer_interiors = 0, pointer_drag_interiors = 0;
        int password_interior = -1;
        const auto pointer_clicks = [&](auto* field) {
            const auto value = utf8(u8"a\u0301b");
            UiTextSelection native;
            unsigned delivered = 0;
            KeyListener listener(*field, Rml::EventId::Mousedown);
            listener.action = [&](Rml::Event&) {
                ++delivered;
                native = selection(*field);
            };
            for (int offset = 2; offset < 65; offset += 2) {
                field->SetValue(value);
                field->SetSelectionRange(0, 0);
                presenter.release_input();
                presenter.update(++pointer_time, 800, 600);
                const auto position = field->GetAbsoluteOffset();
                presenter.mouse_move(int(position.x) + offset, int(position.y) + 15, 0);
                const auto before = delivered;
                presenter.mouse_button(0, true, 0);
                presenter.mouse_button(0, false, 0);
                check(delivered == before + 1 && native.begin == native.end && native.end >= 0 &&
                          native.end <= 3,
                      "Pointer did not deliver one ordinary native caret action");
                const int expected = editing && native.end == 1 ? 2 : native.end;
                check(selection(*field) == UiTextSelection{expected, expected} &&
                          field->GetValue() == value && handler.active,
                      "Native pointer focus/caret split a combined character or changed its value");
                pointer_interiors += native.end == 1;
                if (field->GetId() == "password" && native.end == 1)
                    password_interior = offset;
            }
            UiTextSelection moved;
            unsigned move_events = 0, nonempty_drags = 0;
            KeyListener move_listener(*field, Rml::EventId::Drag);
            move_listener.action = [&](Rml::Event&) {
                ++move_events;
                moved = selection(*field);
            };
            for (bool backwards : {true, false}) {
                field->SetValue(value);
                presenter.release_input();
                presenter.update(++pointer_time, 800, 600);
                const auto position = field->GetAbsoluteOffset();
                const int x = int(position.x), y = int(position.y) + 15;
                presenter.mouse_move(x + (backwards ? 60 : 2), y, 0);
                const auto down_before = delivered;
                presenter.mouse_button(0, true, 0);
                check(delivered == down_before + 1 && native.begin == native.end &&
                          native.end >= 0 && native.end <= 3,
                      "Drag start did not observe one native collapsed click");
                const int anchor = editing && native.end == 1 ? 2 : native.end;
                if (selection(*field) != UiTextSelection{anchor, anchor})
                    throw std::runtime_error(Json{
                        {"reason", "Native drag start differs"},
                        {"field", field->GetId()},
                        {"shaping", shaping},
                        {"editing", editing},
                        {"backwards", backwards},
                        {"selection", {selection(*field).begin, selection(*field).end}},
                        {"position", {x, y}},
                        {"time", pointer_time}}.dump());
                const auto drag = [&](int offset) {
                    const auto before = move_events;
                    moved = selection(*field);
                    presenter.mouse_move(x + offset, y, 0);
                    check(move_events <= before + 1 &&
                              (moved.begin == anchor || moved.end == anchor),
                          "Pointer drag duplicated dispatch or changed the native anchor");
                    if (move_events != before) {
                        nonempty_drags += moved.begin != moved.end;
                        pointer_drag_interiors += moved.begin == 1 || moved.end == 1;
                    }
                    const int begin = editing && moved.begin == 1 ? 0 : moved.begin;
                    const int end = editing && moved.end == 1 ? 2 : moved.end;
                    check(
                        selection(*field) == UiTextSelection{begin, end} &&
                            field->GetValue() == value,
                        "Forward/backward pointer drag split a character or oscillated inside it");
                };
                if (backwards) {
                    for (int offset = 58; offset >= 2; offset -= 2)
                        drag(offset);
                    for (int offset = 4; offset <= 60; offset += 2)
                        drag(offset);
                } else {
                    for (int offset = 4; offset <= 60; offset += 2)
                        drag(offset);
                    for (int offset = 58; offset >= 2; offset -= 2)
                        drag(offset);
                }
                presenter.mouse_button(0, false, 0);
            }
            check(move_events > 0 && nonempty_drags > 0,
                  "Pointer fixture did not execute native drag selection");
            field->SetValue("abc");
            field->SetSelectionRange(0, 0);
        };
        pointer_clicks(text);
        pointer_clicks(input("password"));
        auto* pointer_area =
            rmlui_dynamic_cast<Rml::ElementFormControlTextArea*>(document->GetElementById("area"));
        check(pointer_area, "Pointer textarea is missing");
        pointer_clicks(pointer_area);
        check(pointer_interiors > 0, "Native pointer fixture did not exercise an interior caret");
        check(pointer_drag_interiors > 0,
              "Native drag fixture did not exercise a partial-character range");
        check(text->Focus() && handler.active, "Pointer fixture did not restore native focus");
        check(password_interior >= 0, "Masked pointer fixture has no native interior caret");
        auto* password = input("password");
        const auto pointer_value = utf8(u8"a\u0301b");
        const auto prepare_pointer = [&] {
            presenter.release_input();
            password->SetValue(pointer_value);
            password->SetSelectionRange(0, 0);
            check(password->Focus() && handler.active, "Guarded pointer field did not activate");
            presenter.update(++pointer_time, 800, 600);
            const auto position = password->GetAbsoluteOffset();
            presenter.mouse_move(int(position.x) + password_interior, int(position.y) + 15, 0);
        };
        const auto click = [&](int modifiers = 0) {
            presenter.mouse_button(0, true, modifiers);
            presenter.mouse_button(0, false, modifiers);
        };
        prepare_pointer();
        password->SetSelectionRange(3, 3);
        {
            UiTextSelection native;
            unsigned events = 0;
            KeyListener observed(*password, Rml::EventId::Mousedown);
            observed.action = [&](Rml::Event&) {
                ++events;
                native = selection(*password);
            };
            click(Rml::Input::KM_SHIFT);
            check(events == 1 && native == UiTextSelection{1, 3} &&
                      selection(*password) == UiTextSelection{editing ? 0 : 1, 3},
                  "Native Shift click lost its proven anchor or split a character");
            // A Shift-start gesture has no admitted plain-click drag anchor.
            UiTextSelection dragged;
            unsigned drags = 0;
            KeyListener drag(*password, Rml::EventId::Drag);
            drag.action = [&](Rml::Event&) {
                ++drags;
                dragged = selection(*password);
            };
            presenter.update(++pointer_time, 800, 600);
            presenter.mouse_button(0, true, Rml::Input::KM_SHIFT);
            const auto position = password->GetAbsoluteOffset();
            for (int offset : {2, 60, password_interior}) {
                const auto before = drags;
                presenter.mouse_move(int(position.x) + offset, int(position.y) + 15,
                                     Rml::Input::KM_SHIFT);
                if (drags != before)
                    check(selection(*password) == dragged,
                          "Shift-start drag replaced the native gesture policy");
            }
            presenter.mouse_button(0, false, Rml::Input::KM_SHIFT);
            check(drags > 0, "Shift-start fixture did not execute native drag selection");
        }
        prepare_pointer();
        {
            UiTextSelection native;
            unsigned doubles = 0;
            KeyListener observed(*password, Rml::EventId::Dblclick);
            observed.action = [&](Rml::Event&) {
                ++doubles;
                native = selection(*password);
            };
            click();
            click();
            check(doubles > 0 && selection(*password) == native &&
                      password->GetValue() == pointer_value,
                  "Double click did not preserve native masked word selection");
        }
        // Ancestor cancellation prevents the widget action and any boundary correction.
        prepare_pointer();
        password->SetSelectionRange(1, 1);
        {
            KeyListener cancel(*document, Rml::EventId::Mousedown);
            unsigned events = 0;
            cancel.action = [&](Rml::Event& event) {
                ++events;
                event.StopImmediatePropagation();
            };
            click();
            check(events == 1 && selection(*password) == UiTextSelection{1, 1} &&
                      password->GetValue() == pointer_value,
                  "Canceled pointer action changed selection or value");
        }
        // Ancestor bubble callbacks run after the widget and transient observation.
        for (int callback = 0; callback != 4; ++callback) {
            prepare_pointer();
            KeyListener later(*document, Rml::EventId::Mousedown, false);
            unsigned events = 0;
            later.action = [&](Rml::Event&) {
                ++events;
                if (callback == 0)
                    password->SetSelectionRange(0, 0);
                else if (callback == 1) {
                    password->SetValue("callback");
                    password->SetSelectionRange(1, 1);
                } else if (callback == 2) {
                    check(text->Focus() && password->Focus(), "Pointer focus callback failed");
                    password->SetSelectionRange(1, 1);
                } else {
                    press(Rml::Input::KI_LEFT);
                    password->SetSelectionRange(1, 1);
                }
            };
            click();
            const int expected = callback == 0 ? 0 : 1;
            if (events != 1 || selection(*password) != UiTextSelection{expected, expected} ||
                password->GetValue() != (callback == 1 ? "callback" : pointer_value))
                throw std::runtime_error(Json{
                    {"reason", "Pointer callback result differs"},
                    {"callback", callback},
                    {"events", events},
                    {"editing", editing},
                    {"shaping", shaping},
                    {"selection", {selection(*password).begin, selection(*password).end}},
                    {"value",
                     password->GetValue()}}.dump());
        }
        if (editing) {
            auto* other = input("pointer-other");
            check(other->Focus(), "Inactive retirement fixture did not activate");
            prepare_pointer();
            {
                KeyListener later(*document, Rml::EventId::Mousedown, false);
                later.action = [&](Rml::Event&) {
                    auto retired = other->GetParentNode()->RemoveChild(other);
                };
                click();
            }
            check(selection(*password) == UiTextSelection{2, 2},
                  "Retiring an unrelated input invalidated the focused pointer correction");
            prepare_pointer();
            handler.active->SetCompositionRange(0, 1);
            click();
            check(selection(*password) == UiTextSelection{1, 1} &&
                      password->GetValue() == pointer_value,
                  "Pointer alignment interfered with active native composition");
            handler.active->SetCompositionRange(0, 0);
        }
        prepare_pointer();
        auto* removable_pointer = input("pointer-removable");
        presenter.release_input();
        check(removable_pointer->Focus(), "Pointer retirement fixture did not focus");
        presenter.update(++pointer_time, 800, 600);
        const auto removable_position = removable_pointer->GetAbsoluteOffset();
        presenter.mouse_move(int(removable_position.x) + 4, int(removable_position.y) + 15, 0);
        {
            const auto observed = removable_pointer->GetObserverPtr();
            KeyListener retire(*document, Rml::EventId::Mousedown, false);
            unsigned events = 0;
            retire.action = [&](Rml::Event& event) {
                ++events;
                event.StopImmediatePropagation();
                auto retired = removable_pointer->GetParentNode()->RemoveChild(removable_pointer);
            };
            click();
            check(events == 1 && !observed, "Pointer callback did not retire its focused field");
        }
        check(text->Focus() && handler.active, "Pointer guards did not restore native focus");
        const auto words = [&](auto* field, bool masked) {
            check(field && field->Focus() && handler.active, "Word field did not activate");
            const int ctrl = Rml::Input::KM_CTRL, selecting = ctrl | Rml::Input::KM_SHIFT;
            if (masked) {
                // Keep the native masked display's word policy; do not scan
                // plaintext password words in FORGE.
                const auto value = utf8(u8".\u0301a next");
                field->SetValue(value);
                field->SetSelectionRange(0, 0);
                press(Rml::Input::KI_RIGHT, ctrl);
                check(selection(*field) == UiTextSelection{8, 8},
                      "Masked native next-word policy changed");
                press(Rml::Input::KI_LEFT, ctrl);
                check(selection(*field) == UiTextSelection{0, 0},
                      "Masked native previous-word policy changed");
                press(Rml::Input::KI_RIGHT, selecting);
                check(selection(*field) == UiTextSelection{0, 8},
                      "Masked native word selection exposed plaintext grouping");
                press(Rml::Input::KI_LEFT, selecting);
                check(selection(*field) == UiTextSelection{0, 0},
                      "Masked word reversal lost its anchor");
                field->SetSelectionRange(1, 1);
                press(Rml::Input::KI_RIGHT, selecting);
                check(selection(*field) == UiTextSelection{editing ? 0 : 1, 8} &&
                          field->GetValue() == value,
                      "Masked word selection split an interior anchor or changed text");
                return;
            }
            for (const auto& value : {utf8(u8".\u0301a next"), utf8(u8" \u0301a b")}) {
                field->SetValue(value);
                field->SetSelectionRange(0, 0);
                press(Rml::Input::KI_RIGHT, ctrl);
                const int destination = editing ? 2 : 1;
                check(selection(*field) == UiTextSelection{destination, destination},
                      "Ctrl Right split a character or changed disabled native policy");
                field->SetSelectionRange(0, 0);
                press(Rml::Input::KI_RIGHT, selecting);
                check(selection(*field) == UiTextSelection{0, destination},
                      "Ctrl Shift Right split the first character");
                press(Rml::Input::KI_RIGHT, selecting);
                check(selection(*field) == UiTextSelection{0, 4},
                      "Repeated word selection changed the native word destination");
                press(Rml::Input::KI_LEFT, selecting);
                const auto reversed = selection(*field);
                if (reversed != UiTextSelection{0, 0})
                    throw std::runtime_error("Word reversal: editing=" + std::to_string(editing) +
                                             " field=" + field->GetId() + " value=" + value +
                                             " range=" + std::to_string(reversed.begin) + "," +
                                             std::to_string(reversed.end));
                field->SetSelectionRange(4, 4);
                press(Rml::Input::KI_LEFT, ctrl);
                check(selection(*field) == UiTextSelection{0, 0},
                      "Native previous-word boundary changed around combining punctuation");
                field->SetSelectionRange(2, 1);
                press(Rml::Input::KI_RIGHT, selecting);
                check(selection(*field) == UiTextSelection{2, 4},
                      "Word selection failed to cross its native anchor");
                press(Rml::Input::KI_LEFT, selecting);
                check(selection(*field) == UiTextSelection{0, 2},
                      "Word reversal across an anchor lost orientation");
                check(field->GetValue() == value, "Word movement changed the native text");
            }
            field->SetValue("alpha beta");
            field->SetSelectionRange(0, 0);
            press(Rml::Input::KI_RIGHT, ctrl);
            check(selection(*field) == UiTextSelection{6, 6}, "Native next-word policy changed");
            press(Rml::Input::KI_RIGHT, ctrl);
            check(selection(*field) == UiTextSelection{10, 10}, "Native final word was skipped");
            for (int point : {6, 0}) {
                press(Rml::Input::KI_LEFT, ctrl);
                check(selection(*field) == UiTextSelection{point, point},
                      "Native previous-word policy changed");
            }
            for (int point : {6, 10}) {
                press(Rml::Input::KI_RIGHT, selecting);
                check(selection(*field) == UiTextSelection{0, point},
                      "Word extension lost its native anchor");
            }
            for (int point : {6, 0}) {
                press(Rml::Input::KI_LEFT, selecting);
                check(selection(*field) == UiTextSelection{0, point},
                      "Word shrinking failed to return to its anchor");
            }
            field->SetSelectionRange(10, 10);
            for (int point : {6, 0}) {
                press(Rml::Input::KI_LEFT, selecting);
                check(selection(*field) == UiTextSelection{point, 10},
                      "Reverse word extension lost the right anchor");
            }
            for (int point : {6, 10}) {
                press(Rml::Input::KI_RIGHT, selecting);
                check(selection(*field) == UiTextSelection{point, 10},
                      "Reverse word shrinking lost the right anchor");
            }
        };
        words(text, false);
        words(input("password"), true);
        words(
            rmlui_dynamic_cast<Rml::ElementFormControlTextArea*>(document->GetElementById("area")),
            false);
        check(text->Focus() && handler.active, "Text field did not regain focus");
        text->SetValue(utf8(u8"a\u0301b"));
        handler.active->SetCursorPosition(2);
        press(Rml::Input::KI_BACK);
        check(text->GetValue() == (editing ? "b" : "ab"),
              "Optional plain Backspace did not preserve the selected policy");
        text->SetValue(utf8(u8"a\u0301b"));
        text->SetSelectionRange(0, 0);
        press(Rml::Input::KI_RIGHT, Rml::Input::KM_SHIFT);
        const auto initial_shift = selection(*text);
        if (initial_shift != UiTextSelection{0, editing ? 2 : 1})
            throw std::runtime_error("Optional Shift arrow: shaping=" + std::to_string(shaping) +
                                     " editing=" + std::to_string(editing) +
                                     " range=" + std::to_string(initial_shift.begin) + "," +
                                     std::to_string(initial_shift.end));
        const std::pair<std::string, int> cases[]{{utf8(u8"a\u0301x"), 2},
                                                  {utf8(u8"\U0001f469\u200d\U0001f4bbx"), 3},
                                                  {utf8(u8"\U0001f1fa\U0001f1f8x"), 2},
                                                  {utf8(u8"\u1100\u1161\u11a8x"), 3}};
        const auto keys = [&](auto* field) {
            check(field->Focus() && handler.active, "Editable field did not activate");
            for (const auto& [value, scalars] : cases) {
                for (const auto key : {Rml::Input::KI_BACK, Rml::Input::KI_DELETE}) {
                    field->SetValue(value);
                    field->SetSelectionRange(key == Rml::Input::KI_BACK ? scalars : 0,
                                             key == Rml::Input::KI_BACK ? scalars : 0);
                    press(key);
                    check(field->GetValue() == "x", "Native deletion split a grapheme");
                    field->SetValue(value);
                    field->SetSelectionRange(1, 1);
                    press(key);
                    check(field->GetValue() == "x", "Interior caret deletion split a grapheme");
                    field->SetValue(value);
                    field->SetSelectionRange(1, scalars);
                    press(key);
                    check(field->GetValue() == "x", "Partial native selection split a grapheme");
                }
                // Native modified deletion consumes an existing selection;
                // its word scanner must not replace that selected range.
                for (const int modifiers :
                     std::array<int, 3>{Rml::Input::KM_CTRL, Rml::Input::KM_SHIFT,
                                        Rml::Input::KM_CTRL | Rml::Input::KM_SHIFT})
                    for (const auto key : {Rml::Input::KI_BACK, Rml::Input::KI_DELETE}) {
                        field->SetValue(value);
                        field->SetSelectionRange(1, scalars);
                        press(key, modifiers);
                        check(field->GetValue() == "x",
                              "Modified native selected deletion split a character");
                    }
                for (const auto key : {Rml::Input::KI_LEFT, Rml::Input::KI_RIGHT}) {
                    field->SetValue(value);
                    field->SetSelectionRange(key == Rml::Input::KI_LEFT ? scalars : 0,
                                             key == Rml::Input::KI_LEFT ? scalars : 0);
                    press(key);
                    const int expected = key == Rml::Input::KI_LEFT ? 0 : scalars;
                    check(selection(*field) == UiTextSelection{expected, expected},
                          "Native arrow stopped inside a grapheme");
                    field->SetSelectionRange(1, scalars);
                    press(key);
                    check(selection(*field) == UiTextSelection{expected, expected},
                          "Native arrow did not collapse an expanded selection");
                }
                field->SetValue(value);
                field->SetSelectionRange(0, 0);
                press(Rml::Input::KI_RIGHT, Rml::Input::KM_SHIFT);
                check(selection(*field) == UiTextSelection{0, scalars},
                      "Shift Right split a character");
                press(Rml::Input::KI_RIGHT, Rml::Input::KM_SHIFT);
                check(selection(*field) == UiTextSelection{0, scalars + 1},
                      "Repeated Shift Right lost its native anchor");
                press(Rml::Input::KI_LEFT, Rml::Input::KM_SHIFT);
                check(selection(*field) == UiTextSelection{0, scalars},
                      "Reversed Shift Left changed its anchor");
                press(Rml::Input::KI_LEFT, Rml::Input::KM_SHIFT);
                check(selection(*field) == UiTextSelection{0, 0},
                      "Shift Left could not collapse at its native anchor");
                field->SetSelectionRange(scalars + 1, scalars + 1);
                press(Rml::Input::KI_LEFT, Rml::Input::KM_SHIFT);
                press(Rml::Input::KI_LEFT, Rml::Input::KM_SHIFT);
                check(selection(*field) == UiTextSelection{0, scalars + 1},
                      "Repeated Shift Left split a character");
                press(Rml::Input::KI_RIGHT, Rml::Input::KM_SHIFT);
                check(selection(*field) == UiTextSelection{scalars, scalars + 1},
                      "Reversed Shift Right lost the right native anchor");
                press(Rml::Input::KI_RIGHT, Rml::Input::KM_SHIFT);
                check(selection(*field) == UiTextSelection{scalars + 1, scalars + 1},
                      "Shift Right could not collapse at the right native anchor");
                for (const auto key : {Rml::Input::KI_LEFT, Rml::Input::KI_RIGHT}) {
                    field->SetSelectionRange(1, 1);
                    press(key, Rml::Input::KM_SHIFT);
                    check(selection(*field) == UiTextSelection{0, scalars},
                          "An interior native anchor produced a partial-character selection");
                }
                // A public native reference proves that Home leaves an
                // interior anchor partial at this pin. The adapter must expand
                // it through the ordinary native selection API.
                field->SetValue(value);
                field->SetSelectionRange(1, 1);
                auto* native = Rml::GetContext(0);
                native->ProcessKeyDown(Rml::Input::KI_HOME, Rml::Input::KM_SHIFT);
                native->ProcessKeyUp(Rml::Input::KI_HOME, Rml::Input::KM_SHIFT);
                check(selection(*field) == UiTextSelection{0, 1},
                      "Native Home reference no longer exercises a partial anchor");
                for (const int modifiers : std::array<int, 2>{
                         Rml::Input::KM_SHIFT, Rml::Input::KM_SHIFT | Rml::Input::KM_CTRL}) {
                    field->SetSelectionRange(1, 1);
                    press(Rml::Input::KI_HOME, modifiers);
                    check(selection(*field) == UiTextSelection{0, scalars},
                          "Native Home selection split its interior anchor");
                    field->SetSelectionRange(1, 1);
                    press(Rml::Input::KI_END, modifiers);
                    check(selection(*field) == UiTextSelection{0, scalars + 1},
                          "Native End selection split its interior anchor");
                }
                field->SetSelectionRange(scalars, 0);
                press(Rml::Input::KI_RIGHT, Rml::Input::KM_SHIFT);
                check(selection(*field) == UiTextSelection{scalars, scalars},
                      "Native reverse selection orientation was not retained");
            }
        };
        if (editing) {
            keys(text);
            keys(input("password"));
            auto* area = rmlui_dynamic_cast<Rml::ElementFormControlTextArea*>(
                document->GetElementById("area"));
            check(area != nullptr, "Missing native textarea");
            keys(area);
            text->Focus();
            KeyListener listener;
            document->AddEventListener(Rml::EventId::Keydown, &listener, true);
            text->SetValue(utf8(u8"a\u0301b"));
            unsigned delivered = 0;
            text->SetSelectionRange(0, 0);
            listener.action = [&](Rml::Event&) {
                ++delivered;
                check(selection(*text) == UiTextSelection{0, 0},
                      "Shift selection changed before native event listeners");
            };
            press(Rml::Input::KI_RIGHT, Rml::Input::KM_SHIFT);
            check(delivered == 1 && selection(*text) == UiTextSelection{0, 2},
                  "Shift selection bypassed or duplicated native key dispatch");
            for (int modifier : std::array<int, 5>{Rml::Input::KM_SHIFT, Rml::Input::KM_CTRL,
                                                   Rml::Input::KM_CTRL | Rml::Input::KM_SHIFT,
                                                   Rml::Input::KM_ALT, Rml::Input::KM_META}) {
                text->SetSelectionRange(2, 2);
                bool called = false;
                listener.action = [&](Rml::Event& event) {
                    called = true;
                    check(selection(*text) == UiTextSelection{2, 2},
                          "Modified key altered selection before native event handling");
                    event.StopImmediatePropagation();
                };
                press(Rml::Input::KI_LEFT, modifier);
                check(called, "Modified key bypassed native event dispatch");
            }
            text->SetSelectionRange(0, 0);
            listener.action = [&](Rml::Event& event) {
                text->SetSelectionRange(1, 1);
                event.StopImmediatePropagation();
            };
            press(Rml::Input::KI_RIGHT, Rml::Input::KM_SHIFT);
            check(selection(*text) == UiTextSelection{1, 1},
                  "Canceled Shift action overwrote callback-authored selection");
            bool nested = false;
            text->SetSelectionRange(0, 0);
            listener.action = [&](Rml::Event& event) {
                if (!nested) {
                    nested = true;
                    text->SetSelectionRange(2, 2);
                    press(Rml::Input::KI_LEFT, Rml::Input::KM_SHIFT);
                    event.StopImmediatePropagation();
                }
            };
            press(Rml::Input::KI_RIGHT, Rml::Input::KM_SHIFT);
            check(nested && selection(*text) == UiTextSelection{0, 2},
                  "Outer Shift correction overwrote a reentrant native input operation");
            for (const auto key : {Rml::Input::KI_UP, Rml::Input::KI_DOWN, Rml::Input::KI_PRIOR,
                                   Rml::Input::KI_NEXT, Rml::Input::KI_HOME, Rml::Input::KI_END}) {
                nested = false;
                text->SetSelectionRange(0, 0);
                press(key, Rml::Input::KM_SHIFT);
                check(nested && selection(*text) == UiTextSelection{0, 2},
                      "Outer native jump overwrote a reentrant input operation");
            }
            for (auto key : {Rml::Input::KI_LEFT, Rml::Input::KI_RIGHT, Rml::Input::KI_BACK,
                             Rml::Input::KI_DELETE}) {
                text->SetSelectionRange(1, 1);
                listener.action = [](Rml::Event& event) { event.StopImmediatePropagation(); };
                press(key);
                check(selection(*text) == UiTextSelection{1, 1} &&
                          text->GetValue() == utf8(u8"a\u0301b"),
                      "Canceled native event left a temporary selection or changed text");
            }
            for (const int modifiers :
                 std::array<int, 3>{Rml::Input::KM_CTRL, Rml::Input::KM_SHIFT,
                                    Rml::Input::KM_CTRL | Rml::Input::KM_SHIFT})
                for (const auto key : {Rml::Input::KI_BACK, Rml::Input::KI_DELETE}) {
                    text->SetValue(utf8(u8"a\u0301b"));
                    text->SetSelectionRange(1, 2);
                    listener.action = [](Rml::Event& event) { event.StopImmediatePropagation(); };
                    press(key, modifiers);
                    check(selection(*text) == UiTextSelection{1, 2} &&
                              text->GetValue() == utf8(u8"a\u0301b"),
                          "Canceled modified deletion retained its transient selection");
                }
            for (const auto key : {Rml::Input::KI_UP, Rml::Input::KI_DOWN, Rml::Input::KI_PRIOR,
                                   Rml::Input::KI_NEXT, Rml::Input::KI_HOME, Rml::Input::KI_END}) {
                text->SetSelectionRange(1, 1);
                listener.action = [&](Rml::Event& event) {
                    check(selection(*text) == UiTextSelection{1, 1},
                          "Native jump changed selection before its key event");
                    event.StopImmediatePropagation();
                };
                press(key, Rml::Input::KM_SHIFT);
                check(selection(*text) == UiTextSelection{1, 1},
                      "Canceled native jump changed its original selection");
            }
            // Exercise the same public composition calls used by the platform delegate.
            handler.active->SetText(utf8(u8"a\u0301b"), 0, 3);
            handler.active->SetCompositionRange(0, 2);
            handler.active->SetCursorPosition(2);
            bool composition_event = false;
            listener.action = [&](Rml::Event& event) {
                composition_event = true;
                check(selection(*text) == UiTextSelection{2, 2},
                      "Boundary editing interfered with active composition");
                event.StopImmediatePropagation();
            };
            press(Rml::Input::KI_BACK);
            check(composition_event, "Composition key did not reach native dispatch");
            composition_event = false;
            press(Rml::Input::KI_LEFT, Rml::Input::KM_SHIFT);
            check(composition_event && selection(*text) == UiTextSelection{2, 2},
                  "Shift preparation interfered with active native composition");
            composition_event = false;
            press(Rml::Input::KI_RIGHT, Rml::Input::KM_CTRL | Rml::Input::KM_SHIFT);
            check(composition_event && selection(*text) == UiTextSelection{2, 2},
                  "Word preparation interfered with active native composition");
            for (const auto key : {Rml::Input::KI_UP, Rml::Input::KI_DOWN, Rml::Input::KI_PRIOR,
                                   Rml::Input::KI_NEXT, Rml::Input::KI_HOME, Rml::Input::KI_END,
                                   Rml::Input::KI_BACK, Rml::Input::KI_DELETE}) {
                composition_event = false;
                press(key, Rml::Input::KM_CTRL | Rml::Input::KM_SHIFT);
                check(composition_event && selection(*text) == UiTextSelection{2, 2},
                      "Modified native editing interfered with active composition");
            }
            handler.active->CommitComposition(utf8(u8"a\u0301"));
            check(text->GetValue() == utf8(u8"a\u0301b"),
                  "Public composition commit did not reach the native field");
            document->RemoveEventListener(Rml::EventId::Keydown, &listener, true);
            text->SetValue(utf8(u8"a\u0301b"));
            text->SetSelectionRange(2, 2);
            press(Rml::Input::KI_BACK);
            check(text->GetValue() == "b", "Committed composition kept boundary editing disabled");
            if (!shaping) {
                // The shaped font's independent glyph-run bound rejects this large value.
                text->SetValue(utf8(u8"a\u0301") + std::string(ui_edit_text_characters, 'x'));
                text->SetSelectionRange(2, 2);
                press(Rml::Input::KI_BACK);
                check(text->GetValue() == "a" + std::string(ui_edit_text_characters, 'x'),
                      "Oversized native field was truncated instead of retaining native editing");
                text->SetValue(utf8(u8"a\u0301") + std::string(ui_edit_text_characters, 'x'));
                text->SetSelectionRange(0, 0);
                press(Rml::Input::KI_RIGHT, Rml::Input::KM_SHIFT);
                check(selection(*text) == UiTextSelection{0, 1},
                      "Oversized Shift input did not retain native selection policy");
            }
            auto* removable = input("removable");
            check(removable->Focus() && handler.active, "Removal field did not activate");
            removable->SetValue(utf8(u8"a\u0301b"));
            removable->SetSelectionRange(2, 2);
            listener.action = [&](Rml::Event& event) {
                event.StopImmediatePropagation();
                auto retired = removable->GetParentNode()->RemoveChild(removable);
            };
            document->AddEventListener(Rml::EventId::Keydown, &listener, true);
            press(Rml::Input::KI_BACK);
            document->RemoveEventListener(Rml::EventId::Keydown, &listener, true);
            check(!handler.active, "Removed field remained active in the delegate");
            auto* shifted = input("shift-removable");
            check(shifted->Focus() && handler.active, "Shift removal field did not activate");
            shifted->SetValue(utf8(u8"a\u0301b"));
            shifted->SetSelectionRange(0, 0);
            bool removed = false;
            listener.action = [&](Rml::Event& event) {
                removed = true;
                event.StopImmediatePropagation();
                auto retired = shifted->GetParentNode()->RemoveChild(shifted);
            };
            shifted->AddEventListener(Rml::EventId::Keydown, &listener);
            press(Rml::Input::KI_RIGHT, Rml::Input::KM_SHIFT);
            check(removed && !handler.active,
                  "Shift removal retained an active native field or skipped its callback");
        }
        auto* limited = input("limited");
        check(limited->Focus() && handler.active, "Limited field did not activate");
        handler.active->SetSelectionRange(2, 2);
        check(presenter.text("cd"), "Native text insertion was not consumed");
        check(limited->GetValue() == "abc", "Native maximum length was bypassed");
        limited->SetValue("ab");
        handler.active->SetText("cd", 2, 2);
        handler.active->SetCompositionRange(2, 4);
        handler.active->CommitComposition("xyz");
        custom_composition = limited->GetValue();
        // SDL clears preedit before it supplies the committed text through native text input.
        limited->SetValue("ab");
        handler.active->SetText("cd", 2, 2);
        handler.active->SetCompositionRange(2, 4);
        handler.active->SetText("", 2, 4);
        handler.active->SetCompositionRange(2, 2);
        handler.active->SetCursorPosition(2);
        handler.active->CommitComposition("");
        presenter.text("xyz");
        check(limited->GetValue() == "abx", "Platform commit lost native maximum length");
        auto* disabled = input("disabled");
        disabled->Focus();
        press(Rml::Input::KI_BACK);
        check(disabled->GetValue() == "Locked", "Disabled input accepted native deletion");
        presenter.release_input();
        check(!handler.active, "Blur kept a borrowed platform input active");
        text->Focus();
        text->SetValue("abc");
        text->SetSelectionRange(1, 1);
        presenter.text("z");
        check(text->GetValue() == "azbc", "Plain text insertion stopped using the native field");
        presenter.update(pointer_time + .02, 800, 600);
        presenter.render();
        check(presenter.diagnostic().empty(), presenter.diagnostic().c_str());
        presenter.reset("retired-input", 2);
        check(!handler.active && handler.live.empty() && handler.destroyed >= 2 &&
                  Rml::GetNumContexts() == 0,
              "Reset did not retire native text-input handles");
    }
    check(renderer.geometry.empty() && renderer.textures.empty(),
          "Native input resources survived presenter shutdown");
    return custom_composition;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need scratch and default font");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        const auto font = asset_detail::read_bytes(argv[2], 4 * 1024 * 1024);
        for (bool shaping : {false, true}) {
            const auto baseline =
                exercise(root / (std::to_string(shaping) + "0"), font, shaping, false);
            check(exercise(root / (std::to_string(shaping) + "1"), font, shaping, true) == baseline,
                  "Delegate changed native nonempty custom composition behavior");
            std::cout << "Native nonempty custom commit (not SDL commit path): " << baseline << " ("
                      << baseline.size() << " bytes; native maxlength 3)\n";
        }
        std::filesystem::remove_all(root);
        std::cout << "Native input/password/textarea graphemes, events, limits, composition, "
                     "blur/removal/reset and both font modes passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
