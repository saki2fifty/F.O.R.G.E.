#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "fixtures/prefab_nested_gallery.hpp"
#include "fixtures/prefab_source_layers_gallery.hpp"
#include "gallery_planar_coverage.hpp"
#include "runtime_package.hpp"
#include <iostream>

using namespace forge;
int main(int argc, char** argv) {
    try {
        using namespace test_prefab_nested_gallery;
        check(argc == 3 || (argc == 4 && std::string_view(argv[3]) == "--source-layers"),
              "Provide Gallery and isolated output directories, optionally --source-layers");
        const bool layers = argc == 4;
        const auto* scene_path = layers ? test_prefab_source_layers_gallery::scene_path
                                        : test_prefab_nested_gallery::scene_path;
        const auto* scene_uuid = layers ? test_prefab_source_layers_gallery::scene_uuid
                                        : test_prefab_nested_gallery::scene_uuid;
        const auto author =
            layers ? test_prefab_source_layers_gallery::author : test_prefab_nested_gallery::author;
        const auto verify =
            layers ? test_prefab_source_layers_gallery::verify : test_prefab_nested_gallery::verify;
        const auto output = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto source = output / "source", package = output / "export",
                   moved = output / "relocated";
        std::filesystem::create_directories(output);
        std::filesystem::copy(argv[1], source, std::filesystem::copy_options::recursive);
        if (!std::filesystem::exists(source / scene_path))
            author(source);
        verify(source);
        const auto id = AssetId::parse(scene_uuid);
        Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
        Diligent::RefCntAutoPtr<Diligent::IDeviceContext> graphics;
        Diligent::EngineVkCreateInfo info;
        info.Features.GeometryShaders = Diligent::DEVICE_FEATURE_STATE_ENABLED;
        Diligent::GetEngineFactoryVk()->CreateDeviceAndContextsVk(info, &device, &graphics);
        check(device && graphics, "Vulkan device unavailable");
        DiligentPresentation presentation(device);
        const auto before = test_gallery_capture::render(presentation, graphics, source, id,
                                                         output / "authored", 3, false, {1, 2, 3});
        auto color_proof = Json();
        if (layers) {
            const auto body_channels = [&](double world_x) {
                std::array<double, 3> sum{};
                const auto x = int(std::lround(320 + world_x * 360 / 6.6));
                const auto y = int(std::lround(180 + .7 * 360 / 6.6));
                for (int dy = -12; dy <= 12; ++dy)
                    for (int dx = -12; dx <= 12; ++dx)
                        for (unsigned channel = 0; channel < 3; ++channel)
                            sum[channel] +=
                                before[0].at(unsigned(y + dy) * 640 + unsigned(x + dx))[channel] /
                                625.;
                return sum;
            };
            const auto common = body_channels(-3.9), colored = body_channels(1.3);
            check(colored[0] + 32 < common[0] && colored[2] > .6 * common[2] &&
                      colored[2] > colored[0] + 32,
                  "Gallery material contribution did not visibly change the rendered color");
            color_proof = {{"common_rgb_mean", common}, {"contribution_rgb_mean", colored}};
        }
        std::vector<test_gallery_capture::PlanarSilhouette> shapes;
        const auto rectangle = [&](double x, double y, double width, double height) {
            shapes.push_back({{{{-width / 2, -height / 2},
                                {width / 2, -height / 2},
                                {width / 2, height / 2},
                                {-width / 2, height / 2}}},
                              {x, y}});
        };
        if (layers) {
            for (unsigned i = 0; i < 4; ++i) {
                const double x = -3.9 + 2.6 * i;
                const bool wide = i == 1 || i == 3;
                rectangle(x, -.7, wide ? 2.2 : 1.4, 1.4);
                rectangle(x, .6, wide ? 2.8 : 2, .6);
            }
        } else {
            for (double x : {-4.1, -2.5, -.8, 2.5, 4.1})
                rectangle(x, -.455, .91, .91);
            for (double x : {-4.1, -2.5, -.8, 2.5})
                rectangle(x, .39, 1.3, .39);
            rectangle(4.1, .39, 1.56, .13);
            for (double x : {4.1 - .585, 4.1 + .585})
                rectangle(x, .715, .39, .52);
        }
        const auto coverage =
            test_gallery_capture::planar_coverage(before[0], shapes, layers ? 6.6 : 5.7);
        const std::array scenes{id};
        {
            ProjectLease lease(source);
            prepare_runtime_content_catalog(lease, scenes, {});
        }
        package_runtime_content(source, package, scenes, {"windows", "d3d12"});
        std::filesystem::rename(package, moved);
        std::filesystem::rename(source, output / "source-hidden");
        verify(moved);
        const auto after = test_gallery_capture::render(presentation, graphics, moved, id,
                                                        output / "exported", 3, false, {1, 2, 3});
        check(before == after && before.size() == 4,
              "Source-hidden relocated export changed prefab composition Game pixels");
        const Json receipt{{"scene", id},
                           {"scene_path", scene_path},
                           {"exact_image_pairs", before.size()},
                           {"coverage", coverage},
                           {"source_contribution_color_channels", color_proof},
                           {"repeated_cleared_and_replaced_scopes_checked", !layers},
                           {"ordered_sparse_source_contributions_checked", layers},
                           {"scope", "Production Game and relocated source-hidden Windows-target "
                                     "data on software Vulkan; no Windows or physical acceptance"}};
        std::ofstream(output / "receipt.json") << receipt.dump(2) << '\n';
        std::cout << receipt.dump() << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
