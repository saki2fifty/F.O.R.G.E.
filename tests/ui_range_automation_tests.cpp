#include "asset_bytes.hpp"
#include "ui_control_access.hpp"
#include "ui_test_renderer.hpp"
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
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
        throw std::runtime_error("Invalid slider action admitted at line " +
                                 std::to_string(where.line()));
}
Json slider(const Json& snapshot, const char* id) {
    for (const auto& row : snapshot.at("ranges"))
        if (row.at("id") == id)
            return row;
    throw std::runtime_error(std::string("Native slider missing: ") + id);
}
struct Listener final : Rml::EventListener {
    Rml::ObserverPtr<Rml::Element> element;
    Rml::EventId event;
    std::function<void(Rml::Event&)> callback;
    Listener(Rml::Element& source, Rml::EventId type, std::function<void(Rml::Event&)> fn)
        : element(source.GetObserverPtr()), event(type), callback(std::move(fn)) {
        source.AddEventListener(event, this);
    }
    ~Listener() override {
        if (element)
            element->RemoveEventListener(event, this);
    }
    void ProcessEvent(Rml::Event& value) override { callback(value); }
};
void exercise(const std::filesystem::path& root, const std::vector<std::byte>& font, bool shaping) {
    std::filesystem::create_directories(root);
    const std::string markup = R"rml(<rml><head><style>
body {font-family:Lato;font-size:18px;} input {display:block;width:260px;height:25px;}
#hidden {visibility:hidden;} #no-focus {focus:none;}
</style></head><body>
<input id="range" type="range" min="-2" max="8" step="2" value="1"/>
<input id="default" type="range"/>
<input id="vertical" type="range" orientation="vertical"/>
<input id="locked" type="range" disabled="disabled"/>
<input id="hidden" type="range"/><input id="no-focus" type="range"/>
<input id="duplicate" type="range"/><p id="duplicate">Other</p>
<input id="secret" type="password" value="range-native-secret"/>
<input id="upper" type="RANGE" value="Fallback text"/>
<input id="field" type="text"/><input id="check" type="checkbox"/>
<button id="button">Button</button>
</body></rml>)rml";
    std::ofstream(root / "ranges.rml") << markup;
    const auto asset = register_ui_document(root, "ranges.rml");
    const auto catalog = asset_detail::read_bytes(root / "forge.assets.json", 4 * 1024 * 1024);
    Json state{{"version", 1},
               {"session", "native-ranges"},
               {"generation", 1u},
               {"revision", 1u},
               {"documents", Json::array({{{"entity", EntityId::generate()},
                                           {"asset", asset.id},
                                           {"instance", "ranges"},
                                           {"visible", true},
                                           {"layer", 0u},
                                           {"model", Json::object()},
                                           {"commands", Json::array()}}})}};
    UiTestRenderer renderer;
    {
        UiPresenter presenter(renderer, root, font, {}, nullptr, shaping);
        presenter.reset("native-ranges", 1);
        check(presenter.accept(state), presenter.diagnostic().c_str());
        presenter.update(0, 800, 700);
        const auto initial = presenter.range_controls("ranges");
        const auto revision = initial.at("presentation_revision").get<std::uint64_t>();
        auto* document = Rml::GetContext(0)->GetDocument(0);
        auto* range =
            rmlui_dynamic_cast<Rml::ElementFormControlInput*>(document->GetElementById("range"));
        check(initial.at("ranges").size() == 7 && !initial.at("truncated").get<bool>(),
              "Range observation includes unrelated or case-folded native controls");
        check(slider(initial, "range").at("value") == "2.000000" &&
                  slider(initial, "range").at("parameters") ==
                      Json{{"min", -2}, {"max", 8}, {"step", 2}} &&
                  slider(initial, "default").at("parameters") ==
                      Json{{"min", 0}, {"max", 100}, {"step", 1}} &&
                  slider(initial, "vertical").at("orientation") == "vertical",
              "Native slider value/defaults/orientation differ");
        check(initial.dump().find("range-native-secret") == std::string::npos &&
                  !ui_range(document->GetElementById("upper")),
              "Range view exposed unrelated secret/fallback text");
        const auto set = [&](const char* id, double value) {
            return presenter.set_range_value("ranges", id, revision, value);
        };
        rejects([&] { presenter.range_controls("stale"); });
        rejects([&] { presenter.set_range_value("ranges", "range", revision + 1, 1); });
        for (const auto* id : {"missing", "locked", "hidden", "no-focus", "duplicate", "secret",
                               "upper", "field", "check", "button", ""})
            rejects([&] { set(id, 1); });
        for (double value : {std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN(), 1000001., -1000001.})
            rejects([&] { set("range", value); });
        int changes = 0;
        Listener count(*range, Rml::EventId::Change, [&](auto&) { ++changes; });
        check(set("range", 1).at("value_text_changed") == false && changes == 0 &&
                  range->GetAttribute<Rml::String>("value", "") == "1" &&
                  range->GetValue() == "2.000000",
              "Raw attribute replaced native value or wrapper fabricated change");
        check(set("range", 7).at("value_text_changed") == true && changes == 1 &&
                  range->GetValue() == "8.000000",
              "Native slider quantization/event was bypassed");
        for (int expected : {2, 3})
            check(set("range", 100).at("value_text_changed") == false && changes == expected,
                  "Repeated out-of-range native change was suppressed or duplicated");
        check(set("range", 8).at("delivered") == true && changes == 3,
              "Native exact-value delivery acquired a fabricated change event");
        check(set("range", -100).at("value_text_changed") == true && changes == 4 &&
                  range->GetValue() == "-2.000000",
              "Native lower bound behavior differs");
        presenter.key(Rml::Input::KI_LEFT, true, 0);
        rejects([&] { set("range", 1); });
        presenter.key(Rml::Input::KI_LEFT, false, 0);
        for (const auto* value : {"0", "-1", "0.0000001", "1000001"}) {
            range->SetAttribute("step", value);
            check(!slider(presenter.range_controls("ranges"), "range").at("editable").get<bool>(),
                  "Unsupported step advertised editable native state");
            rejects([&] { set("range", 3); });
        }
        range->SetAttribute("step", "2");
        range->SetAttribute("min", "10");
        range->SetAttribute("max", "10");
        set("range", 3);
        check(range->GetValue() == "10.000000",
              "Equal native bounds were reimplemented incorrectly");
        range->SetAttribute("min", "10");
        range->SetAttribute("max", "0");
        range->SetValue("3");
        const auto reversed = range->GetValue();
        check(slider(presenter.range_controls("ranges"), "range").at("parameters_available") ==
                  true,
              "Finite reversed native bounds were replaced by wrapper rules");
        set("range", 3);
        check(range->GetValue() == reversed,
              "Semantic setter disagreed with native reversed bounds");
        range->SetAttribute("max", "1000001");
        check(slider(presenter.range_controls("ranges"), "range").at("parameters").is_null(),
              "Out-of-profile native bounds were advertised as admitted");
        rejects([&] { set("range", 3); });
        range->SetAttribute("min", "-2");
        range->SetAttribute("max", "8");
        range->SetValue("0");
        check(document->Focus(), "Attached focus reset failed");
        {
            Listener focus(*range, Rml::EventId::Focus,
                           [&](auto&) { range->SetAttribute("step", "0"); });
            rejects([&] { set("range", 7); });
        }
        range->SetAttribute("step", "2");
        check(document->Focus(), "Attached focus reset failed");
        {
            Listener focus(*range, Rml::EventId::Focus, [&](auto&) { range->SetId("renamed"); });
            rejects([&] { set("range", 7); });
        }
        range->SetId("range");
        check(document->Focus(), "Attached focus reset failed");
        {
            Listener focus(*range, Rml::EventId::Focus, [&](auto&) {
                range->SetAttribute("type", "password");
                range->SetAttribute("value", "range-focus-secret");
            });
            rejects([&] { set("range", 7); });
            check(presenter.range_controls("ranges").dump().find("range-focus-secret") ==
                      std::string::npos,
                  "Focus kind change leaked secret value");
        }
        range->SetAttribute("type", "range");
        range->SetValue("0");
        check(document->Focus(), "Attached focus reset failed");
        {
            Rml::ElementPtr removed;
            Listener change(*range, Rml::EventId::Change, [&](auto&) {
                if (!removed)
                    removed = document->RemoveChild(range);
            });
            const auto result = set("range", 7);
            check(removed && result.at("delivered") == true &&
                      result.at("value_text_changed").is_null(),
                  "Safely retained native detach invented value confirmation");
            check(document->Focus(),
                  "Restore native attached focus before releasing retained owner");
        }
        presenter.release_input();
        std::string many = "<input type='text'/><input type='checkbox'/><button>Button</button>";
        for (int i = 0; i < 65; ++i)
            many += "<input type='range'/>";
        document->SetInnerRML(many);
        presenter.update(.1, 800, 700);
        check(presenter.range_controls("ranges").at("truncated") == true &&
                  presenter.range_controls("ranges").at("ranges").size() == 64 &&
                  presenter.text_fields("ranges").at("truncated") == false &&
                  presenter.form_controls("ranges").at("truncated") == false &&
                  presenter.buttons("ranges").at("truncated") == false,
              "Range count consumed existing independent control budgets");
        many = "<input id='only' type='range'/>";
        for (int i = 0; i < 65; ++i)
            many += "<input type='text'/><input type='checkbox'/><button>Button</button>";
        document->SetInnerRML(many);
        presenter.update(.2, 800, 700);
        check(presenter.range_controls("ranges").at("truncated") == false &&
                  presenter.set_range_value("ranges", "only", presenter.presentation_revision(), 3)
                          .at("delivered") == true,
              "Existing control count truncated independent range access");
        check(document->Focus(), "Node budget focus reset failed");
        many = "<input id='only' type='range'/>";
        for (int i = 0; i < 4100; ++i)
            many += "<p/>";
        document->SetInnerRML(many);
        presenter.update(.3, 800, 700);
        const auto bounded = presenter.range_controls("ranges");
        check(bounded.at("visited_nodes") == 4096 && bounded.at("truncated") == true &&
                  slider(bounded, "only").at("addressable") == false,
              "Range node budget or unique identity admission differs");
        rejects([&] {
            presenter.set_range_value("ranges", "only", presenter.presentation_revision(), 3);
        });
    }
    check(asset_detail::read_bytes(root / "forge.assets.json", 4 * 1024 * 1024) == catalog &&
              asset_detail::read_bytes(root / "ranges.rml", 4 * 1024 * 1024) ==
                  std::vector<std::byte>(
                      reinterpret_cast<const std::byte*>(markup.data()),
                      reinterpret_cast<const std::byte*>(markup.data() + markup.size())),
          "Native slider action wrote authored source/catalog");
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
        std::cout << "Native slider value/events/admission/scope/budgets/source isolation passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
