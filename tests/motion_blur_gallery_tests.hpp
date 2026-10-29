#pragma once
#include "motion_blur_scene_tests.hpp"
#include <forge/animation.hpp>
#include <forge/game_content.hpp>
#include <forge/runtime.hpp>

namespace forge::test_motion_scene {
inline void gallery(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                    const std::filesystem::path& project, const std::filesystem::path& images) {
    using test_sprite_draw::check;
    const auto id = AssetId::parse("38a4d328-ee99-4b5e-88b0-907902377829");
    auto host = std::make_shared<MeshResourceHost>(presentation, context, project, true);
    host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(project)));
    EngineContext engine(WorldRole::Runtime, false, {animation_module(project)});
    Scene scene(engine.world());
    scene.restore_snapshot(load_game_scene(project, {id}));
    const auto animation = animation_runtime(engine.world());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!animation->prepare_initial_pose()) {
        check(std::chrono::steady_clock::now() < deadline, "Motion Gallery animation stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    RuntimeSimulation simulation(engine.world(), scene);
    simulation.reset_presentation();
    std::filesystem::create_directories(images);
    for (const auto& [width, height] : {std::pair{640u, 360u}, std::pair{360u, 640u}}) {
        FrameRenderer blurred(presentation), sharp(presentation);
        blurred.resources(host);
        sharp.resources(host);
        test_sprite_draw::Pixels a, b;
        double time = 1;
        for (unsigned frame = 0; frame < 12; ++frame) {
            const auto source = extract_render_scene(simulation.presentation(1));
            auto reference = source;
            reference.settings.motion_blur.enabled = false;
            unsigned warmup = 0;
            do {
                presentation.begin_frame();
                auto* actual = blurred.game(context, source, width, height, time);
                auto* current = sharp.game(context, reference, width, height, time);
                presentation.end_frame(context);
                host->submit();
                a = test_sprite_draw::read_pixels(presentation.device(), context,
                                                  actual->GetTexture());
                b = test_sprite_draw::read_pixels(presentation.device(), context,
                                                  current->GetTexture());
                context->FinishFrame();
                presentation.device()->ReleaseStaleResources();
                time += 1. / 60;
                check(++warmup < 128, "Motion Gallery resources did not settle");
            } while (blurred.pending() || sharp.pending());
            check(blurred.diagnostics().empty() && sharp.diagnostics().empty(),
                  "Motion Gallery presentation reported errors");
            if (frame < 11)
                simulation.tick(1.f / 60);
        }
        std::size_t different = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            different += a[i] != b[i];
            check(a[i][3] == b[i][3], "Motion Gallery changed current alpha");
            if (b[i] == b.front())
                check(a[i] == b[i], "Motion Gallery spread foreground into clear background");
        }
        check(different > 100, "Motion Gallery has no visible actual movement exposure");
        for (bool enabled : {true, false}) {
            const auto path =
                images / (std::string("motion-gallery-") + (enabled ? "enabled-" : "disabled-") +
                          std::to_string(width) + "x" + std::to_string(height) + ".ppm");
            std::ofstream file(path, std::ios::binary);
            file << "P6\n" << width << ' ' << height << "\n255\n";
            for (const auto& pixel : enabled ? a : b)
                file.write(reinterpret_cast<const char*>(pixel.data()), 3);
            check(bool(file.flush()), "Motion Gallery image write failed");
        }
        std::cout << "Gallery104 " << width << 'x' << height << ": " << different
                  << " changed motion pixels, alpha/background exact PASS\n";
    }
    context->WaitForIdle();
}
} // namespace forge::test_motion_scene
