#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "editor/authoring.hpp"
#include "editor/viewport.hpp"
#include "gallery_game_capture.hpp"
#include "runtime_package.hpp"
#include <iostream>
using namespace forge;
using namespace Diligent;
namespace {
Json scene_view(DiligentPresentation& presentation, IDeviceContext* context,
                const std::filesystem::path& root, AssetId id) {
    EngineContext engine(WorldRole::Runtime, false);
    Scene scene(engine.world());
    scene.restore_snapshot(load_game_scene(root, {id}));
    const auto source = scene.effective_document();
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root);
    host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
    forge::Viewport viewport(presentation);
    viewport.resources(host);
    viewport.preview_lighting(true);
    EditorCamera camera;
    camera.target = {-.5f, 1, 0};
    camera.pitch = -.35f;
    camera.distance = 20;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    unsigned settled = 0;
    do {
        presentation.begin_frame();
        auto* image =
            viewport.render(context, source, 640, 360, camera, scene.revision(), false, {false});
        test_gallery_capture::check(image, "Foliage Scene View has no output");
        presentation.end_frame(context);
        host->submit();
        (void)test_sprite_draw::read_pixels(presentation.device(), context, image->GetTexture());
        context->FinishFrame();
        presentation.device()->ReleaseStaleResources();
        test_gallery_capture::check(std::chrono::steady_clock::now() < deadline,
                                    "Foliage Scene View resources stalled");
        settled = viewport.meshes()->pending() ? 0 : settled + 1;
        if (!settled)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (settled < 2);
    const auto* meshes = viewport.meshes();
    test_gallery_capture::check(meshes->diagnostics().empty() && meshes->omitted_diagnostics() == 0,
                                "Foliage Scene View rejected its Mesh/material values");
    const auto stats = meshes->draw_stats();
    const auto counts = Json{{"instances", stats.instances},
                             {"batched_calls", stats.batched_calls},
                             {"calls", stats.calls}};
    const auto failure = "Foliage static Mesh draw counts: " + counts.dump();
    test_gallery_capture::check(stats.instances == 195 && stats.batched_calls > 0 &&
                                    stats.calls < stats.instances,
                                failure.c_str());
    std::string prototype;
    for (const auto& e : source.at("entities"))
        if (e.at("name").get<std::string>().starts_with("Blade template"))
            prototype = e.at("id");
    const auto point = project_point(camera, {-6.875f, .8f, -3}, 640, 360);
    test_gallery_capture::check(
        point && viewport.pick(source, camera, 640, 360, (*point)[0], (*point)[1]) == prototype,
        "Independent template picking failed after batched foliage rendering");
    return {{"instances", stats.instances},
            {"draw_calls", stats.calls},
            {"batched_calls", stats.batched_calls},
            {"independent_template_pick", true}};
}
} // namespace
int main(int argc, char** argv) {
    try {
        test_gallery_capture::check(argc == 3, "Need Gallery and scratch paths");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto root = scratch / "Gallery";
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        EngineVkCreateInfo create;
        create.Features.GeometryShaders = DEVICE_FEATURE_STATE_ENABLED;
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        test_gallery_capture::check(device && context, "Vulkan unavailable");
        DiligentPresentation presentation(device);
        Json source;
        std::ifstream(root / "Scenes/144-foliage-placement.scene.json") >> source;
        const auto id = source.at("asset_id").get<AssetId>();
        const auto stats = scene_view(presentation, context, root, id);
        const auto authored =
            test_gallery_capture::render(presentation, context, root, id, scratch / "authored", 6);
        unsigned green = 0, amber = 0;
        for (const auto& p : authored[2]) {
            green += double(p[1]) > double(p[0]) * 1.3 && double(p[1]) > double(p[2]) * 1.1;
            amber += double(p[0]) > double(p[1]) * 1.1 && double(p[1]) > double(p[2]) * 1.4;
        }
        test_gallery_capture::check(green > 200 && amber > 100,
                                    "Gallery did not visibly render both foliage patches");
        const std::array roots{id};
        package_runtime_content(root, scratch / "Content", roots, {"windows", "d3d12"});
        std::filesystem::rename(scratch / "Content", scratch / "Relocated game");
        std::filesystem::rename(root, scratch / "SourcesOffline");
        const auto exported = test_gallery_capture::render(
            presentation, context, scratch / "Relocated game", id, scratch / "relocated", 6);
        test_gallery_capture::check(authored == exported,
                                    "Source-hidden foliage displays changed after relocation");
        Json receipt = {{"scene", id},
                        {"exact_display_pairs", 4},
                        {"scene_view", stats},
                        {"green_pixels", green},
                        {"amber_pixels", amber},
                        {"scope", "Actual Scene/Game on software Vulkan; no Windows/physical "
                                  "GPU/graphical EXE acceptance"}};
        std::ofstream(scratch / "receipt.json") << receipt.dump(2) << '\n';
        context->WaitForIdle();
        std::cout << receipt.dump() << "\nFoliage native evidence: " << scratch << '\n';
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
