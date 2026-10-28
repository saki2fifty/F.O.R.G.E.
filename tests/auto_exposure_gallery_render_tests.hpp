#pragma once
#include <forge/animation.hpp>
#include <forge/game_content.hpp>
#include <forge/runtime.hpp>
#include <thread>

namespace forge::test_exposure_gallery {
inline void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                     const std::filesystem::path& root, const std::filesystem::path& images) {
    const auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    const auto id = AssetId::parse("43420ace-33d5-4768-ae3f-771fd7ec384a");
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root, true);
    host->catalog(catalog);
    EngineContext engine(WorldRole::Runtime, false, {animation_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(load_game_scene(root, {id}));
    const auto animation = animation_runtime(engine.world());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!animation->prepare_initial_pose()) {
        require(std::chrono::steady_clock::now() < deadline,
                "Exposure Gallery light clip timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    RuntimeSimulation simulation(engine.world(), scene);
    simulation.reset_presentation();
    FrameRenderer game(presentation);
    game.resources(host);
    const auto render = [&](double time) {
        const auto ready = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        Pixels pixels;
        do {
            presentation.begin_frame();
            auto* output = game.game(context, simulation.presentation(1), 640, 360, time);
            presentation.end_frame(context);
            host->submit();
            pixels = readback(presentation.device(), context, output);
            context->FinishFrame();
            presentation.device()->ReleaseStaleResources();
            require(std::chrono::steady_clock::now() < ready, "Exposure Gallery resources stalled");
        } while (game.pending());
        require(game.diagnostics().empty(),
                "Exposure Gallery Game image has rendering diagnostics");
        return pixels;
    };
    const auto initial = render(0);
    for (unsigned step = 0; step < 8; ++step)
        simulation.tick(.25f);
    const auto flash = render(2);
    Pixels settled;
    for (unsigned frame = 1; frame <= 16; ++frame)
        settled = render(2 + frame * .125);
    const auto brightness = [](const Pixels& pixels) {
        double sum = 0;
        for (const auto& pixel : pixels)
            sum += .2126 * pixel[0] + .7152 * pixel[1] + .0722 * pixel[2];
        return sum / pixels.size();
    };
    require(brightness(initial) > 10 && brightness(flash) > brightness(settled) + 10 &&
                initial != settled,
            "Actual animated Gallery image failed to settle after its light change");
    std::filesystem::create_directories(images);
    save(initial, 640, 360, images / "automatic-exposure-dark.ppm");
    save(flash, 640, 360, images / "automatic-exposure-transition.ppm");
    save(settled, 640, 360, images / "automatic-exposure-settled.ppm");
    std::cout << "Gallery96 native light change/adapted pixels: " << brightness(initial) << ','
              << brightness(flash) << ',' << brightness(settled) << '\n';
}
} // namespace forge::test_exposure_gallery
