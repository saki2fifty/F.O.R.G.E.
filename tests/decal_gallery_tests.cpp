#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "decal_scene_tests.hpp"
#include "frame_renderer.hpp"
#include "sprite_draw_render_tests.hpp"
#include <chrono>
#include <iostream>
#include <set>
#include <thread>
using namespace Diligent;
using namespace forge;
namespace {
using test_decal_scene::check;
using Pixels = test_sprite_draw::Pixels;
Json read(const std::filesystem::path& path) {
    std::ifstream file(path);
    check(bool(file), "Decal content document is missing");
    return Json::parse(file);
}
void save(const std::filesystem::path& path, const Pixels& pixels, unsigned width,
          unsigned height) {
    std::ofstream file(path, std::ios::binary);
    file << "P6\n" << width << ' ' << height << "\n255\n";
    for (const auto& pixel : pixels)
        file.write(reinterpret_cast<const char*>(pixel.data()), 3);
    check(bool(file), "Decal display capture write failed");
}
std::vector<Pixels> render(DiligentPresentation& presentation, IDeviceContext* context,
                           const std::filesystem::path& root, const AssetCatalog& catalog,
                           AssetId scene_id, const std::filesystem::path& captures,
                           bool check_projection, bool surface_channels) {
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root);
    host->catalog(std::make_shared<const AssetCatalog>(catalog));
    EngineContext engine(WorldRole::Runtime, false);
    Scene runtime(engine.world());
    runtime.restore_snapshot(read(root / catalog.records().at(scene_id).source));
    auto scene = extract_render_scene(runtime.effective_document());
    check(scene.decals.size() == (surface_channels ? 3 : 4) && scene.diagnostics.empty(),
          "Saved decal scene failed native extraction");
    FrameRenderer frame(presentation);
    frame.resources(host);
    const auto image = [&](const RenderScene& source, unsigned width, unsigned height) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
        Pixels pixels;
        do {
            presentation.begin_frame();
            auto* target = frame.game(context, source, width, height);
            check(target, "Decal Gallery has no display output");
            presentation.end_frame(context);
            host->submit();
            pixels =
                test_sprite_draw::read_pixels(presentation.device(), context, target->GetTexture());
            context->FinishFrame();
            presentation.device()->ReleaseStaleResources();
            check(std::chrono::steady_clock::now() < deadline, "Decal Gallery resources stalled");
            if (frame.pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (frame.pending());
        if (!frame.diagnostics().empty())
            throw std::runtime_error(frame.diagnostics().front().text);
        check(frame.omitted_diagnostics() == 0 && pixels.size() == std::size_t(width) * height,
              "Decal Gallery capture or diagnostics were incomplete");
        return pixels;
    };
    std::filesystem::create_directories(captures);
    std::vector<Pixels> result;
    for (const auto& [width, height] :
         {std::pair{640u, 360u}, {360u, 640u}, {96u, 64u}, {17u, 13u}}) {
        auto pixels = image(scene, width, height);
        check(std::any_of(pixels.begin(), pixels.end(),
                          [](const auto& p) { return p[0] || p[1] || p[2]; }),
              "Decal Gallery is blank");
        check(std::none_of(pixels.begin(), pixels.end(),
                           [](const auto& p) { return p[0] == 255 && p[1] == 0 && p[2] == 255; }),
              "Decal Gallery reached the numerical rejection color");
        if (check_projection && width == 640) {
            auto ordinary = scene;
            ordinary.decals.clear();
            const auto plain = image(ordinary, width, height);
            unsigned changed = 0;
            for (std::size_t i = 0; i < pixels.size(); ++i)
                changed += pixels[i] != plain[i];
            check(changed > 100, "Textured and color projectors did not change the Gallery");
            for (const auto& projector : scene.decals) {
                auto individual = scene;
                individual.decals = {projector};
                const auto isolated = image(individual, width, height);
                unsigned affected = 0;
                for (std::size_t i = 0; i < isolated.size(); ++i)
                    affected += isolated[i] != plain[i];
                check(affected > 5, "An authored Gallery projector has no visible effect");
            }
            check(image(scene, width, height) == pixels,
                  "Gallery failed projector removal/re-add recovery");
        }
        save(captures / (std::to_string(width) + "x" + std::to_string(height) + ".ppm"), pixels,
             width, height);
        result.push_back(std::move(pixels));
    }
    context->WaitForIdle();
    return result;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc >= 3 && argc <= 5,
              "Need Gallery, scratch root and optional --prepare-only / --surface-channels");
        bool prepare_only = false, surface_channels = false;
        for (int i = 3; i < argc; ++i) {
            const std::string_view option(argv[i]);
            check(option == "--prepare-only" || option == "--surface-channels",
                  "Unknown Gallery option");
            prepare_only = prepare_only || option == "--prepare-only";
            surface_channels = surface_channels || option == "--surface-channels";
        }
        const auto prepared = test_decal_scene::prepare(argv[1], argv[2], surface_channels);
        const auto catalog = AssetCatalog::open_project(prepared.root);
        std::cout << Json{{"project", prepared.root.string()},
                          {"scene", prepared.scene},
                          {"prior_records", prepared.prior_records},
                          {"created", prepared.created}}
                         .dump()
                  << '\n';
        if (prepare_only)
            return 0;
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo create;
        create.Features.GeometryShaders = DEVICE_FEATURE_STATE_ENABLED;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        check(device && context, "Decal Gallery native device unavailable");
        DiligentPresentation presentation(device);
        const auto authored = render(presentation, context, prepared.root, catalog, prepared.scene,
                                     prepared.scratch / "authored", true, surface_channels);
        const std::array roots{prepared.scene};
        const auto package = prepared.scratch / "Export";
        (void)package_runtime_content(prepared.root, package, roots, {"windows", "d3d12"});
        const auto relocated = prepared.scratch / "Relocated";
        std::filesystem::rename(package, relocated);
        const auto shipped = open_runtime_content(relocated, {"windows", "d3d12"});
        std::set<AssetId> expected{
            prepared.scene,
            AssetId::parse(surface_channels ? "7ed3592a-fab7-41d2-a5c4-c4c7c5eda387"
                                            : "7949266a-8b55-4b6e-beb6-37526ca79446"),
            AssetId::parse(surface_channels ? "45cb1cd6-c3ae-4f51-a7ff-314eea5cc26f"
                                            : "0c07b499-c865-4e5b-838d-8021bb7a99d3")};
        std::set<AssetId> actual;
        for (const auto& [id, record] : shipped.records()) {
            (void)record;
            actual.insert(id);
        }
        check(actual == expected,
              "Projected Material/Texture export closure differs from typed ownership");
        check(!std::filesystem::exists(relocated / "Assets/Sprites/Badge.material.json"),
              "Material source leaked into the runtime package");
        std::filesystem::rename(prepared.root, prepared.scratch / "Source-offline");
        const auto exported = render(presentation, context, relocated, shipped, prepared.scene,
                                     prepared.scratch / "relocated", false, surface_channels);
        check(authored == exported, "Source-free relocated decal images changed");
        std::ofstream receipt(prepared.scratch / "receipt.json");
        receipt << Json{{"scene", prepared.scene},
                        {"exact_readback_pairs", 4},
                        {"asset_closure", expected},
                        {"created", prepared.created},
                        {"scope", "Actual FrameRenderer display pixels on software Vulkan; no "
                                  "Windows or physical GPU acceptance"}}
                       .dump(2)
                << '\n';
        check(bool(receipt), "Decal Gallery receipt write failed");
        std::cout
            << "Editable projector Gallery, typed Material/Texture closure, source-free relocation "
               "and four exact display readbacks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
