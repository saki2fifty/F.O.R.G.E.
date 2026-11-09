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
<textarea id="area">abc</textarea></body></rml>)rml";
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
            for (auto key : {Rml::Input::KI_LEFT, Rml::Input::KI_RIGHT, Rml::Input::KI_BACK,
                             Rml::Input::KI_DELETE}) {
                text->SetSelectionRange(1, 1);
                listener.action = [](Rml::Event& event) { event.StopImmediatePropagation(); };
                press(key);
                check(selection(*text) == UiTextSelection{1, 1} &&
                          text->GetValue() == utf8(u8"a\u0301b"),
                      "Canceled native event left a temporary selection or changed text");
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
        presenter.update(.02, 800, 600);
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
