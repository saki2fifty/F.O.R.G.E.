#include "asset_bytes.hpp"
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
template <class F> void rejects(F fn, std::source_location at = std::source_location::current()) {
    bool rejected = false;
    try {
        fn();
    } catch (const std::exception&) {
        rejected = true;
    }
    if (!rejected)
        throw std::runtime_error("Invalid widget keyboard action admitted at " +
                                 std::to_string(at.line()));
}
struct Listener final : Rml::EventListener {
    Rml::ObserverPtr<Rml::Element> element;
    Rml::EventId event;
    bool capture;
    std::function<void(Rml::Event&)> callback;
    Listener(Rml::Element& source, Rml::EventId type, std::function<void(Rml::Event&)> fn,
             bool capturing = false)
        : element(source.GetObserverPtr()), event(type), capture(capturing),
          callback(std::move(fn)) {
        source.AddEventListener(event, this, capture);
    }
    ~Listener() override {
        if (element)
            element->RemoveEventListener(event, this, capture);
    }
    void ProcessEvent(Rml::Event& value) override { callback(value); }
};
Json row(const Json& view, const char* list, const char* id) {
    for (const auto& item : view.at(list))
        if (item.at("id") == id)
            return item;
    throw std::runtime_error(std::string("Missing native widget ") + id);
}
void exercise(const std::filesystem::path& root, const std::vector<std::byte>& font, bool shaping) {
    std::filesystem::create_directories(root);
    const std::string markup = R"(<rml><head><style>
body{font-family:Lato;font-size:18px;} input,button,select{display:block;width:200px;height:25px;}
#hidden{visibility:hidden;} #no-focus{focus:none;}
</style></head><body>
<input id="range" type="range" min="-2" max="8" step="2" value="2"/>
<input id="vertical" type="range" orientation="vertical" value="2"/>
<input id="check" type="checkbox" value="yes"/>
<input id="radio-a" type="radio" name="group" value="a"/>
<input id="radio-b" type="radio" name="group" value="b"/>
<select id="choice"><option value="a" selected="selected">A</option><option value="b">B</option></select>
<input id="push" type="button" value="Push"/><button id="plain">Plain</button>
<input id="hidden" type="range"/><input id="locked" type="range" disabled="disabled"/>
<input id="no-focus" type="range"/><input id="duplicate" type="range"/><p id="duplicate">Other</p>
<input id="secret" type="password" value="widget-private-secret"/>
<progress id="progress" value="0.5"/>
</body></rml>)";
    std::ofstream(root / "keys.rml", std::ios::binary) << markup;
    const auto asset = register_ui_document(root, "keys.rml");
    const auto catalog = asset_detail::read_bytes(root / "forge.assets.json", 4 * 1024 * 1024);
    Json state{{"version", 1},
               {"session", "widget-keys"},
               {"generation", 1u},
               {"revision", 1u},
               {"documents", Json::array({{{"entity", EntityId::generate()},
                                           {"asset", asset.id},
                                           {"instance", "keys"},
                                           {"visible", true},
                                           {"layer", 0u},
                                           {"model", Json::object()},
                                           {"commands", Json::array()}}})}};
    UiTestRenderer renderer;
    {
        UiPresenter p(renderer, root, font, {}, nullptr, shaping);
        p.reset("widget-keys", 1);
        check(p.accept(state), p.diagnostic().c_str());
        p.update(0, 800, 700);
        auto* context = Rml::GetContext(0);
        auto* doc = context->GetDocument(0);
        auto* range =
            rmlui_dynamic_cast<Rml::ElementFormControlInput*>(doc->GetElementById("range"));
        const auto revision = p.presentation_revision();
        const auto act = [&](const char* id, const char* kind, const char* key) {
            return p.widget_keyboard_action("keys", id, revision, kind, key);
        };
        check(row(p.buttons("keys"), "buttons", "plain").at("tab_stop") == false &&
                  row(p.buttons("keys"), "buttons", "push").at("tab_stop") == true &&
                  row(p.range_controls("keys"), "ranges", "range").at("tab_stop") == true &&
                  row(p.form_controls("keys"), "controls", "check").at("focusable") == true,
              "Native tab-stop/focus facts were fabricated");
        check(p.range_controls("keys").dump().find("widget-private-secret") == std::string::npos &&
                  p.form_controls("keys").dump().find("widget-private-secret") ==
                      std::string::npos &&
                  p.buttons("keys").dump().find("widget-private-secret") == std::string::npos,
              "Native keyboard family observations leaked unrelated password plaintext");
        rejects([&] { act("range", "range", "right"); });
        rejects([&] { p.widget_keyboard_action("keys", "range", revision + 1, "range", "focus"); });
        for (const auto* id :
             {"hidden", "locked", "no-focus", "duplicate", "secret", "progress", "missing", ""})
            rejects([&] { act(id, "range", "focus"); });
        rejects([&] { act("range", "button", "focus"); });
        rejects([&] { act("range", "unsupported", "focus"); });
        rejects([&] { act("range", "range", "select_all"); });
        rejects([&] { act("secret", "password", "focus"); });
        int clicks = 0, changes = 0;
        Listener click(*doc, Rml::EventId::Click, [&](auto&) { ++clicks; });
        Listener change(*doc, Rml::EventId::Change, [&](auto&) { ++changes; });
        check(act("range", "range", "focus").at("delivered") == true,
              "Native widget focus was not delivered");
        act("range", "range", "right");
        check(range->GetValue() == "4.000000" && changes == 1,
              "Native slider keyboard step changed or duplicated");
        act("range", "range", "left");
        check(range->GetValue() == "2.000000" && changes == 2, "Native slider left step differs");
        act("vertical", "range", "focus");
        act("vertical", "range", "down");
        check(row(p.range_controls("keys"), "ranges", "vertical").at("value") == "3.000000",
              "Vertical native Down did not increment");
        act("check", "checkbox", "focus");
        act("check", "checkbox", "space");
        check(row(p.form_controls("keys"), "controls", "check").at("checked") == true,
              "Native Space did not toggle checkbox");
        act("check", "checkbox", "accept");
        check(row(p.form_controls("keys"), "controls", "check").at("checked") == false,
              "Native Return did not toggle checkbox");
        act("radio-a", "radio", "focus");
        changes = 0;
        act("radio-a", "radio", "space");
        act("radio-a", "radio", "accept");
        check(changes == 2 &&
                  row(p.form_controls("keys"), "controls", "radio-a").at("checked") == true,
              "Fresh native radio key events were replaced by desired-state no-ops");
        act("choice", "select", "focus");
        act("choice", "select", "down");
        check(row(p.form_controls("keys"), "controls", "choice").at("value") == "b",
              "Native dropdown arrow selection differs");
        act("plain", "button", "focus");
        clicks = 0;
        check(act("plain", "button", "space").at("delivered") == true && clicks == 0,
              "Generic button keyboard fabricated native Click");
        act("plain", "button", "accept");
        check(clicks == 0, "Generic button Return fabricated native activation");
        auto* plain = doc->GetElementById("plain");
        check(plain->SetProperty("tab-index", "auto"), "Native tab-index property rejected");
        p.update(.01, 800, 700);
        act("plain", "button", "focus");
        act("plain", "button", "space");
        check(
            clicks == 1 && row(p.buttons("keys"), "buttons", "plain").at("tab_stop") == true,
            "Authored native tab stop did not enable ordinary generic button keyboard activation");
        check(plain->SetProperty("tab-index", "none"), "Native tab-index restore rejected");
        p.update(.02, 800, 700);
        clicks = 0;
        act("push", "input-button", "focus");
        act("push", "input-button", "space");
        check(clicks == 1, "Native input button Space did not dispatch exactly once");
        act("push", "input-button", "next");
        check(context->GetFocusElement() &&
                  context->GetFocusElement() != doc->GetElementById("push") &&
                  context->GetFocusElement() != doc->GetElementById("plain"),
              "Native Tab did not leave input button or skipped its tab-stop policy");
        rejects([&] { act("push", "input-button", "space"); });
        act("vertical", "range", "focus");
        act("vertical", "range", "previous");
        check(context->GetFocusElement() == range,
              "Native Shift-Tab did not follow the adjacent native control order");
        act("range", "range", "focus");
        p.key(Rml::Input::KI_RIGHT, true, 0);
        rejects([&] { act("range", "range", "left"); });
        p.key(Rml::Input::KI_RIGHT, false, 0);
        const auto before = range->GetValue();
        {
            Listener cancel(
                *doc, Rml::EventId::Keydown, [](auto& e) { e.StopImmediatePropagation(); }, true);
            check(act("range", "range", "right").at("delivered") == true &&
                      range->GetValue() == before,
                  "Native keyboard cancellation was bypassed");
        }
        check(doc->Focus(), "Attached focus reset failed");
        {
            Listener focus(*range, Rml::EventId::Focus,
                           [&](auto&) { range->SetAttribute("step", "0"); });
            rejects([&] { act("range", "range", "focus"); });
        }
        range->SetAttribute("step", "2");
        check(doc->Focus(), "Attached focus reset failed");
        {
            Listener focus(*range, Rml::EventId::Focus, [&](auto&) { range->SetId("renamed"); });
            rejects([&] { act("range", "range", "focus"); });
        }
        range->SetId("range");
        check(doc->Focus(), "Attached focus reset failed");
        {
            Listener focus(*range, Rml::EventId::Focus, [&](auto&) {
                range->SetAttribute("type", "password");
                range->SetAttribute("value", "widget-focus-secret");
            });
            rejects([&] { act("range", "range", "focus"); });
            check(
                p.range_controls("keys").dump().find("widget-focus-secret") == std::string::npos &&
                    p.form_controls("keys").dump().find("widget-focus-secret") == std::string::npos,
                "Post-focus kind change exposed password plaintext");
        }
        range->SetAttribute("type", "range");
        range->SetValue("2");
        check(doc->Focus(), "Attached focus reset failed");
        act("range", "range", "focus");
        {
            Rml::ElementPtr removed;
            Listener detach(
                *doc, Rml::EventId::Keydown,
                [&](auto& e) {
                    removed = doc->RemoveChild(range);
                    e.StopImmediatePropagation();
                },
                true);
            check(act("range", "range", "right").at("delivered") == true && bool(removed),
                  "Safely retained native key detach lost delivery");
            check(doc->Focus(), "Restore attached focus before releasing retained native owner");
        }
        p.release_input();
        std::string many = "<input id='only' type='range'/>";
        for (int i = 0; i < 65; ++i)
            many += "<input type='range'/>";
        doc->SetInnerRML(many);
        p.update(.1, 800, 700);
        rejects([&] {
            p.widget_keyboard_action("keys", "only", p.presentation_revision(), "range", "focus");
        });
        check(p.range_controls("keys").dump().find("widget-private-secret") == std::string::npos,
              "Keyboard observations exposed password plaintext");
    }
    check(asset_detail::read_bytes(root / "forge.assets.json", 4 * 1024 * 1024) == catalog &&
              asset_detail::read_bytes(root / "keys.rml", 4 * 1024 * 1024) ==
                  std::vector<std::byte>(
                      reinterpret_cast<const std::byte*>(markup.data()),
                      reinterpret_cast<const std::byte*>(markup.data() + markup.size())),
          "Native widget keyboard input wrote source/catalog");
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
        std::cout << "Native widget keyboard/focus/events/guards/source isolation "
                     "passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
