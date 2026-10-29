#pragma once
#include "ambient_occlusion_scene_tests.hpp"
#include <forge/game_content.hpp>
#include <forge/runtime.hpp>
#include <fstream>

namespace forge::test_occlusion_scene {
inline void gallery(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                    const std::filesystem::path& project, const std::filesystem::path& images,
                    bool material_normals = false) {
    using test_sprite_draw::check;
    const auto id = AssetId::parse(material_normals ? "277f080c-b97b-4202-bd31-5cded7e4dd18"
                                                    : "d4b6e7a5-5c2a-4ee0-852d-cb53ffc7319a");
    auto host = std::make_shared<MeshResourceHost>(presentation, context, project, true);
    host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(project)));
    EngineContext engine(WorldRole::Runtime, false);
    Scene scene(engine.world());
    scene.restore_snapshot(load_game_scene(project, {id}));
    RuntimeSimulation simulation(engine.world(), scene);
    simulation.reset_presentation();
    const auto source = extract_render_scene(simulation.presentation(1));
    auto disabled = source;
    if (material_normals)
        disabled.settings.ambient_occlusion.normal_influence = 0;
    else
        disabled.settings.ambient_occlusion.enabled = false;
    std::filesystem::create_directories(images);
    for (const auto& [width, height] : {std::pair{640u, 360u}, std::pair{360u, 640u}}) {
        FrameRenderer effect(presentation), ordinary(presentation);
        effect.resources(host);
        ordinary.resources(host);
        test_sprite_draw::Pixels a, b;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(40);
        do {
            presentation.begin_frame();
            auto* current = effect.game(context, source, width, height);
            auto* reference = ordinary.game(context, disabled, width, height);
            presentation.end_frame(context);
            host->submit();
            a = test_sprite_draw::read_pixels(presentation.device(), context,
                                              current->GetTexture());
            b = test_sprite_draw::read_pixels(presentation.device(), context,
                                              reference->GetTexture());
            context->FinishFrame();
            presentation.device()->ReleaseStaleResources();
            check(std::chrono::steady_clock::now() < deadline, "Ambient Gallery resources stalled");
        } while (effect.pending() || ordinary.pending());
        check(effect.diagnostics().empty() && ordinary.diagnostics().empty(),
              "Ambient Gallery presentation reported errors");
        std::size_t changed = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            changed += a[i] != b[i];
            check(a[i][3] == b[i][3], "Ambient Gallery changed current alpha");
            if (b[i] == b.front())
                check(a[i] == b[i], "Ambient Gallery changed clear background");
        }
        check(changed > 100, "Ambient Gallery has no visible contact shading");
        for (bool enabled : {true, false}) {
            const auto path =
                images /
                (std::string(material_normals ? "normal-ambient-gallery-" : "ambient-gallery-") +
                 (material_normals ? enabled ? "normal-" : "geometric-"
                  : enabled        ? "enabled-"
                                   : "disabled-") +
                 std::to_string(width) + "x" + std::to_string(height) + ".ppm");
            std::ofstream file(path, std::ios::binary);
            file << "P6\n" << width << ' ' << height << "\n255\n";
            for (const auto& pixel : enabled ? a : b)
                file.write(reinterpret_cast<const char*>(pixel.data()), 3);
            check(bool(file.flush()), "Ambient Gallery image write failed");
        }
        std::cout << (material_normals ? "Gallery108 " : "Gallery105 ") << width << 'x' << height
                  << ": " << changed << " contact pixels changed, alpha/background exact PASS\n";
    }
    context->WaitForIdle();
}
} // namespace forge::test_occlusion_scene
