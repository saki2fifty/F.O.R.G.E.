#include "runtime_ui_input.hpp"
#include <RmlUi/Core.h>
#include <fstream>
#include <iostream>
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
        check(SDL_Init(0), SDL_GetError());
        auto root = std::filesystem::path(argv[3]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        std::ofstream(root / "hud.rml")
            << R"(<rml><head><style>body {font-family:Lato;font-size:18px;pointer-events:none;} input {width:120px;height:40px;pointer-events:auto;} button {width:120px;height:40px;pointer-events:auto;}</style></head><body><input id="field" type="text"/><button id="button">Button</button></body></rml>)";
        auto asset = register_ui_document(root, "hud.rml");
        std::ifstream f(argv[2], std::ios::binary);
        std::string bytes{std::istreambuf_iterator<char>(f), {}};
        std::vector<std::byte> font(bytes.size());
        std::memcpy(font.data(), bytes.data(), bytes.size());
        Renderer renderer;
        UiPresenter presenter(renderer, root, std::move(font));
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
            60, InputMap(
                    {{"version", 1},
                     {"actions",
                      Json::array({{{"id", action},
                                    {"name", "UI leak"},
                                    {"kind", "digital"},
                                    {"bindings", Json::array({{{"control", "key.w"}},
                                                              {{"control", "mouse.left"}}})}}})}}));
        play.start(argv[1], {{"version", 1}, {"entities", Json::array()}}, {}, true);
        auto wait = [&](auto done) {
            auto end = SDL_GetTicks() + 5000;
            while (!done() && play.active() && SDL_GetTicks() < end) {
                play.pump();
                SDL_Delay(1);
            }
            check(play.active() && done(), play.status().c_str());
        };
        wait([&] { return play.control_ready(); });
        GameInput game;
        game.pump(play, true);
        game.capture(play);
        RuntimeUiInput input;
        input.bounds({0, 0}, {640, 480}, 640, 480);
        TextInputMethodEditor_SDL ime;
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
        input.reset(play);
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
        auto generation = play.ui_snapshot().at("generation");
        check(play.submit_ui(request(2)), "Queue command before reload");
#ifdef _WIN32
        const auto sample = std::filesystem::path(argv[1]).parent_path() / "forge_sample.dll";
#else
        const auto sample = std::filesystem::path(argv[1]).parent_path() / "forge_sample.so";
#endif
        play.reload(sample.string());
        wait([&] { return play.pending_activation(); });
        check(play.ui_snapshot().at("generation") != generation,
              "Native reload changes UI generation");
        const auto deadline = SDL_GetTicks() + 30;
        while (SDL_GetTicks() < deadline) {
            play.pump();
            SDL_Delay(1);
        }
        check(play.active() && play.ui_ack().is_null(),
              "Old queued UI request discarded at native replacement");
        play.step();
        wait([&] { return play.reload_result() == PlaySession::Reload::Succeeded; });
        check(play.submit_ui(request(1)), "Fresh-generation UI sequence restarts");
        wait([&] { return !play.ui_ack().is_null(); });
        check(play.ui_ack().at("ok"), "Fresh UI after native reload");
        play.stop();
        std::filesystem::remove_all(root);
        SDL_Quit();
        std::cout << "Runtime UI input ownership checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
