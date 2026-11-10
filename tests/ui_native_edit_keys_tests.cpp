#include "asset_bytes.hpp"
#include "ui_control_access.hpp"
#include "ui_test_renderer.hpp"
#include "ui_text_boundaries.hpp"
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Elements/ElementFormControlTextArea.h>
#include <array>
#include <forge/ui_assets.hpp>
#include <fstream>
#include <iostream>

namespace {
using namespace forge;
using Json = nlohmann::json;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
struct KeyCase {
    const char* name;
    int code, modifiers;
};
const std::array cases{
    KeyCase{"up", Rml::Input::KI_UP, 0},
    KeyCase{"down", Rml::Input::KI_DOWN, 0},
    KeyCase{"select_up", Rml::Input::KI_UP, Rml::Input::KM_SHIFT},
    KeyCase{"select_down", Rml::Input::KI_DOWN, Rml::Input::KM_SHIFT},
    KeyCase{"page_up", Rml::Input::KI_PRIOR, 0},
    KeyCase{"page_down", Rml::Input::KI_NEXT, 0},
    KeyCase{"select_page_up", Rml::Input::KI_PRIOR, Rml::Input::KM_SHIFT},
    KeyCase{"select_page_down", Rml::Input::KI_NEXT, Rml::Input::KM_SHIFT},
    KeyCase{"select_home", Rml::Input::KI_HOME, Rml::Input::KM_SHIFT},
    KeyCase{"select_end", Rml::Input::KI_END, Rml::Input::KM_SHIFT},
    KeyCase{"document_home", Rml::Input::KI_HOME, Rml::Input::KM_CTRL},
    KeyCase{"document_end", Rml::Input::KI_END, Rml::Input::KM_CTRL},
    KeyCase{"select_document_home", Rml::Input::KI_HOME,
            Rml::Input::KM_CTRL | Rml::Input::KM_SHIFT},
    KeyCase{"select_document_end", Rml::Input::KI_END, Rml::Input::KM_CTRL | Rml::Input::KM_SHIFT},
    KeyCase{"word_backspace", Rml::Input::KI_BACK, Rml::Input::KM_CTRL},
    KeyCase{"word_delete", Rml::Input::KI_DELETE, Rml::Input::KM_CTRL}};
struct Trace final : Rml::EventListener {
    Json events = Json::array();
    unsigned changes = 0;
    bool cancel = false;
    void ProcessEvent(Rml::Event& event) override {
        if (event.GetId() == Rml::EventId::Change) {
            ++changes;
            return;
        }
        events.push_back({{"down", event.GetId() == Rml::EventId::Keydown},
                          {"code", event.GetParameter<int>("key_identifier", 0)},
                          {"ctrl", event.GetParameter<int>("ctrl_key", 0) != 0},
                          {"shift", event.GetParameter<int>("shift_key", 0) != 0},
                          {"target", event.GetTargetElement()->GetId()}});
        if (cancel && event.GetId() == Rml::EventId::Keydown)
            event.StopImmediatePropagation();
    }
    void reset() {
        events = Json::array();
        changes = 0;
    }
};
Json find(const Json& snapshot, const char* id) {
    for (const auto& field : snapshot.at("fields"))
        if (field.at("id") == id)
            return field;
    throw std::runtime_error("Native field observation missing");
}
void exercise(const std::filesystem::path& root, const std::vector<std::byte>& font, bool shaping,
              bool editing) {
    std::filesystem::create_directories(root);
    std::ofstream(root / "keys.rml") << R"rml(<rml><head><style>
body {display:block;width:100%;height:100%;font-family:Lato;font-size:24px;}
input,textarea {display:block;width:320px;height:80px;}
textarea {white-space:pre-wrap;}
</style></head><body><input id="text" type="text" value="abc"/>
<input id="password" type="password" value="abc"/><textarea id="area">abc</textarea>
</body></rml>)rml";
    const auto saved = asset_detail::read_bytes(root / "keys.rml", 65536);
    const auto asset = register_ui_document(root, "keys.rml");
    UiTestRenderer renderer;
    {
        UiPresenter presenter(renderer, root, font, {}, nullptr, shaping, editing);
        const auto entity = EntityId::generate();
        presenter.reset("native-edit-keys", 1);
        const Json snapshot{{"version", 1},
                            {"session", "native-edit-keys"},
                            {"generation", 1u},
                            {"revision", 1u},
                            {"documents", Json::array({{{"entity", entity},
                                                        {"asset", asset.id},
                                                        {"instance", "keys"},
                                                        {"visible", true},
                                                        {"layer", 0u},
                                                        {"model", Json::object()},
                                                        {"commands", Json::array()}}})}};
        const bool accepted = presenter.accept(snapshot);
        check(accepted, presenter.diagnostic().c_str());
        presenter.update(0, 800, 600);
        presenter.render();
        check(renderer.draws > 0, "Native edit-key fixture has no admitted font/layout");
        auto* document = Rml::GetContext(0)->GetDocument(0);
        Trace trace;
        for (const auto event : {Rml::EventId::Keydown, Rml::EventId::Keyup, Rml::EventId::Change})
            document->AddEventListener(event, &trace, true);
        const auto revision = presenter.presentation_revision();
        double time = 0;
        for (const char* id : {"text", "password", "area"}) {
            const auto field = ui_text_field(document->GetElementById(id));
            check(bool(field), "Native edit-key field missing");
            std::visit(
                [&](auto* control) {
                    const auto check_corpus = [&](const std::string& value, int steps) {
                        const auto prepare = [&] {
                            check(control->Focus(), "Native edit-key focus failed");
                            presenter.key(Rml::Input::KI_A, true, Rml::Input::KM_CTRL);
                            presenter.key(Rml::Input::KI_A, false, Rml::Input::KM_CTRL);
                            presenter.text(value);
                            presenter.update(time += .02, 800, 600);
                            presenter.key(Rml::Input::KI_HOME, true, Rml::Input::KM_CTRL);
                            presenter.key(Rml::Input::KI_HOME, false, Rml::Input::KM_CTRL);
                            for (int step = 0; step < steps; ++step) {
                                presenter.key(Rml::Input::KI_RIGHT, true, 0);
                                presenter.key(Rml::Input::KI_RIGHT, false, 0);
                            }
                            presenter.update(time += .02, 800, 600);
                            presenter.render();
                            trace.reset();
                        };
                        for (const auto& key : cases) {
                            prepare();
                            presenter.key(key.code, true, key.modifiers);
                            presenter.key(key.code, false, key.modifiers);
                            const auto expected = find(presenter.text_fields("keys"), id);
                            const auto expected_events = trace.events;
                            const auto expected_changes = trace.changes;
                            prepare();
                            const auto result =
                                presenter.text_field_action("keys", id, revision, key.name);
                            if (editing && key.code != Rml::Input::KI_BACK &&
                                key.code != Rml::Input::KI_DELETE) {
                                const auto boundaries = ui_grapheme_boundaries(control->GetValue());
                                int begin = 0, end = 0;
                                control->GetSelection(&begin, &end, nullptr);
                                if (!std::binary_search(boundaries.begin(), boundaries.end(),
                                                        begin) ||
                                    !std::binary_search(boundaries.begin(), boundaries.end(), end))
                                    throw std::runtime_error(
                                        std::string("Native navigation split a character: ") + id +
                                        "/" + key.name);
                            }
                            if (!(result.at("delivered") && find(result, id) == expected &&
                                  trace.events == expected_events &&
                                  trace.changes == expected_changes))
                                throw std::runtime_error(
                                    std::string("Native key mismatch ") + id + "/" + key.name +
                                    " shaping=" + std::to_string(shaping) + " editing=" +
                                    std::to_string(editing) + " expected=" + expected.dump() +
                                    " actual=" + find(result, id).dump() +
                                    " events=" + trace.events.dump() +
                                    " expected_events=" + expected_events.dump());
                            check(trace.events.size() == 2 && trace.events[0].at("down") == true &&
                                      trace.events[1].at("down") == false &&
                                      trace.events[0].at("code") == key.code,
                                  "Named edit key lost its native paired dispatch");
                            if (std::string_view(id) == "password")
                                check(!find(result, id).contains("value"),
                                      "Modified key exposed password text");
                            prepare();
                            const auto before = find(presenter.text_fields("keys"), id);
                            trace.cancel = true;
                            const auto canceled =
                                presenter.text_field_action("keys", id, revision, key.name);
                            trace.cancel = false;
                            check(find(canceled, id) == before && trace.changes == 0 &&
                                      trace.events.size() == 2,
                                  "Canceled named edit key changed a native field or lost key "
                                  "release");
                        }
                    };
                    check_corpus("one two\nthree four\nfive six\nseven eight", 10);
                    const auto unicode = std::u8string_view(
                        u8"a\u0301b\nWWa\u0301b\nc\u0301d\n\U0001f469\u200d\U0001f4bbx\nq\u0301r");
                    for (const int steps : {1, 4, 10})
                        check_corpus(
                            {reinterpret_cast<const char*>(unicode.data()), unicode.size()}, steps);
                    for (const auto& key : std::array{
                             KeyCase{"word_backspace", Rml::Input::KI_BACK, Rml::Input::KM_CTRL},
                             KeyCase{"word_delete", Rml::Input::KI_DELETE, Rml::Input::KM_CTRL}}) {
                        const auto partial = [&] {
                            check(control->Focus(), "Selected deletion focus failed");
                            control->SetValue(
                                {reinterpret_cast<const char*>(unicode.data()), unicode.size()});
                            control->SetSelectionRange(1, 2);
                            presenter.update(time += .02, 800, 600);
                            trace.reset();
                        };
                        partial();
                        const auto original = control->GetValue();
                        const auto expected = editing ? original.substr(3)
                                                      : original.substr(0, 1) + original.substr(3);
                        presenter.key(key.code, true, key.modifiers);
                        presenter.key(key.code, false, key.modifiers);
                        check(control->GetValue() == expected && trace.changes == 1,
                              "Raw selected word key lost its boundary/native change policy");
                        const auto raw_events = trace.events;
                        partial();
                        const auto delivered =
                            presenter.text_field_action("keys", id, revision, key.name);
                        check(delivered.at("delivered") && control->GetValue() == expected &&
                                  trace.events == raw_events && trace.changes == 1,
                              "Named selected deletion diverged from the native keyboard");
                        partial();
                        trace.cancel = true;
                        presenter.text_field_action("keys", id, revision, key.name);
                        trace.cancel = false;
                        int begin = 0, end = 0;
                        control->GetSelection(&begin, &end, nullptr);
                        check(control->GetValue() == original && begin == 1 && end == 2 &&
                                  trace.changes == 0,
                              "Canceled selected named word key changed native state");
                    }
                },
                *field);
        }
        for (const auto event : {Rml::EventId::Keydown, Rml::EventId::Keyup, Rml::EventId::Change})
            document->RemoveEventListener(event, &trace, true);
        check(presenter.diagnostic().empty(), presenter.diagnostic().c_str());
    }
    check(renderer.geometry.empty() && renderer.textures.empty(),
          "Native edit-key resources survived retirement");
    check(asset_detail::read_bytes(root / "keys.rml", 65536) == saved,
          "Native edit keys changed authored UI source");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need scratch and default UI font");
        const auto root = std::filesystem::absolute(argv[1]);
        const auto font = forge::asset_detail::read_bytes(argv[2], 4 * 1024 * 1024);
        for (bool shaping : {false, true})
            for (bool editing : {false, true})
                exercise(root / forge::AssetId::generate().str(), font, shaping, editing);
        std::cout << "Native edit-key pairing, values, selection, cancellation, password and "
                     "retirement passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
