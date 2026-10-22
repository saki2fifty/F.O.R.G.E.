#pragma once
#include "frame_renderer.hpp"
#include "runtime_tile_map.hpp"
#include "sprite_draw_render_tests.hpp"
#include <forge/animation.hpp>
#include <forge/game_content.hpp>
#include <forge/runtime.hpp>
#include <forge/runtime_world.hpp>
#include <fstream>
#include <thread>

namespace forge::test_physics_2d_gallery_render {
template <class Readback>
void painted(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
             const Readback& readback, const std::filesystem::path& root,
             const std::filesystem::path& images) {
    using test_sprite_draw::check;
    std::filesystem::create_directories(images);
    const auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    AssetId scene_id;
    for (const auto& [id, record] : catalog->records())
        if (!record.subasset && record.source == "Scenes/77-painted-2d-collision.scene.json")
            scene_id = id;
    check(bool(scene_id), "Painted Gallery Scene missing");
    RuntimeWorld world({}, {}, std::nullopt, root, false);
    world.scene.restore_snapshot(load_game_scene(root, {scene_id}));
    const auto ready = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!world.physics()->prepare_assets() || !world.prepare_scene_resources()) {
        check(std::chrono::steady_clock::now() < ready,
              "Native painted Gallery preparation stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::vector<EntityRef> actors;
    const auto initial_scene = world.scene.document();
    for (const auto& row : initial_scene.at("entities"))
        if (row.at("components").contains("forge.physics_body_2d") &&
            row.at("components").at("forge.physics_body_2d").at("motion") == 2)
            actors.push_back(world.scene.reference(row.at("id").get<std::string>()));
    check(actors.size() == 4, "Native painted Gallery lost Dynamic actors");
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root);
    host->catalog(catalog);
    FrameRenderer game(presentation);
    game.resources(host);
    unsigned current_tick = 0;
    for (unsigned target_tick : {0u, 60u, 180u}) {
        while (current_tick < target_tick) {
            world.simulation.tick(1.f / 60);
            ++current_tick;
        }
        for (const auto [width, height] : {std::pair{640u, 360u}, std::pair{1280u, 720u}}) {
            test_sprite_draw::Pixels pixels;
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            do {
                auto frame = extract_render_scene(world.simulation.presentation(1));
                detail::bind_runtime_tile_maps(world.engine.world(), frame);
                presentation.begin_frame();
                auto* output = game.game(context, std::move(frame), width, height);
                presentation.end_frame(context);
                host->submit();
                pixels = readback(output->GetTexture());
                context->FinishFrame();
                presentation.device()->ReleaseStaleResources();
                check(std::chrono::steady_clock::now() < end, "Native painted Game did not settle");
                if (game.pending())
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while (game.pending());
            check(game.diagnostics().empty() && game.game_scene() &&
                      game.game_scene()->sprites.size() == 4 &&
                      game.game_scene()->tile_maps.size() == 1 &&
                      game.game_scene()->tile_maps[0].cells &&
                      game.game_scene()->tile_maps[0].cells->occupied() == 23,
                  "Painted Game did not render ordinary sprites and its runtime Map instance");
            for (const auto actor : actors) {
                const auto body = world.physics()->body_state_2d(actor);
                check(bool(body), "Native painted Game body unavailable");
                const auto x = int(std::floor(width * .5 + body->position[0] * height / 13.5));
                const auto y = int(std::floor(height * .5 - body->position[1] * height / 13.5));
                check(x >= 0 && y >= 0 && x < int(width) && y < int(height),
                      "Painted Game actor left its intended viewport");
                const auto color = pixels.at(std::size_t(y) * width + unsigned(x));
                check(std::max({color[0], color[1], color[2]}) > 120 &&
                          *std::max_element(color.begin(), color.begin() + 3) -
                                  *std::min_element(color.begin(), color.begin() + 3) >
                              40,
                      "Mapped painted Game pixels lost a simulated colored actor");
            }
            std::ofstream file(images / ("gallery77-tick" + std::to_string(target_tick) + "-" +
                                         std::to_string(width) + ".ppm"),
                               std::ios::binary);
            file << "P6\n" << width << ' ' << height << "\n255\n";
            for (const auto pixel : pixels)
                file.write(reinterpret_cast<const char*>(pixel.data()), 3);
            check(bool(file), "Painted Game capture could not be saved");
        }
    }
}
template <class Readback>
void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
              const Readback& readback, const std::filesystem::path& root,
              const std::filesystem::path& images, bool connected = false) {
    using test_sprite_draw::check;
    std::filesystem::create_directories(images);
    const auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    AssetId scene_id;
    for (const auto& [id, record] : catalog->records())
        if (!record.subasset &&
            record.source == (connected ? "Scenes/76-connected-2d-bodies.scene.json"
                                        : "Scenes/75-2d-physics.scene.json"))
            scene_id = id;
    check(bool(scene_id), "2D Gallery Scene missing");
    EngineContext engine(WorldRole::Runtime, false, {physics_module(), animation_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(load_game_scene(root, {scene_id}));
    const auto animation = animation_runtime(engine.world());
    animation->catalog(catalog);
    const auto initial = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!animation->prepare_initial_pose()) {
        check(std::chrono::steady_clock::now() < initial, "Native 2D Gallery clip timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto physics = std::static_pointer_cast<PhysicsRuntime>(engine.services().physics());
    physics->synchronize(0);
    RuntimeSimulation simulation(engine.world(), scene);
    simulation.reset_presentation();
    EntityRef amber, platform;
    const auto authored = scene.effective_document();
    for (const auto& row : authored.at("entities")) {
        if (row.at("name") == (connected ? "Cyan pendulum" : "Dynamic amber box"))
            amber = {scene_id, row.at("id").get<EntityId>()};
        if (connected ? row.at("name") == "Green motor carriage"
                      : row.at("components").contains("forge.transform_animator"))
            platform = {scene_id, row.at("id").get<EntityId>()};
    }
    check(bool(amber.entity) && bool(platform.entity), "Native 2D Gallery participants missing");
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root);
    host->catalog(catalog);
    FrameRenderer game(presentation);
    game.resources(host);
    unsigned current_tick = 0;
    for (unsigned target_tick : {0u, 60u, 180u}) {
        while (current_tick < target_tick) {
            simulation.tick(1.f / 60);
            ++current_tick;
        }
        for (const auto [width, height] : {std::pair{640u, 360u}, std::pair{1280u, 720u}}) {
            test_sprite_draw::Pixels pixels;
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            do {
                presentation.begin_frame();
                auto* output = game.game(context, simulation.presentation(1), width, height);
                presentation.end_frame(context);
                host->submit();
                pixels = readback(output->GetTexture());
                context->FinishFrame();
                presentation.device()->ReleaseStaleResources();
                check(std::chrono::steady_clock::now() < end, "Native 2D Gallery did not settle");
                if (game.pending())
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while (game.pending());
            check(game.diagnostics().empty() && game.game_scene() &&
                      game.game_scene()->sprites.size() == (connected ? 14u : 12u) &&
                      game.game_scene()->meshes.empty(),
                  "Actual 2D Gallery Game renderer lost its Sprite material/profile");
            const auto sample = [&](Double2 point) {
                const double extent = connected ? 11 : 12;
                const auto x = int(std::floor(width * .5 + point[0] * height / extent));
                const auto y = int(std::floor(height * .5 - point[1] * height / extent));
                check(x >= 0 && y >= 0 && x < int(width) && y < int(height),
                      "2D Gallery body sample left the viewport");
                return pixels[std::size_t(y) * width + unsigned(x)];
            };
            const auto body = physics->body_state_2d(amber);
            const auto moving = physics->body_state_2d(platform);
            check(body && moving && (target_tick == 0 || body->position[1] < 3),
                  "Native 2D Gallery body did not fall before its later capture");
            const auto gold = sample(body->position), blue = sample(moving->position);
            check(connected ? gold[2] > gold[1] && gold[1] > gold[0] && blue[1] > blue[2] &&
                                  blue[2] > blue[0]
                            : gold[0] > gold[1] && gold[1] > gold[2] && blue[2] > blue[1] &&
                                  blue[1] > blue[0],
                  "Actual Game pixels lost the simulated amber body or blue platform");
            std::ofstream file(
                images / (std::string(connected ? "gallery76-tick" : "gallery75-tick") +
                          std::to_string(target_tick) + "-" + std::to_string(width) + ".ppm"),
                std::ios::binary);
            file << "P6\n" << width << ' ' << height << "\n255\n";
            for (const auto pixel : pixels)
                file.write(reinterpret_cast<const char*>(pixel.data()), 3);
            check(bool(file), "Native 2D Gallery capture write failed");
        }
    }
}
} // namespace forge::test_physics_2d_gallery_render
