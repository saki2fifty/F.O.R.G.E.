#pragma once
#include "game_host_source.hpp"
#include <algorithm>
#include <cstring>
#include <forge/animation.hpp>
#include <forge/engine_assets.hpp>
#include <forge/render_components.hpp>
namespace forge::test {
struct GameHostFixture {
    std::filesystem::path output, project;
    unsigned stage = 0, frames = 0;
    bool pause_seen = false, packaged = false, mixed = false, prepare_only = false,
         expect_module = false;
    std::string waiting = "startup";
    std::uint64_t ticket = 0;
    Uint64 started = SDL_GetTicks();
    Json original;
    void storage_probe(GameStorage& storage, const Json& settings, const InputMap& input,
                       AssetId startup) {
        const GameSaveSchema schema{1,
                                    [startup](const GameSave& save) {
                                        check(save.scene == startup && save.data.at("marker") == 42,
                                              "Relocation changed saved fixture state");
                                    },
                                    {}};
        const auto slots = storage.slots();
        const bool reopened = std::find(slots.begin(), slots.end(), "relocation") != slots.end();
        const auto validate = [&](const Json& overrides) {
            (void)resolve_game_settings(settings, overrides, input);
        };
        if (reopened) {
            (void)storage.load("relocation", schema);
            check(storage.load_settings(validate).at("audio").at("master_volume") == .75,
                  "Relocation lost persistent user settings");
        } else {
            storage.save("relocation", {startup, {{"marker", 42}}}, schema);
            storage.save_settings({{"audio", {{"master_volume", .75}}}}, validate);
        }
        atomic_write(output / "storage-result.json", Json{{"root", path_utf8(storage.root())},
                                                          {"reopened", reopened},
                                                          {"scene", startup},
                                                          {"settings_persisted", true}}
                                                         .dump(2));
    }
    explicit GameHostFixture(int argc, char** argv) {
        mixed = argc == 3 && std::string_view(argv[1]) == "--packaged-mixed";
        packaged = mixed || (argc == 3 && std::string_view(argv[1]) == "--packaged");
        prepare_only = argc == 3 && std::string_view(argv[1]) == "--prepare";
        if (argc != 2 && !packaged && !prepare_only)
            throw std::runtime_error("Game fixture requires output directory");
        output = std::filesystem::absolute(argv[argc == 3 ? 2 : 1]);
        std::filesystem::create_directories(output);
        if (packaged)
            return;
        project = make_game_host_project(output);
    }
    static void check(bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    }
    static void click(SDL_Window* window, float x, float y) {
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.windowID = SDL_GetWindowID(window);
        e.motion.x = x;
        e.motion.y = y;
        check(SDL_PushEvent(&e), "Fixture mouse motion failed");
        e = {};
        e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        e.button.windowID = SDL_GetWindowID(window);
        e.button.button = SDL_BUTTON_LEFT;
        e.button.down = true;
        e.button.x = x;
        e.button.y = y;
        check(SDL_PushEvent(&e), "Fixture button down failed");
        e.type = SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.down = false;
        check(SDL_PushEvent(&e), "Fixture button up failed");
    }
    void capture(const char* name, Diligent::ITextureView* image, Diligent::IRenderDevice* device,
                 Diligent::IDeviceContext* context) {
        using namespace Diligent;
        auto desc = image->GetTexture()->GetDesc();
        desc.Usage = USAGE_STAGING;
        desc.BindFlags = BIND_NONE;
        desc.CPUAccessFlags = CPU_ACCESS_READ;
        RefCntAutoPtr<ITexture> staging;
        device->CreateTexture(desc, nullptr, &staging);
        check(bool(staging), "Standalone readback allocation failed");
        context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
        CopyTextureAttribs copy;
        copy.pSrcTexture = image->GetTexture();
        copy.pDstTexture = staging;
        copy.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        copy.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        context->CopyTexture(copy);
        context->WaitForIdle();
        MappedTextureSubresource data;
        context->MapTextureSubresource(staging, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr,
                                       data);
        check(data.pData != nullptr, "Standalone readback map failed");
        const auto* pixels = static_cast<const unsigned char*>(data.pData);
        const auto* center = pixels + (desc.Height / 2) * data.Stride + (desc.Width / 2) * 4;
        const auto* corner = pixels + (desc.Height - 8) * data.Stride + (desc.Width - 8) * 4;
        unsigned contrast = 0;
        for (unsigned c = 0; c < 3; ++c)
            contrast += unsigned(std::abs(int(center[c]) - int(corner[c])));
        std::size_t imported_pixels = 0;
        if (mixed && std::string_view(name) == "standalone-running.ppm")
            for (unsigned y = desc.Height * 4 / 10; y < desc.Height * 6 / 10; ++y)
                for (unsigned x = desc.Width * 65 / 100; x < desc.Width * 9 / 10; ++x) {
                    const auto* pixel = pixels + y * data.Stride + x * 4;
                    unsigned difference = 0;
                    for (unsigned c = 0; c < 3; ++c)
                        difference += unsigned(std::abs(int(pixel[c]) - int(corner[c])));
                    imported_pixels += difference > 40;
                }
        std::ofstream file(output / name, std::ios::binary);
        file << "P6\n" << desc.Width << ' ' << desc.Height << "\n255\n";
        for (unsigned y = 0; y < desc.Height; ++y) {
            const auto* row = static_cast<const unsigned char*>(data.pData) + y * data.Stride;
            for (unsigned x = 0; x < desc.Width; ++x)
                file.write(reinterpret_cast<const char*>(row + x * 4), 3);
        }
        context->UnmapTextureSubresource(staging, 0, 0);
        check(bool(file), "Standalone capture write failed");
        check(contrast > 30, "Standalone cube is not distinguishable from the background");
        if (mixed && std::string_view(name) == "standalone-running.ppm")
            check(imported_pixels > 100,
                  "Imported textured mesh is not visible to the right of the built-in cube");
    }
    void frame(GameSession& game, Diligent::ITextureView* image, Diligent::IRenderDevice* device,
               Diligent::IDeviceContext* context, SDL_Window* window, bool& running) {
        if (SDL_GetTicks() - started >= 90000) {
            atomic_write(output / "workflow-timeout.json", Json{{"stage", stage},
                                                                {"frames", frames},
                                                                {"waiting", waiting},
                                                                {"game", game.status()}}
                                                               .dump(2));
            throw std::runtime_error("Standalone workflow timed out at stage " +
                                     std::to_string(stage));
        }
        if (!image || ++frames < 4)
            return;
        if (stage == 0) {
            Json proof = {{"module_required", expect_module},
                          {"module_ticked", false},
                          {"mixed_content", mixed}};
            if (expect_module) {
                check(bool(game.active().engine.world().world().lookup("example.state")),
                      "Packaged gameplay module did not start");
                const auto diagnostics = game.active().engine.services().diagnostics();
                const bool ticked =
                    std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& d) {
                        return d.at("text").template get<std::string>().starts_with(
                            "Example tick=");
                    });
                if (!ticked) {
                    waiting = "native gameplay fixed tick";
                    return;
                }
                proof["module_ticked"] = true;
            }
            auto effective = game.active().scene.effective_document();
            if (mixed)
                for (const auto* name :
                     {"Animated actor", "Agent", "Speaker", "Imported renderable"})
                    check(std::count_if(
                              effective.at("entities").begin(), effective.at("entities").end(),
                              [&](const auto& entity) { return entity.at("name") == name; }) == 1,
                          "Mixed-content runtime lost an expected entity");
            for (const auto& entity : effective.at("entities")) {
                if (entity.at("name") == "Animated actor") {
                    const auto animation = animation_runtime(game.active().engine.world());
                    check(bool(animation), "Packaged animation runtime is unavailable");
                    const auto actor =
                        game.active().scene.entity(entity.at("id").get<std::string>());
                    // effective_document() describes scene data/transforms. The
                    // sampled pose belongs to AnimationRuntime, never authored ECS truth.
                    const auto pose = animation->presentation(actor.id(), 1);
                    if (pose.is_null() || pose.at("time").get<double>() <= 0) {
                        waiting = "animation sample after first fixed tick";
                        return;
                    }
                    proof["animation_time"] = pose.at("time");
                }
                if (entity.at("name") == "Agent") {
                    if (entity.at("world_affine")[3].get<double>() <= -8) {
                        waiting = "navigation agent movement";
                        return;
                    }
                    proof["navigation_x"] = entity.at("world_affine")[3];
                }
                if (entity.at("name") == "Speaker") {
                    const auto audio = std::static_pointer_cast<AudioRuntime>(
                        game.active().engine.services().audio());
                    check(audio && audio->status().at("voices").get<unsigned>() == 1,
                          "Packaged audio did not realize a voice");
                    proof["audio_voices"] = audio->status().at("voices");
                }
            }
            atomic_write(output / "runtime-content-result.json", proof.dump(2));
            waiting = "UI pause/resume and scene replacement";
            original = game.active().scene.snapshot();
            capture("standalone-running.ppm", image, device, context);
            click(window, 80, 40);
            stage = 1;
        } else if (stage == 1 && game.status().at("state") == "paused") {
            if (!pause_seen) {
                pause_seen = true;
                return; // Capture after the next UI snapshot reflects the click.
            }
            capture("standalone-paused.ppm", image, device, context);
            click(window, 80, 92);
            stage = 2;
        } else if (stage == 2 && game.status().at("state") == "running") {
            game.pause(RuntimeClock::Clock::now());
            original = game.active().scene.snapshot();
            auto broken = original;
            for (auto& e : broken.at("entities"))
                if (e.at("components").contains("forge.mesh_renderer"))
                    e["components"]["forge.mesh_renderer"]["mesh"] = AssetId::generate();
            ticket = game.prepare(broken);
            stage = 3;
        } else if (stage == 3) {
            bool failed = false;
            try {
                if (game.poll_preparation(ticket).ready)
                    throw std::logic_error("Invalid mesh was reported ready");
            } catch (const std::logic_error&) {
                throw;
            } catch (const std::runtime_error&) {
                failed = true;
            }
            if (failed) {
                check(game.active().scene.snapshot() == original,
                      "Failed graphical candidate changed the active scene");
                stage = 30; // Let the visible loading binding show the failed outcome.
            }
        } else if (stage == 30) {
            capture("standalone-failed-load-retained.ppm", image, device, context);
            ticket = game.prepare(original);
            stage = 31;
        } else if (stage == 31) {
            capture("standalone-loading.ppm", image, device, context);
            click(window, 80, 252);
            stage = 32;
        } else if (stage == 32) {
            if (game.loading_state().state != "cancelled")
                return;
            check(game.active().scene.snapshot() == original,
                  "Loading cancel changed active scene");
            capture("standalone-cancelled.ppm", image, device, context);
            ticket = game.prepare(original);
            stage = 4;
        } else if (stage == 4 && game.poll_preparation(ticket).ready) {
            game.activate(ticket, RuntimeClock::Clock::now(), false);
            stage = 5;
        } else if (stage == 5) {
            check(game.status().at("state") == "paused", "Replacement did not stay paused");
            capture("standalone-replaced.ppm", image, device, context);
            check(SDL_SetWindowSize(window, 800, 600), "Standalone resize failed");
            stage = 6;
        } else if (stage == 6) {
            int client_width = 0, client_height = 0;
            check(SDL_GetWindowSize(window, &client_width, &client_height),
                  "Standalone client size unavailable");
            if (client_width != 800 || client_height != 600)
                return;
            int width = 0, height = 0;
            check(SDL_GetWindowSizeInPixels(window, &width, &height),
                  "Standalone output size unavailable");
            const auto& desc = image->GetTexture()->GetDesc();
            if (desc.Width != unsigned(width) || desc.Height != unsigned(height))
                return;
            check(game.active().scene.snapshot() == original,
                  "Window resize changed authored scene state");
            capture("standalone-resized.ppm", image, device, context);
            atomic_write(output / "standalone-result.json",
                         Json{{"startup", true},
                              {"ui_pause_resume", true},
                              {"failed_gpu_candidate_retained", true},
                              {"prepared_replacement", true},
                              {"loading_ui_cancel", true},
                              {"resized", {{"width", width}, {"height", height}}},
                              {"final", game.status()}}
                             .dump(2));
            running = false;
        }
    }
};
} // namespace forge::test
