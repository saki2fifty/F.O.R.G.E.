#include "asset_bytes.hpp"
#include "ui_test_renderer.hpp"
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <fstream>
#include <iostream>

namespace {
using namespace forge;
using Json = nlohmann::json;
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F operation) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Invalid native field action was admitted");
}
Json field(const Json& state, const char* id) {
    for (const auto& row : state.at("fields"))
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
                  field(initial, "text").at("value") == "old" && initial.at("keys").size() == 10,
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
        const auto removed = act("text", "backspace");
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
