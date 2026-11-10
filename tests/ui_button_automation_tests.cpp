#include "asset_bytes.hpp"
#include "ui_control_access.hpp"
#include "ui_test_renderer.hpp"
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
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
        throw std::runtime_error("Invalid button activation admitted at line " +
                                 std::to_string(where.line()));
}
Json button(const Json& snapshot, const char* id) {
    for (const auto& row : snapshot.at("buttons"))
        if (row.at("id") == id)
            return row;
    throw std::runtime_error(std::string("Native button missing: ") + id);
}
struct NativeListener final : Rml::EventListener {
    Rml::ObserverPtr<Rml::Element> element;
    Rml::EventId event;
    bool capture;
    std::function<void(Rml::Event&)> callback;
    NativeListener(Rml::Element& source, Rml::EventId type, std::function<void(Rml::Event&)> fn,
                   bool capturing = false)
        : element(source.GetObserverPtr()), event(type), capture(capturing),
          callback(std::move(fn)) {
        source.AddEventListener(event, this, capture);
    }
    ~NativeListener() override {
        if (element)
            element->RemoveEventListener(event, this, capture);
    }
    void ProcessEvent(Rml::Event& value) override { callback(value); }
};
void exercise(const std::filesystem::path& root, const std::vector<std::byte>& font, bool shaping) {
    std::filesystem::create_directories(root);
    const std::string markup = R"rml(<rml><head><style>
body {font-family:Lato;font-size:18px;}
button,input {display:block;width:260px;height:30px;}
#hidden {visibility:hidden;} #no-focus {focus:none;}
</style></head><body>
<button id="plain"><span>Plain &amp; safe</span></button>
<input id="push" type="button" value="A &amp; &lt;B&gt;"/>
<input id="locked" type="button" value="Locked" disabled="disabled"/>
<button id="no-focus">No native focus</button><button id="hidden">Hidden</button>
<button id="attribute-only" disabled="disabled">Generic attribute</button>
<button id="duplicate">Ambiguous</button><p id="duplicate">Other element</p>
<button id="nested">Caption <input id="secret" type="password" value="native-button-secret"/></button>
<input id="submit" type="submit" value="Submit"/>
<input id="upper-button" type="BUTTON" value="Fallback text"/>
<input id="upper-check" type="CHECKBOX" value="Fallback text"/>
<input id="upper-radio" type="RADIO" value="Fallback text"/>
</body></rml>)rml";
    std::ofstream(root / "buttons.rml") << markup;
    const auto asset = register_ui_document(root, "buttons.rml");
    const auto catalog = asset_detail::read_bytes(root / "forge.assets.json", 4 * 1024 * 1024);
    Json state{{"version", 1},
               {"session", "native-buttons"},
               {"generation", 1u},
               {"revision", 1u},
               {"documents", Json::array({{{"entity", EntityId::generate()},
                                           {"asset", asset.id},
                                           {"instance", "buttons"},
                                           {"visible", true},
                                           {"layer", 0u},
                                           {"model", Json::object()},
                                           {"commands", Json::array()}}})}};
    UiTestRenderer renderer;
    {
        UiPresenter presenter(renderer, root, font, {}, nullptr, shaping);
        presenter.reset("native-buttons", 1);
        check(presenter.accept(state), presenter.diagnostic().c_str());
        presenter.update(0, 800, 700);
        const auto initial = presenter.buttons("buttons");
        const auto revision = initial.at("presentation_revision").get<std::uint64_t>();
        auto* document = Rml::GetContext(0)->GetDocument(0);
        auto* plain = document->GetElementById("plain");
        auto* push = document->GetElementById("push");
        check(initial.at("buttons").size() == 8 && !initial.at("truncated").get<bool>(),
              "Native button view includes unrelated controls or lost buttons");
        for (const auto* id : {"upper-button", "upper-check", "upper-radio"}) {
            auto* fallback = document->GetElementById(id);
            check(fallback->IsClassSet("text") && !ui_button(fallback) &&
                      !ui_form_control(fallback),
                  "Case-sensitive native fallback text was classified as a form/button action");
        }
        rejects([&] {
            presenter.form_control_action("buttons", "upper-check", revision, "check", true);
        });
        rejects([&] { presenter.press_button("buttons", "upper-button", revision); });
        check(!rmlui_dynamic_cast<Rml::ElementFormControlInput*>(plain) &&
                  rmlui_dynamic_cast<Rml::ElementFormControlInput*>(push),
              "Pinned generic/native input button classes differ");
        check(button(initial, "plain").at("text") == "Plain & safe" &&
                  button(initial, "plain").at("enabled").is_null() &&
                  button(initial, "push").at("value") == "A & <B>" &&
                  button(initial, "push").at("enabled") == true,
              "Native button captions/values or enabled facts were invented");
        check(!button(initial, "locked").at("pressable").get<bool>() &&
                  !button(initial, "hidden").at("pressable").get<bool>() &&
                  !button(initial, "no-focus").at("pressable").get<bool>() &&
                  !button(initial, "duplicate").at("addressable").get<bool>(),
              "Ineligible native buttons advertised activation");
        check(initial.dump().find("native-button-secret") == std::string::npos &&
                  !button(initial, "nested").at("text_available").get<bool>(),
              "Nested field/password text leaked into button observations");
        check(button(initial, "attribute-only").at("enabled").is_null() &&
                  button(initial, "attribute-only").at("disabled_attribute") == true &&
                  button(initial, "attribute-only").at("pressable") == true,
              "Generic button acquired an invented native disabled state");
        const auto press = [&](const char* id) {
            return presenter.press_button("buttons", id, revision);
        };
        rejects([&] { presenter.buttons("stale"); });
        rejects([&] { presenter.press_button("buttons", "plain", revision + 1); });
        for (const auto* id :
             {"missing", "locked", "hidden", "no-focus", "duplicate", "secret", "submit", ""})
            rejects([&] { press(id); });
        int clicks = 0;
        {
            NativeListener count(*plain, Rml::EventId::Click, [&](auto&) { ++clicks; });
            check(press("plain").at("delivered") == true &&
                      press("plain").at("delivered") == true && clicks == 2,
                  "Fresh native button activation was dropped or duplicated");
            check(button(presenter.buttons("buttons"), "plain").at("focused") == true,
                  "Native button did not retain ordinary focus");
            presenter.key(Rml::Input::KI_LEFT, true, 0);
            rejects([&] { press("plain"); });
            presenter.key(Rml::Input::KI_LEFT, false, 0);
            const auto position = plain->GetAbsoluteOffset(Rml::BoxArea::Border);
            const auto size = plain->GetBox().GetSize(Rml::BoxArea::Border);
            presenter.mouse_move(int(position.x + size.x / 2), int(position.y + size.y / 2), 0);
            check(presenter.mouse_button(0, true, 0),
                  "Button held-pointer fixture missed native hit box");
            rejects([&] { press("plain"); });
            presenter.mouse_button(0, false, 0);
        }
        {
            int bubble = 0;
            NativeListener count(*push, Rml::EventId::Click, [&](auto&) { ++bubble; });
            NativeListener cancel(
                *document, Rml::EventId::Click,
                [](auto& event) { event.StopImmediatePropagation(); }, true);
            check(press("push").at("delivered") == true && bubble == 0,
                  "Native cancellation was bypassed or delivery was treated as acknowledgement");
        }
        {
            int count = 0;
            auto* generic = document->GetElementById("attribute-only");
            NativeListener clicks(*generic, Rml::EventId::Click, [&](auto&) { ++count; });
            press("attribute-only");
            check(count == 1,
                  "Generic native disabled-attribute behavior differs from reported eligibility");
        }
        push->Blur();
        {
            NativeListener change(*push, Rml::EventId::Focus, [&](auto&) {
                push->SetAttribute("type", "password");
                push->SetAttribute("value", "focus-button-secret");
            });
            rejects([&] { press("push"); });
            check(presenter.buttons("buttons").dump().find("focus-button-secret") ==
                      std::string::npos,
                  "Focus type change exposed secret value");
        }
        push->SetAttribute("type", "button");
        push->SetAttribute("value", "A & <B>");
        check(document->Focus(), "Native attached focus restore failed");
        {
            Rml::ElementPtr removed;
            NativeListener retire(*push, Rml::EventId::Click,
                                  [&](auto&) { removed = document->RemoveChild(push); });
            const auto result = press("push");
            check(removed && result.at("delivered") == true && !document->GetElementById("push"),
                  "Safely detached native button did not report delivery");
            check(document->Focus(), "Native post-click focus restore failed");
        }
        // Adding many buttons must preserve existing text/form discovery counters.
        presenter.release_input();
        check(document->Focus(), "Native focus reset failed");
        std::string many =
            "<input id='field' type='text' value='Text'/><input id='check' type='checkbox'/>";
        for (int i = 0; i < 65; ++i)
            many += "<button>Anonymous</button>";
        document->SetInnerRML(many);
        presenter.update(.1, 800, 700);
        check(presenter.buttons("buttons").at("truncated") == true &&
                  presenter.buttons("buttons").at("buttons").size() == 64 &&
                  presenter.text_fields("buttons").at("truncated") == false &&
                  presenter.form_controls("buttons").at("truncated") == false,
              "Button counter silently truncated the old independent text/form views");
        many = "<button id='only'>Only button</button>";
        for (int i = 0; i < 65; ++i)
            many += "<input type='text'/><input type='checkbox'/>";
        document->SetInnerRML(many);
        presenter.update(.2, 800, 700);
        check(presenter.text_fields("buttons").at("truncated") == true &&
                  presenter.form_controls("buttons").at("truncated") == true &&
                  presenter.buttons("buttons").at("truncated") == false &&
                  presenter.press_button("buttons", "only", presenter.presentation_revision())
                          .at("delivered") == true,
              "Old field/form counters leaked into independent button admission");
        check(document->Focus(), "Native node-budget focus reset failed");
        many = "<button id='only'>Only</button>";
        for (int i = 0; i < 4100; ++i)
            many += "<p/>";
        document->SetInnerRML(many);
        presenter.update(.3, 800, 700);
        const auto bounded = presenter.buttons("buttons");
        check(bounded.at("truncated") == true && bounded.at("visited_nodes") == 4096 &&
                  !button(bounded, "only").at("pressable").get<bool>(),
              "Native button node budget or addressing differs");
        rejects(
            [&] { presenter.press_button("buttons", "only", presenter.presentation_revision()); });
        presenter.reset("retired-buttons", 2);
        rejects([&] { presenter.buttons("buttons"); });
    }
    check(renderer.geometry.empty() && renderer.textures.empty(), "Native button resources leaked");
    check(asset_detail::read_bytes(root / "forge.assets.json", 4 * 1024 * 1024) == catalog &&
              asset_detail::read_bytes(root / "buttons.rml", 65536) ==
                  std::vector<std::byte>(
                      reinterpret_cast<const std::byte*>(markup.data()),
                      reinterpret_cast<const std::byte*>(markup.data() + markup.size())),
          "Button runtime actions wrote authored source/catalog");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need scratch directory and font");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        const auto font = asset_detail::read_bytes(argv[2], 4 * 1024 * 1024);
        exercise(root / "native", font, false);
        exercise(root / "shaped", font, true);
        std::filesystem::remove_all(root);
        std::cout << "Native button discovery/activation/events/scope/budgets/secret and source "
                     "isolation passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
