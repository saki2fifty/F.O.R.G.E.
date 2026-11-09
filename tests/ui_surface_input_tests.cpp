#include "asset_bytes.hpp"
#include "ui_test_renderer.hpp"
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <forge/runtime_ui.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
Rml::Element* element(const char* id) {
    for (int i = 0; i < Rml::GetNumContexts(); ++i) {
        auto* context = Rml::GetContext(i);
        for (int d = 0; d < context->GetNumDocuments(); ++d)
            if (auto* result = context->GetDocument(d)->GetElementById(id))
                return result;
    }
    throw std::runtime_error(std::string("Missing native element: ") + id);
}
} // namespace
int main(int argc, char** argv) {
    try {
        const bool editing = argc == 4 && std::string_view(argv[3]) == "--grapheme-editing";
        check(argc == 3 || editing, "Need input scratch, default font and optional editing mode");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        struct Cleanup {
            std::filesystem::path root;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove_all(root, ec);
            }
        } cleanup{root};
        std::ofstream(root / "Main.rml") << R"rml(<rml><head><style>
body {font-family:Lato;pointer-events:none;margin:0;}
button {position:absolute;left:210px;top:110px;width:60px;height:24px;background-color:#acf;pointer-events:auto;}
</style></head><body><button id="main" data-event-click="command('Step')">Main</button></body></rml>)rml";
        std::ofstream(root / "Output.rml") << R"rml(<rml><head><style>
body {font-family:Lato;pointer-events:none;margin:0;}
button,input {display:block;position:absolute;left:10px;pointer-events:auto;background-color:#acf;}
button {top:10px;width:80px;height:24px;tab-index:auto;}input {top:50px;width:100px;height:25px;}
</style></head><body><button id="surface" data-event-click="command('Step')">Surface</button><input id="entry" type="text" value="old"/></body></rml>)rml";
        const auto main = register_ui_document(root, "Main.rml"),
                   output = register_ui_document(root, "Output.rml");
        const auto image = AssetId::generate();
        const auto surface = EntityId::generate();
        UiTestRenderer renderer;
        std::array<float, 3> ime{};
        UiPlatformCallbacks platform;
        platform.activate_text = [&](float x, float y, float h) { ime = {x, y, h}; };
        UiPresenter presenter(renderer, root, asset_detail::read_bytes(argv[2], 4 * 1024 * 1024),
                              platform, nullptr, false, editing);
        presenter.reset("surface-input", 1);
        auto row = [](const AssetRecord& asset, const char* instance) {
            return Json{{"entity", EntityId::generate()},
                        {"asset", asset.id},
                        {"instance", instance},
                        {"visible", true},
                        {"layer", 0u},
                        {"model", {{"tick", 0u}, {"paused", true}}},
                        {"commands", {"Pause", "Resume", "Step"}}};
        };
        auto routed = row(output, "output");
        routed["render_texture"] = image;
        Json state{{"version", 2},
                   {"session", "surface-input"},
                   {"generation", 1u},
                   {"revision", 1u},
                   {"documents", Json::array({row(main, "main"), routed})}};
        check(presenter.accept(state), presenter.diagnostic().c_str());
        presenter.update(0, 480, 300);
        presenter.update_output({image}, 160, 100);
        presenter.render();
        presenter.render_output({image});
        bool alive = true;
        double surface_x = 200;
        unsigned retained = 0, picks = 0;
        UiSurfaceCallbacks callbacks;
        callbacks.pick = [&](int x, int y,
                             std::optional<UiSurfaceHit> captured) -> std::optional<UiSurfaceHit> {
            ++picks;
            if (captured) {
                check(captured->surface == surface, "Capture changed its surface identity");
                ++retained;
            }
            if (!alive ||
                (!captured && (x < surface_x || x >= surface_x + 160 || y < 100 || y >= 200)))
                return {};
            return UiSurfaceHit{surface, {image}, {(x - surface_x) / 160., (y - 100) / 100.}};
        };
        callbacks.project = [&](const UiSurfaceHit& hit) -> std::optional<std::array<double, 2>> {
            if (!alive || hit.surface != surface || hit.output.id != image)
                return {};
            return std::array<double, 2>{surface_x + 160 * hit.uv[0], 100 + 100 * hit.uv[1]};
        };
        presenter.surface_input(callbacks);
        if (editing) {
            const auto fields = presenter.text_fields("output");
            bool refused = false;
            try {
                presenter.text_field_action("output", "entry", fields.at("presentation_revision"),
                                            "focus");
            } catch (const std::exception&) {
                refused = true;
            }
            check(refused, "Output field automation invented a missing native surface hit");
        }
        bool rejected = false;
        try {
            presenter.surface_input({callbacks.pick, {}});
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check(rejected, "Partial surface callback registration was accepted");
        ui_protocol::CommandGate gate;
        gate.reset("surface-input", 1);
        unsigned command_count = 0;
        const auto command = [&](const char* instance) {
            ++command_count;
            const auto value = presenter.pending_command();
            if (!value || value->at("instance") != instance || value->at("command") != "Step")
                throw std::runtime_error(std::string("Native command expected ") + instance + ": " +
                                         (value ? value->dump() : "none") +
                                         "; stage=" + std::to_string(command_count) + "; " +
                                         presenter.diagnostic());
            presenter.acknowledge(gate.dispatch(*value, [](const Json&) {}));
            check(!presenter.pending_command(), "One native gesture produced duplicate commands");
        };
        const auto click = [&](int x, int y) {
            check(presenter.mouse_move(x, y, 0), "UI pointer was not consumed");
            check(presenter.mouse_button(0, true, 0) && presenter.mouse_button(0, false, 0),
                  "UI click was not consumed");
        };
        const auto before = picks;
        click(220, 120);
        command("main");
        check(picks == before, "Main UI did not take precedence over a surface");
        state["revision"] = 2u;
        state["documents"][0]["visible"] = false;
        check(presenter.accept(state), presenter.diagnostic().c_str());
        presenter.update(.05, 480, 300);
        presenter.update_output({image}, 160, 100);
        presenter.render();
        presenter.render_output({image});
        click(220, 120);
        command("output");
        auto* output_context = element("surface")->GetContext();
        check(output_context->GetHoverElement() == element("surface"), "Surface hover missing");
        surface_x = 300;
        presenter.update(.1, 480, 300);
        presenter.update_output({image}, 160, 100);
        check(output_context->GetHoverElement() != element("surface"),
              "A moving surface retained hover without pointer motion");
        surface_x = 200;
        presenter.update(.15, 480, 300);
        presenter.update_output({image}, 160, 100);
        check(output_context->GetHoverElement() == element("surface"),
              "A returning surface did not recover stationary pointer hover");
        presenter.update_output({image}, 320, 200);
        check(output_context->GetHoverElement() != element("surface"),
              "Resized output retained stale pointer pixels");
        click(210, 110);
        command("output");
        presenter.update_output({image}, 160, 100);
        state["revision"] = 3u;
        state["documents"][0]["visible"] = true;
        element("main")->SetProperty("visibility", "hidden");
        check(presenter.accept(state), presenter.diagnostic().c_str());
        presenter.update(.2, 480, 300);
        presenter.update_output({image}, 160, 100);
        presenter.mouse_move(220, 120, 0);
        element("main")->SetProperty("visibility", "visible");
        presenter.update(.25, 480, 300);
        presenter.update_output({image}, 160, 100);
        check(output_context->GetHoverElement() != element("surface"),
              "New main UI did not take stationary-pointer precedence");
        element("main")->SetProperty("visibility", "hidden");
        presenter.update(.3, 480, 300);
        presenter.update_output({image}, 160, 100);
        presenter.mouse_move(220, 120, 0);
        presenter.mouse_button(0, true, 0);
        check(presenter.mouse_move(100, 80, 0) && retained > 0, "Drag lost its captured surface");
        check(presenter.mouse_button(0, false, 0) && !presenter.pending_command(),
              "Release outside surface clicked or lost its owned release");
        click(220, 160);
        check(presenter.wants_text(), "Output input did not own text focus");
        check(ime[0] >= 200 && ime[0] < 360 && ime[1] >= 100 && ime[1] < 200 && ime[2] > 0,
              "Output text input anchor did not project into host coordinates");
        presenter.key(Rml::Input::KI_A, true, Rml::Input::KM_CTRL);
        presenter.key(Rml::Input::KI_A, false, Rml::Input::KM_CTRL);
        check(presenter.text("typed"), "Focused output rejected text input");
        check(element("entry")->GetAttribute<Rml::String>("value", "") == "typed",
              "Text input changed the wrong native context");
        if (editing) {
            const auto fields = presenter.text_fields("output");
            check(presenter
                      .text_field_action("output", "entry", fields.at("presentation_revision"),
                                         "focus")
                      .at("delivered"),
                  "Current native surface hit was not reused by field focus");
            auto* field = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(element("entry"));
            check(field != nullptr, "Output text field is missing");
            field->SetValue("a\xcc\x81"
                            "b");
            field->SetSelectionRange(2, 2);
            presenter.key(Rml::Input::KI_LEFT, true, 0);
            presenter.key(Rml::Input::KI_LEFT, false, 0);
            int begin = -1, end = -1;
            field->GetSelection(&begin, &end, nullptr);
            check(begin == 0 && end == 0, "Output arrow split a combining character");
            presenter.key(Rml::Input::KI_DELETE, true, 0);
            presenter.key(Rml::Input::KI_DELETE, false, 0);
            check(field->GetValue() == "b", "Output deletion split a combining character");
        }
        presenter.navigate("next");
        if (output_context->GetFocusElement() != element("surface"))
            throw std::runtime_error("Output navigation focus is " +
                                     output_context->GetFocusElement()->GetTagName() + ":" +
                                     output_context->GetFocusElement()->GetId());
        presenter.navigate("accept");
        command("output");
        check(!presenter.wants_text(), "Native output navigation retained field focus");
        presenter.mouse_move(220, 120, 0);
        presenter.mouse_button(0, true, 0);
        alive = false;
        check(presenter.mouse_move(230, 120, 0),
              "Missing captured surface leaked gameplay pointer motion");
        presenter.mouse_button(0, false, 0);
        check(!presenter.pending_command() && !presenter.wants_text(),
              "Disappearing surface retained input or clicked on cancellation");
        alive = true;
        click(220, 160);
        check(presenter.wants_text(), "Surface focus did not recover");
        alive = false;
        presenter.update(.35, 480, 300);
        check(!presenter.wants_text() && output_context->GetFocusElement() != element("entry"),
              "A disappearing surface retained focus without pointer motion");
        alive = true;
        click(220, 160);
        presenter.update(.4, 640, 400);
        check(!presenter.wants_text(), "Viewport resize retained an obsolete surface focus");
        presenter.release_input();
        check(!presenter.wants_text(), "Capture release retained output text focus");
        state["revision"] = 4u;
        state["documents"].erase(1);
        check(presenter.accept(state) && !presenter.has_output({image}),
              "Removed output remained addressable");
        check(!presenter.wants_text(), "Removed output retained native text focus");
        const auto* retained_main = element("main");
        auto invalid = callbacks;
        invalid.pick = [&](int, int, std::optional<UiSurfaceHit>) -> std::optional<UiSurfaceHit> {
            return UiSurfaceHit{surface, {image}, {0, 0}};
        };
        presenter.surface_input(std::move(invalid));
        check(!presenter.mouse_move(400, 250, 0) && !presenter.diagnostic().empty() &&
                  element("main") == retained_main,
              "Invalid output hit was accepted or replaced the usable document");
        presenter.reset("retired", 2);
        check(Rml::GetNumContexts() == 0 && renderer.geometry.empty() && renderer.textures.empty(),
              "Surface input retained native resources");
        std::cout << "Native main/output precedence, pointer capture/release, focus, text, IME "
                     "mapping, navigation and retirement passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
