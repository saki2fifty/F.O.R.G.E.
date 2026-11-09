#include "runtime_ui_host.hpp"
#include "runtime_ui_input.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <fstream>
#include <iostream>
#include <memory>
using namespace forge;
void check(bool v, const char* why) {
    if (!v)
        throw std::runtime_error(why);
}
struct Renderer : Rml::RenderInterface {
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>,
                                                Rml::Span<const int>) override {
        return 1;
    }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override { return 0; }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override {
        return 1;
    }
    void ReleaseTexture(Rml::TextureHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
};
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Need runtime, font and temporary directory");
#ifndef _WIN32
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
#endif
        const bool initialized = SDL_Init(SDL_INIT_VIDEO);
        check(initialized, SDL_GetError());
        struct Quit {
            ~Quit() { SDL_Quit(); }
        } quit;
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> native_window(
            SDL_CreateWindow("FORGE UI touch regression", 640, 480, SDL_WINDOW_HIDDEN),
            SDL_DestroyWindow);
        auto* window = native_window.get();
        check(window, SDL_GetError());
        auto root = std::filesystem::path(argv[3]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        std::ofstream(root / "hud.rml")
            << R"(<rml><head><style>body {font-family:Lato;font-size:18px;pointer-events:none;} input {width:120px;height:40px;pointer-events:auto;} button {width:120px;height:40px;pointer-events:auto;}</style></head><body><input id="field" type="text" maxlength="3"/><button id="button">Button</button></body></rml>)";
        auto asset = register_ui_document(root, "hud.rml");
        std::ifstream f(argv[2], std::ios::binary);
        std::string bytes{std::istreambuf_iterator<char>(f), {}};
        std::vector<std::byte> font(bytes.size());
        std::memcpy(font.data(), bytes.data(), bytes.size());
        Renderer renderer;
        TextInputMethodEditor_SDL ime;
        UiPresenter presenter(renderer, root, std::move(font), {}, &ime, false, true);
        presenter.reset("test", 1);
        auto entity = EntityId::generate();
        check(presenter.accept({{"version", 1},
                                {"session", "test"},
                                {"generation", 1u},
                                {"revision", 1u},
                                {"documents", Json::array({{{"entity", entity},
                                                            {"asset", asset.id},
                                                            {"instance", entity.str() + ":1"},
                                                            {"visible", true},
                                                            {"layer", 0u},
                                                            {"model", Json::object()},
                                                            {"commands", Json::array()}}})}}),
              presenter.diagnostic().c_str());
        presenter.update(1, 640, 480);
        PlaySession play;
        auto action = ActionId::generate();
        play.configure(
            60,
            InputMap(
                {{"version", 1},
                 {"actions",
                  Json::array({{{"id", action},
                                {"name", "UI leak"},
                                {"kind", "digital"},
                                {"bindings", Json::array({{{"control", "key.w"}},
                                                          {{"control", "mouse.left"}},
                                                          {{"control", "touch.0.down"}}})}}})}}));
        play.start(argv[1], {{"version", 1}, {"entities", Json::array()}}, true);
        auto wait = [&](auto done) {
            auto end = SDL_GetTicks() + 5000;
            while (!done() && play.active() && SDL_GetTicks() < end) {
                play.pump();
                SDL_Delay(1);
            }
            check(play.active() && done(), play.status().c_str());
        };
        wait([&] { return play.control_ready(); });
        GameInput game(window);
        game.pump(play, true);
        game.capture(play);
        RuntimeUiInput input;
        input.bounds({0, 0}, {640, 480}, 640, 480);
        auto route = [&](SDL_Event e) {
            if (!input.event(e, presenter, ime, game, play))
                game.event(e, play);
        };
        auto tick = [&] {
            wait([&] { return play.control_ready(); });
            auto before = play.timing().at("tick").get<std::uint64_t>();
            play.step();
            wait([&] { return play.timing().at("tick").get<std::uint64_t>() > before; });
            return play.input_status().at("actions")[0];
        };
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.scancode = SDL_SCANCODE_W;
        e.key.key = SDLK_W;
        e.key.down = true;
        route(e);
        check(tick()["held"], "Unconsumed UI key reaches gameplay");
        auto* doc = Rml::GetContext(0)->GetDocument(0);
        doc->GetElementById("field")->Focus();
        check(presenter.wants_text(), "Text field focus");
        e.type = SDL_EVENT_KEY_UP;
        e.key.down = false;
        route(e);
        check(!tick()["held"].get<bool>(),
              "UI-consumed release neutralizes previous gameplay press");
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.down = true;
        route(e);
        auto state = tick();
        check(!state["held"].get<bool>() && state["presses"] == 1,
              "Typing W does not move gameplay");
        auto* field =
            rmlui_dynamic_cast<Rml::ElementFormControlInput*>(doc->GetElementById("field"));
        check(field != nullptr, "Native routed field is missing");
        SDL_Event text_event{};
        text_event.type = SDL_EVENT_TEXT_INPUT;
        text_event.text.text = "a\xcc\x81"
                               "b";
        route(text_event);
        check(field->GetValue() == "a\xcc\x81"
                                   "b",
              "SDL text did not reach the native field");
        const auto plain_key = [&](SDL_Scancode scan, SDL_Keycode code) {
            SDL_Event value{};
            value.type = SDL_EVENT_KEY_DOWN;
            value.key.scancode = scan;
            value.key.key = code;
            value.key.down = true;
            route(value);
            value.type = SDL_EVENT_KEY_UP;
            value.key.down = false;
            route(value);
        };
        plain_key(SDL_SCANCODE_HOME, SDLK_HOME);
        plain_key(SDL_SCANCODE_RIGHT, SDLK_RIGHT);
        int begin = -1, end = -1;
        field->GetSelection(&begin, &end, nullptr);
        check(begin == 2 && end == 2, "SDL arrow split a combining character");
        plain_key(SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE);
        check(field->GetValue() == "b", "SDL Backspace split a combining character");
        field->SetValue("ab");
        field->SetSelectionRange(2, 2);
        SDL_Event preedit{};
        preedit.type = SDL_EVENT_TEXT_EDITING;
        preedit.edit.text = "cd";
        preedit.edit.start = 0;
        preedit.edit.length = 2;
        route(preedit);
        check(field->GetValue() == "abcd", "SDL preedit was not delegated to the native context");
        preedit.edit.text = "";
        preedit.edit.length = 0;
        route(preedit);
        text_event.text.text = "xyz";
        route(text_event);
        check(field->GetValue() == "abx", "Actual SDL composition commit bypassed maximum length");
        check(!tick()["held"].get<bool>(), "Native character editing leaked into gameplay");
        presenter.release_input();
        e.type = SDL_EVENT_KEY_UP;
        e.key.down = false;
        check(input.event(e, presenter, ime, game, play),
              "UI-owned matching key release consumed after blur");
        check(!tick()["held"].get<bool>(), "Key release remains neutral");
        auto pos = doc->GetElementById("button")->GetAbsoluteOffset();
        e = {};
        e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        e.button.down = true;
        e.button.button = SDL_BUTTON_LEFT;
        e.button.x = pos.x + 5;
        e.button.y = pos.y + 5;
        route(e);
        check(!tick()["held"].get<bool>(), "UI click does not fire gameplay");
        e.type = SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.down = false;
        e.button.x = 600;
        e.button.y = 450;
        check(input.event(e, presenter, ime, game, play),
              "UI-owned mouse release consumed outside button");
        doc->GetElementById("field")->Focus();
        e = {};
        e.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        route(e);
        check(!game.captured() && !presenter.wants_text() && !tick()["held"].get<bool>(),
              "Focus loss clears UI and gameplay");
        game.capture(play);
        doc->GetElementById("field")->Focus();
        auto before = play.timing().at("tick").get<std::uint64_t>();
        wait([&] { return play.control_ready(); });
        e = {};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.scancode = SDL_SCANCODE_F7;
        e.key.down = true;
        route(e);
        wait([&] { return play.timing().at("tick").get<std::uint64_t>() > before; });
        check(play.timing()["tick"] == before + 1 && play.paused(),
              "Reserved F7 steps exactly once through UI focus");
        e.key.scancode = SDL_SCANCODE_ESCAPE;
        route(e);
        check(!game.captured() && !presenter.wants_text(), "Escape releases both input owners");
        game.capture(play);
        auto touch = [&](SDL_EventType type, float x, float y) {
            SDL_Event value{};
            value.type = type;
            value.tfinger.windowID = SDL_GetWindowID(window);
            value.tfinger.touchID = 15;
            value.tfinger.fingerID = 7;
            value.tfinger.x = x;
            value.tfinger.y = y;
            value.tfinger.pressure = .5f;
            route(value);
        };
        e = {};
        e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        e.button.which = SDL_TOUCH_MOUSEID;
        e.button.button = SDL_BUTTON_LEFT;
        e.button.down = true;
        e.button.x = pos.x + 5;
        e.button.y = pos.y + 5;
        route(e);
        touch(SDL_EVENT_FINGER_DOWN, e.button.x / 640, e.button.y / 480);
        check(!tick()["held"].get<bool>(), "UI-owned touch leaked into gameplay");
        e.type = SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.down = false;
        route(e);
        touch(SDL_EVENT_FINGER_UP, .5f, .5f);
        touch(SDL_EVENT_FINGER_DOWN, .8f, .8f);
        check(tick()["held"].get<bool>(), "Unclaimed captured touch did not reach gameplay");
        input.reset(play);
        touch(SDL_EVENT_FINGER_MOTION, .7f, .7f);
        check(!tick()["held"].get<bool>(), "UI neutral reset revived a held touch");
        play.stop();
        // Exercise the editor process bridge itself, including a queued old UI command
        // superseded by native activation (generation change).
        play.configure(60, InputMap{}, {0, -9.81, 0}, root);
        Json authored = {{"version", 1},
                         {"entities", Json::array({{{"id", "hud"},
                                                    {"name", "HUD"},
                                                    {"components",
                                                     {{"forge.ui_document",
                                                       {{"document", asset.id},
                                                        {"enabled", true},
                                                        {"visible", true},
                                                        {"layer", 0}}}}}}})}};
        play.start(argv[1], authored);
        wait([&] {
            return play.control_ready() && !play.ui_snapshot().is_null() &&
                   !play.ui_snapshot().at("documents").empty();
        });
        play.pause();
        wait([&] { return play.control_ready() && play.paused(); });
        auto request = [&](unsigned id) {
            const auto& state = play.ui_snapshot();
            return Json{{"version", 1},
                        {"session", play.session()},
                        {"generation", state.at("generation")},
                        {"id", id},
                        {"instance", state.at("documents")[0].at("instance")},
                        {"command", "Step"}};
        };
        check(play.submit_ui(request(1)), "UI request queued through editor");
        wait([&] { return !play.ui_ack().is_null(); });
        check(play.ui_ack().at("ok"), "Editor observes UI acknowledgement");
        wait([&] { return play.control_ready(); });
        play.stop();
        std::filesystem::remove_all(root);

        std::cout << "Runtime UI input ownership checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
