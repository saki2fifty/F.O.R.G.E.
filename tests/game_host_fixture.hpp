#pragma once
#include <cstring>
#include <forge/engine_assets.hpp>
#include <forge/render_components.hpp>
namespace forge::test {
struct GameHostFixture {
    std::filesystem::path output, project;
    unsigned stage = 0, frames = 0;
    bool pause_seen = false;
    std::uint64_t ticket = 0;
    Uint64 started = SDL_GetTicks();
    Json original;
    explicit GameHostFixture(int argc, char** argv) {
        if (argc != 2)
            throw std::runtime_error("Game fixture requires output directory");
        output = std::filesystem::absolute(argv[1]);
        project = output / ("project-" + AssetId::generate().str());
        std::filesystem::create_directories(project);
        WorldContext world;
        Scene scene(world);
        scene.replace(
            {{"version", 1},
             {"entities",
              Json::array({{{"id", "camera"}, {"name", "Camera"}, {"components", Json::object()}},
                           {{"id", "cube"},
                            {"name", "Cube"},
                            {"components", {{"forge.position", {{"x", 0}, {"y", 0}, {"z", 3}}}}}},
                           {{"id", "light"}, {"name", "Light"}, {"components", Json::object()}},
                           {{"id", "hud"}, {"name", "HUD"}, {"components", Json::object()}}})}});
        Camera camera;
        camera.background_r = .025f;
        camera.background_g = .035f;
        camera.background_b = .055f;
        scene.entity("camera").set(camera);
        scene.entity("camera").set<LocalTranslation>({});
        scene.entity("camera").set<Primitive>({no_primitive});
        scene.entity("light").set<LocalTranslation>({0, 3, 0});
        scene.entity("light").set<Primitive>({no_primitive});
        scene.entity("light").set<Light>({});
        scene.entity("cube").set<MeshRenderer>(
            {engine_primitive(0), {{"surface", engine_material()}}});
        const std::string markup = R"rml(<rml><head><style>
body { width:100%; height:100%; font-family:Lato; font-size:20px; color:#eeeeee; pointer-events:none; }
button { position:absolute; left:24px; width:160px; height:40px; line-height:40px; text-align:center; background-color:#305070; pointer-events:auto; }
#pause { top:20px; } #resume { top:72px; } h1 { position:absolute; left:24px; right:24px; top:130px; font-size:24px; }
#status { position:absolute; left:24px; right:24px; top:190px; }
</style></head><body><button id="pause" data-event-click="command('Pause')">Pause</button>
<button id="resume" data-event-click="command('Resume')">Resume</button>
<h1>FORGE standalone</h1><div id="status">Paused: {{paused}} | Tick: {{tick}}</div></body></rml>)rml";
        atomic_write(project / "hud.rml", markup);
        const auto ui = register_ui_document(project, "hud.rml");
        scene.entity("hud").set<UiDocument>({{ui.id}});
        scene.save(project / "main.scene.json");
        auto catalog = AssetCatalog::open_project(project);
        const auto asset = catalog.add_scene("main.scene.json");
        catalog.save(AssetCatalog::project_index(project));
        auto settings = ProjectSettings::defaults("Standalone acceptance");
        settings["startup_scene"] = {{"asset", asset.id}, {"source", "main.scene.json"}};
        settings["game"] = default_game_settings("org.forge.fixture-" + AssetId::generate().str(),
                                                 "FORGE standalone acceptance");
        settings["game"]["display"]["width"] = 960;
        settings["game"]["display"]["height"] = 540;
        atomic_write(project / "forge.project.json", settings.dump(2));
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
    }
    void frame(GameSession& game, Diligent::ITextureView* image, Diligent::IRenderDevice* device,
               Diligent::IDeviceContext* context, SDL_Window* window, bool& running) {
        check(SDL_GetTicks() - started < 90000, "Standalone workflow timed out");
        if (!image || ++frames < 4)
            return;
        if (stage == 0) {
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
                capture("standalone-failed-load-retained.ppm", image, device, context);
                ticket = game.prepare(original);
                stage = 4;
            }
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
                              {"resized", {{"width", width}, {"height", height}}},
                              {"final", game.status()}}
                             .dump(2));
            running = false;
        }
    }
};
} // namespace forge::test
