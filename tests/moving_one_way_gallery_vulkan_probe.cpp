#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "frame_renderer.hpp"
#include "moving_one_way_scene_tests.hpp"
#include "sprite_draw_render_tests.hpp"
#include <forge/game_content.hpp>
#include <forge/physics.hpp>
#include <forge/runtime_world.hpp>
#include <iostream>
#include <set>
using namespace forge;
using namespace Diligent;
namespace {
using test_moving_one_way_scene::check;
using Pixels = std::vector<std::array<std::uint8_t, 4>>;
void ppm(const std::filesystem::path& path, const Pixels& pixels, unsigned width, unsigned height) {
    std::ofstream stream(path, std::ios::binary);
    stream << "P6\n" << width << ' ' << height << "\n255\n";
    for (const auto& value : pixels)
        stream.write(reinterpret_cast<const char*>(value.data()), 3);
    check(bool(stream), "Moving Gallery image write failed");
}
std::vector<Pixels> render(DiligentPresentation& presentation, IDeviceContext* context,
                           const std::filesystem::path& root, AssetId scene_id,
                           const std::filesystem::path& output) {
    RuntimeWorld world({}, {}, std::nullopt, root, false);
    world.scene.restore_snapshot(load_game_scene(root, {scene_id}));
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!world.physics()->prepare_assets() || !world.prepare_scene_resources()) {
        check(std::chrono::steady_clock::now() < deadline, "Moving Gallery native assets stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root);
    host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
    FrameRenderer frame(presentation);
    frame.resources(host);
    std::filesystem::create_directories(output);
    std::vector<Pixels> results;
    const auto capture = [&](unsigned tick, unsigned width, unsigned height) {
        const auto scene = extract_render_scene(world.simulation.presentation(1));
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
        Pixels pixels;
        do {
            presentation.begin_frame();
            auto* image = frame.game(context, scene, width, height);
            check(image, "Moving Gallery has no Game display");
            presentation.end_frame(context);
            host->submit();
            pixels =
                test_sprite_draw::read_pixels(presentation.device(), context, image->GetTexture());
            context->FinishFrame();
            presentation.device()->ReleaseStaleResources();
            check(std::chrono::steady_clock::now() < deadline,
                  "Moving Gallery visual assets stalled");
            if (frame.pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (frame.pending());
        if (!frame.diagnostics().empty())
            throw std::runtime_error(frame.diagnostics().front().text);
        check(frame.omitted_diagnostics() == 0 && pixels.size() == std::size_t(width) * height,
              "Moving Gallery display is incomplete");
        std::set<std::array<std::uint8_t, 4>> colors(pixels.begin(), pixels.end());
        check(colors.size() > 20, "Moving Gallery did not render its textured course and actors");
        ppm(output / (std::to_string(tick) + "-" + std::to_string(width) + ".ppm"), pixels, width,
            height);
        results.push_back(std::move(pixels));
    };
    for (unsigned tick = 1; tick <= 90; ++tick) {
        world.simulation.tick(1.f / 60);
        if (tick == 30 || tick == 60 || tick == 90)
            capture(tick, 640, 360);
    }
    capture(90, 360, 640);
    unsigned changed = 0;
    for (std::size_t i = 0; i < results[0].size(); ++i)
        changed += results[0][i] != results[2][i];
    check(changed > 100, "Moving Gallery did not visibly animate its gameplay");
    return results;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Need Gallery, scratch and matching asset worker");
        const auto prepared = test_moving_one_way_scene::prepare(argv[1], argv[2]);
        test_moving_one_way_scene::settle(prepared, argv[3]);
        EngineVkCreateInfo create;
        create.Features.GeometryShaders = DEVICE_FEATURE_STATE_ENABLED;
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        check(device && context, "Moving Gallery Vulkan device unavailable");
        DiligentPresentation presentation(device);
        const auto captures = prepared.root.parent_path() / (prepared.scene.str() + "-images");
        const auto authored =
            render(presentation, context, prepared.root, prepared.scene, captures / "authored");
        const auto output = prepared.root.parent_path() / (prepared.scene.str() + "-content");
        const std::array roots{prepared.scene};
        const auto target = desktop_texture_target();
        package_runtime_content(prepared.root, output, roots, {target.platform, target.backend});
        const auto moved = prepared.root.parent_path() / (prepared.scene.str() + " Relocated");
        std::filesystem::rename(output, moved);
        std::filesystem::rename(prepared.root, prepared.root.string() + "-source-offline");
        const auto exported =
            render(presentation, context, moved, prepared.scene, captures / "relocated");
        check(authored == exported, "Relocated source-free moving Gallery displays changed");
        std::ofstream receipt(captures / "receipt.json");
        receipt << Json{{"scene", prepared.scene},
                        {"exact_display_pairs", 4},
                        {"source", test_moving_one_way_scene::source},
                        {"scope", "Actual native Game gameplay/display on software Vulkan; no "
                                  "Windows, physical GPU or graphical EXE acceptance"}}
                       .dump(2)
                << '\n';
        check(bool(receipt), "Moving Gallery display receipt write failed");
        context->WaitForIdle();
        std::cout << "Moving one-way Gallery: actual animated Game and four exact source-free "
                     "relocated displays PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
