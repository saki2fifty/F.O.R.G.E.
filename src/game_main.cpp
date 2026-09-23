#include "RmlUi_Platform_SDL.h"
#include "asset_bytes.hpp"
#include "game_device.hpp"
#include "game_presentation.hpp"
#include "sdl_input.hpp"
#include "standalone_manifest.hpp"
#include <forge/build.hpp>
#include <forge/game_content.hpp>
#include <forge/game_platform.hpp>
#include <forge/game_settings.hpp>
#include <forge/game_storage.hpp>
#include <forge/native_sdk.hpp>
#include <forge/native_sdk_identity.h>
#include <forge/project.hpp>
#include <fstream>
#include <iostream>
#ifdef FORGE_GAME_FIXTURE
#include "game_host_fixture.hpp"
#endif
namespace {
using namespace forge;
using namespace Diligent;
void checked(bool ok) {
    if (!ok)
        throw std::runtime_error(SDL_GetError());
}
struct Sdl {
    Sdl() { checked(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)); }
    ~Sdl() { SDL_Quit(); }
};
void display(SDL_Window* window, const Json& settings) {
    int count = 0;
    auto* ids = SDL_GetDisplays(&count);
    if (!ids || count == 0) {
        SDL_free(ids);
        throw std::runtime_error("No display is available");
    }
    auto ordinal = settings.at("display").get<unsigned>();
    if (ordinal >= unsigned(count)) {
        std::clog << "Saved display is unavailable; using the primary display\n";
        ordinal = 0;
    }
    const auto id = ids[ordinal];
    SDL_free(ids);
    checked(SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED_DISPLAY(id),
                                  SDL_WINDOWPOS_CENTERED_DISPLAY(id)));
    const auto mode = settings.at("mode").get<std::string>();
    if (mode == "fullscreen") {
        SDL_DisplayMode selected{};
        checked(SDL_GetClosestFullscreenDisplayMode(id, settings.at("width").get<int>(),
                                                    settings.at("height").get<int>(), 0, true,
                                                    &selected));
        checked(SDL_SetWindowFullscreenMode(window, &selected));
    } else
        checked(SDL_SetWindowFullscreenMode(window, nullptr));
    checked(SDL_SetWindowFullscreen(window, mode != "windowed"));
}
} // namespace
int main(int argc, char** argv) {
    using namespace forge;
    using namespace Diligent;
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        std::cout << "FORGE game | Build: " << build_id << '\n';
        return 0;
    }
    bool software = false, verify_startup = false;
    unsigned verification_frames = 0;
    std::ofstream log;
    std::streambuf* previous_log = nullptr;
    int result = 0;
    try {
        Sdl sdl;
        const auto* executable_path = SDL_GetBasePath();
        if (!executable_path)
            throw std::runtime_error(SDL_GetError());
        const auto executable_root = std::filesystem::u8path(executable_path);
        auto root = executable_root;
        std::optional<StandaloneDistribution> distribution;
#ifdef FORGE_GAME_FIXTURE
        test::GameHostFixture fixture(argc, argv);
        if (fixture.prepare_only) {
            std::cout << path_utf8(fixture.project) << std::endl;
            return 0;
        }
        if (!fixture.packaged)
            root = fixture.project;
        software = true;
        const bool packaged_start = fixture.packaged;
#else
        const bool development_project = argc == 3 && std::string_view(argv[1]) == "--project";
        verify_startup = argc == 2 && std::string_view(argv[1]) == "--verify-startup";
        const bool packaged_start = argc == 1 || verify_startup;
        software = verify_startup; // Explicit diagnostic mode uses WARP/offline audio.
        if (development_project)
            root = std::filesystem::absolute(std::filesystem::u8path(argv[2]));
        else if (!packaged_start)
            throw std::runtime_error(
                "Usage: forge_game [--project <development directory> | --verify-startup]");
#endif
        if (packaged_start) {
            distribution = open_standalone_distribution(executable_root, {"windows", "d3d12"},
                                                        FORGE_NATIVE_SDK_PROFILE,
                                                        FORGE_NATIVE_SDK_FINGERPRINT);
            if (distribution->manifest.at("engine").at("source_commit") != source_commit ||
                distribution->manifest.at("engine").at("build_id") != build_id)
                throw std::runtime_error(
                    "game.distribution: Executable and distribution build identity differ");
            root = distribution->content;
        }
        const ProjectSettings project =
            distribution ? ProjectSettings(root, distribution->settings) : ProjectSettings(root);
#ifdef FORGE_GAME_FIXTURE
        fixture.expect_module = fixture.packaged && project.requires_native_sdk();
#endif
        if (!project.document().contains("game"))
            throw std::runtime_error("Project needs game settings with a durable application_id");
        const auto& defaults = project.document().at("game");
        GameStorage storage(game_user_data_base(), defaults.at("application_id"));
        // Session-local log. Saves and preferences use separate files.
        const auto log_path = ProjectPaths(storage.root()).resolve("runtime.log");
        if (std::filesystem::is_symlink(log_path))
            throw std::runtime_error("Runtime log cannot be a redirected file");
        log.open(log_path, std::ios::trunc);
        if (!log)
            throw std::runtime_error("Cannot open runtime log");
        previous_log = std::clog.rdbuf(log.rdbuf());
#ifdef FORGE_GAME_FIXTURE
        fixture.storage_probe(storage, defaults, project.input(),
                              project.document().at("startup_scene").at("asset").get<AssetId>());
#endif
        const auto user = storage.load_settings([&](const Json& overrides) {
            (void)resolve_game_settings(defaults, overrides, project.input());
        });
        const auto settings = resolve_game_settings(defaults, user, project.input());
        std::clog << "FORGE game | Build: " << build_id << "\nStarting "
                  << settings.at("title").get<std::string>() << '\n';
        const auto& video = settings.at("display");
        const auto title = settings.at("title").get<std::string>();
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow(title.c_str(), video.at("width").get<int>(),
                             video.at("height").get<int>(),
                             SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY),
            SDL_DestroyWindow);
        if (!window)
            throw std::runtime_error(SDL_GetError());
        display(window.get(), video);
        GameDevice graphics(window.get(), software);
        DiligentPresentation device(graphics.device);
        TextInputMethodEditor_SDL ime;
        UiPlatformCallbacks callbacks;
        callbacks.get_clipboard = [] {
            char* text = SDL_GetClipboardText();
            std::string copy = text ? text : "";
            SDL_free(text);
            return copy;
        };
        callbacks.set_clipboard = [](const std::string& text) {
            (void)SDL_SetClipboardText(text.c_str());
        };
        callbacks.activate_text = [&](float x, float y, float h) {
            int lw = 1, lh = 1, pw = 1, ph = 1;
            SDL_GetWindowSize(window.get(), &lw, &lh);
            SDL_GetWindowSizeInPixels(window.get(), &pw, &ph);
            SDL_Rect caret{int(x * lw / std::max(pw, 1)), int(y * lh / std::max(ph, 1)), 1,
                           std::max(1, int(h * lh / std::max(ph, 1)))};
            (void)SDL_SetTextInputArea(window.get(), &caret, 0);
            (void)SDL_StartTextInput(window.get());
        };
        callbacks.deactivate_text = [&] { (void)SDL_StopTextInput(window.get()); };
        GamePresentation presentation(
            device, graphics.context, root,
            asset_detail::read_bytes(executable_root / "resources/ui/LatoLatin-Regular.ttf",
                                     4 * 1024 * 1024),
            std::move(callbacks), &ime);
        EngineServices bootstrap;
        GameSessionConfig config;
        config.content_root = root;
        config.ui = true;
        config.clock.simulation_hz = project.simulation_hz();
        config.physics = project.physics();
        config.input = InputMap(settings.at("input_map"));
        config.audio = AudioConfig{root, software ? AudioOutput::Offline : AudioOutput::Device,
                                   false, settings.at("audio").at("master_volume").get<float>()};
        config.modules = project_native_modules(distribution ? distribution->root : root,
                                                project.document(), bootstrap.access());
        config.preparation = [&](std::uint64_t ticket) { return presentation.prepare(ticket); };
        GameSession game(std::move(config)); // Dies before presentation/platform/module services.
        const auto startup = project.document().at("startup_scene").at("asset").get<AssetId>();
        auto ticket = game.prepare(load_game_scene(root, {startup}));
        const auto deadline = RuntimeClock::Clock::now() + std::chrono::seconds(60);
        ui_protocol::CommandGate commands;
        bool running = true, active = false, focused = true;
        std::uint64_t command_generation = 0;
        std::string loading_stage, last_ui_error;
        std::uint64_t log_generation = 0, diagnostic_cursor = 0;
        auto& ui = presentation.ui();
        while (running) {
            const auto now = RuntimeClock::Clock::now();
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT ||
                    event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                    running = false;
                    continue;
                }
                if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                    focused = false;
                    ui.release_input();
                    if (active)
                        game.input({{{}, 0, true}});
                } else if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
                    focused = true;
                if (!active || !focused)
                    continue;
                const int modifiers = RmlSDL::GetKeyModifierState();
                if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
                    if (ui.key(RmlSDL::ConvertKey(event.key.key), event.key.down, modifiers))
                        game.input({{{}, 0, true}});
                    else if (!event.key.repeat) {
                        const auto key = sdl_key_control(event.key.scancode);
                        if (!key.empty())
                            game.input({{key, event.key.down ? 1.0 : 0.0}});
                    }
                } else if (event.type == SDL_EVENT_TEXT_INPUT)
                    ui.text(event.text.text);
                else if (event.type == SDL_EVENT_TEXT_EDITING)
                    ime.HandleEdit(event.edit);
                else if (event.type == SDL_EVENT_MOUSE_MOTION) {
                    int lw = 1, lh = 1, pw = 1, ph = 1;
                    SDL_GetWindowSize(window.get(), &lw, &lh);
                    SDL_GetWindowSizeInPixels(window.get(), &pw, &ph);
                    if (!ui.mouse_move(int(event.motion.x * pw / std::max(lw, 1)),
                                       int(event.motion.y * ph / std::max(lh, 1)), modifiers)) {
                        const auto sensitivity =
                            settings.at("input").at("mouse_sensitivity").get<double>();
                        game.input({{"mouse.delta_x", event.motion.xrel * sensitivity},
                                    {"mouse.delta_y", event.motion.yrel * sensitivity}});
                    }
                } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                           event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                    if (ui.mouse_button(RmlSDL::ConvertMouseButton(event.button.button),
                                        event.button.down, modifiers))
                        game.input({{{}, 0, true}});
                    else {
                        const char* control =
                            event.button.button == SDL_BUTTON_LEFT     ? "mouse.left"
                            : event.button.button == SDL_BUTTON_RIGHT  ? "mouse.right"
                            : event.button.button == SDL_BUTTON_MIDDLE ? "mouse.middle"
                                                                       : nullptr;
                        if (control)
                            game.input({{control, event.button.down ? 1.0 : 0.0}});
                    }
                }
            }
            if (!running)
                break;
            int width = 0, height = 0;
            checked(SDL_GetWindowSizeInPixels(window.get(), &width, &height));
            if (width <= 0 || height <= 0 ||
                (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_MINIMIZED)) {
                SDL_Delay(10);
                continue;
            }
            if (graphics.swap->GetDesc().Width != unsigned(width) ||
                graphics.swap->GetDesc().Height != unsigned(height))
                graphics.swap->Resize(unsigned(width), unsigned(height));
            presentation.dimensions(unsigned(width), unsigned(height),
                                    std::clamp(SDL_GetWindowDisplayScale(window.get()), .5f, 4.f));
            if (const auto cancel = ui.take_loading_cancel();
                cancel && *cancel == game.status().at("prepared_ticket").get<std::uint64_t>()) {
                game.cancel(*cancel);
                std::clog << "Scene preparation cancelled: " << *cancel << '\n';
                if (!active) {
                    running = false;
                    continue;
                }
            }
            if (!active)
                ui.loading(game.loading_state());
            auto* image =
                active ? presentation.draw(game, double(SDL_GetTicksNS()) / 1e9) : nullptr;
            if (!active) {
                ui.update(double(SDL_GetTicksNS()) / 1e9, width, height,
                          std::clamp(SDL_GetWindowDisplayScale(window.get()), .5f, 4.f));
                const auto progress = game.poll_preparation(ticket);
                if (progress.stage != loading_stage) {
                    loading_stage = progress.stage;
                    const auto label = title + " | Loading: " + loading_stage;
                    (void)SDL_SetWindowTitle(window.get(), label.c_str());
                    std::clog << "Preparing: " << loading_stage << '\n';
                }
                if (progress.ready) {
                    game.activate(ticket, now, true);
                    commands.reset(presentation.session(), presentation.generation());
                    command_generation = presentation.generation();
                    active = true;
                    (void)SDL_SetWindowTitle(window.get(), title.c_str());
                    std::clog << "Scene ready: " << startup.str() << '\n';
                } else if (now > deadline)
                    throw std::runtime_error("Scene preparation timed out before activation");
            } else {
                if (command_generation != presentation.generation()) {
                    commands.reset(presentation.session(), presentation.generation());
                    command_generation = presentation.generation();
                }
                if (auto command = ui.pending_command()) {
                    auto ack = commands.dispatch(*command, [&](const Json& request) {
                        auto service = std::static_pointer_cast<UiRuntime>(
                            game.active().engine.services().ui());
                        service->command(
                            game.active().scene, request, [&](const std::string& action) {
                                if (action == "Pause")
                                    game.pause(now);
                                else if (action == "Resume")
                                    game.resume(now);
                                else if (action == "Step")
                                    game.step();
                                else
                                    throw std::runtime_error("Unknown runtime control");
                            });
                    });
                    ui.acknowledge(ack);
                }
                game.advance(now);
            }
            if (active) {
                if (log_generation != presentation.generation()) {
                    log_generation = presentation.generation();
                    diagnostic_cursor = 0;
                }
                const auto records = game.active().engine.services().diagnostics();
                for (const auto& record : records) {
                    const auto sequence = record.at("sequence").get<std::uint64_t>();
                    if (sequence <= diagnostic_cursor)
                        continue;
                    if (sequence > diagnostic_cursor + 1)
                        std::clog << Json{{"category", "game.log.gap"},
                                          {"missed", sequence - diagnostic_cursor - 1}}
                                         .dump()
                                  << '\n';
                    std::clog << record.dump() << '\n';
                    diagnostic_cursor = sequence;
                }
            }
            if (ui.diagnostic() != last_ui_error) {
                last_ui_error = ui.diagnostic();
                if (!last_ui_error.empty())
                    std::clog << Json{{"severity", "error"},
                                      {"category", "game.ui"},
                                      {"text", last_ui_error}}
                                     .dump()
                              << '\n';
            }
            auto* target = graphics.swap->GetCurrentBackBufferRTV();
            if (image) {
                graphics.context->SetRenderTargets(0, nullptr, nullptr,
                                                   RESOURCE_STATE_TRANSITION_MODE_NONE);
                CopyTextureAttribs copy;
                copy.pSrcTexture = image->GetTexture();
                copy.pDstTexture = target->GetTexture();
                copy.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
                copy.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
                graphics.context->CopyTexture(copy);
            } else {
                const float background[]{.02f, .025f, .03f, 1};
                graphics.context->ClearRenderTarget(target, background,
                                                    RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            }
#ifdef FORGE_GAME_FIXTURE
            fixture.frame(game, image, graphics.device, graphics.context, window.get(), running);
#endif
            graphics.swap->Present(video.at("vsync").get<bool>() ? 1 : 0);
            graphics.context->FinishFrame();
            if (verify_startup && active && ++verification_frames >= 4) {
                std::cout << "FORGE standalone startup verified" << std::endl;
                running = false;
            }
            log.flush();
        }
        ui.release_input();
        game.unload(RuntimeClock::Clock::now());
        graphics.context->WaitForIdle();
        std::clog << "Shutdown complete\n";
    } catch (const std::exception& e) {
        std::clog << "Fatal: " << e.what() << '\n';
        std::cerr << "FORGE game could not continue: " << e.what() << '\n';
        result = 1;
    }
    if (previous_log)
        std::clog.rdbuf(previous_log);
    return result;
}
