#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "asset_bytes.hpp"
#include "bounded_json.hpp"
#include "frame_renderer.hpp"
#include "runtime_package.hpp"
#include "sprite_draw_render_tests.hpp"
#include <forge/game_content.hpp>
#include <forge/runtime_world.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
using namespace Diligent;
namespace {
using Pixels = test_sprite_draw::Pixels;
void check(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
void save(const std::filesystem::path& path, const Pixels& pixels, unsigned width,
          unsigned height) {
    std::ofstream file(path, std::ios::binary);
    file << "P6\n" << width << ' ' << height << "\n255\n";
    for (const auto& pixel : pixels)
        file.write(reinterpret_cast<const char*>(pixel.data()), 3);
    check(bool(file.flush()), "Runtime reflection Gallery image write failed");
}
std::vector<Pixels> exercise(DiligentPresentation& presentation, IDeviceContext* context,
                             const std::filesystem::path& root, AssetId id,
                             const std::filesystem::path& images) {
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root, true);
    host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
    std::filesystem::create_directories(images);
    std::vector<Pixels> result;
    for (const auto& [width, height] : {std::pair{480u, 270u}, std::pair{270u, 480u}}) {
        RuntimeWorld world({}, {}, std::nullopt, root, false);
        world.scene.restore_snapshot(load_game_scene(root, {id}));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (!world.prepare_scene_resources()) {
            check(std::chrono::steady_clock::now() < deadline, "Runtime Gallery resources stalled");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        world.simulation.reset_presentation();
        FrameRenderer frame(presentation);
        frame.resources(host);
        const auto draw = [&](RenderScene scene, bool runtime, double seconds) {
            check(scene.simulation_seconds.has_value(),
                  "Actual runtime probe omitted simulation time");
            check(scene.reflection_probes.size() == 1 &&
                      scene.reflection_probes[0].probe.runtime_capture,
                  "Gallery did not load an active runtime probe");
            scene.reflection_probes[0].probe.runtime_capture = runtime;
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            ITextureView* image = nullptr;
            do {
                presentation.begin_frame();
                image = frame.game(context, scene, width, height, seconds);
                presentation.end_frame(context);
                host->submit();
                context->Flush();
                context->FinishFrame();
                presentation.device()->ReleaseStaleResources();
                check(std::chrono::steady_clock::now() < end, "Runtime Gallery rendering stalled");
            } while (frame.pending());
            if (!frame.diagnostics().empty())
                throw std::runtime_error(frame.diagnostics().front().text);
            check(!frame.omitted_diagnostics(), "Runtime Gallery omitted diagnostics");
            return test_sprite_draw::read_pixels(presentation.device(), context,
                                                 image->GetTexture());
        };
        const auto initial_snapshot = world.simulation.presentation(1, 0);
        const auto initial = extract_render_scene(initial_snapshot);
        check(initial.meshes.size() == 17 && initial.cameras.size() == 1 &&
                  initial.diagnostics.empty(),
              "Runtime Gallery lost source geometry or Camera");
        const auto off0 = draw(initial, false, 0);
        const auto on0 = draw(initial, true, 0);
        for (unsigned tick = 0; tick < 60; ++tick)
            world.simulation.tick(1.f / 60);
        const auto later_snapshot = world.simulation.presentation(1, 1);
        check(later_snapshot.at("entities") != initial_snapshot.at("entities"),
              "Original Gallery source animation did not advance");
        const auto later = extract_render_scene(later_snapshot);
        const auto on1 = draw(later, true, 1);
        check(draw(later, true, 10000) == on1, "Paused simulation followed presentation wall time");
        const auto off1 = draw(later, false, 1);
        check(off1 == off0, "Hidden source animation changed the static fallback view");
        unsigned changed = 0;
        for (unsigned i = 0; i < on0.size(); ++i) {
            changed += on0[i] != on1[i];
            check(on0[i][3] == on1[i][3], "Runtime reflection changed output opacity");
        }
        check(changed > 100, "Moving hidden sources did not change reflected lighting");
        const auto suffix = std::to_string(width) + "x" + std::to_string(height) + ".ppm";
        save(images / ("runtime-reflection-initial-" + suffix), on0, width, height);
        save(images / ("runtime-reflection-moving-" + suffix), on1, width, height);
        save(images / ("runtime-reflection-fallback-" + suffix), off0, width, height);
        result.push_back(on0);
        result.push_back(on1);
        result.push_back(off0);
        std::cout << "Runtime Gallery " << width << 'x' << height << ": " << changed
                  << " pixels changed by captured source animation\n"
                  << std::flush;
    }
    return result;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Expected Gallery project and scratch directory");
        const auto gallery = std::filesystem::absolute(argv[1]);
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto source = scratch / "Authored";
        std::filesystem::create_directories(scratch);
        std::filesystem::copy(gallery, source, std::filesystem::copy_options::recursive);
        const auto scene_path =
            std::filesystem::path("Scenes/176-runtime-reflection-capture.scene.json");
        const auto source_bytes = asset_detail::read_bytes(source / scene_path, 64 * 1024 * 1024);
        const auto id = asset_detail::parse_bounded_json(source_bytes, 64 * 1024 * 1024)
                            .at("asset_id")
                            .get<AssetId>();
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo create;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        check(device && context, "Runtime Gallery Vulkan device unavailable");
        DiligentPresentation presentation(device);
        const auto authored =
            exercise(presentation, context, source, id, scratch / "authored-images");
        check(asset_detail::read_bytes(source / scene_path, 64 * 1024 * 1024) == source_bytes,
              "Runtime capture changed the authored Scene source");
        const std::array roots{id};
        const auto package = scratch / "Export", relocated = scratch / "Relocated";
        (void)package_runtime_content(source, package, roots, {"windows", "d3d12"});
        std::filesystem::rename(package, relocated);
        std::filesystem::rename(source, scratch / "Source-hidden");
        check(!std::filesystem::exists(source) &&
                  !std::filesystem::exists(relocated / scene_path) &&
                  !std::filesystem::exists(relocated / "Assets/ReflectionCapture/Reflection.dds") &&
                  !std::filesystem::exists(relocated / "Assets/RenderTextures/Spin.animation.json"),
              "Relocation retained authored sources or the original root");
        check(exercise(presentation, context, relocated, id, scratch / "relocated-images") ==
                  authored,
              "Source-hidden relocation changed runtime captures, animation or saved fallback");
        atomic_write(scratch / "receipt.json",
                     Json{{"scene", id},
                          {"exact_readbacks", 6},
                          {"original_source_hidden", true},
                          {"authored_sources_packaged", false},
                          {"limit", "Native RuntimeWorld/FrameRenderer software Vulkan; no Windows "
                                    "or physical acceptance"}}
                         .dump(2));
        std::cout
            << "Runtime reflection Gallery: six source-hidden relocated readbacks exact PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
