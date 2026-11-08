#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "fixtures/prefab_structure_gallery.hpp"
#include "gallery_planar_coverage.hpp"
#include "runtime_package.hpp"
#include <iostream>

using namespace forge;
int main(int argc, char** argv) {
    try {
        using namespace test_prefab_structure_gallery;
        check(argc == 3, "Provide Gallery and isolated output directories");
        const auto output = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto source = output / "source", package = output / "export",
                   moved = output / "relocated";
        std::filesystem::create_directories(output);
        std::filesystem::copy(argv[1], source, std::filesystem::copy_options::recursive);
        if (!std::filesystem::exists(source / scene_path))
            author(source);
        verify_sources(source);
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
        std::vector<test_gallery_capture::PlanarSilhouette> shapes;
        const auto rectangle = [&](double x, double y, double width, double height) {
            shapes.push_back({{{{-width / 2, -height / 2},
                                {width / 2, -height / 2},
                                {width / 2, height / 2},
                                {-width / 2, height / 2}}},
                              {x, y}});
        };
        for (double x : {-3., 0., 3.})
            rectangle(x, -.7, 1.4, 1.4);
        rectangle(-3, .6, 2, .6);
        for (double x : {0., 3.})
            rectangle(x, .6, 2.4, .2);
        for (double x : {-.9, .9, 3.})
            rectangle(x, 1.1, .6, .8);
        const auto coverage = test_gallery_capture::planar_coverage(before[0], shapes, 5);
        const std::array roots{id};
        {
            ProjectLease lease(source);
            prepare_runtime_content_catalog(lease, roots, {});
        }
        package_runtime_content(source, package, roots, {"windows", "d3d12"});
        std::filesystem::rename(package, moved);
        std::filesystem::rename(source, output / "source-hidden");
        const auto after = test_gallery_capture::render(presentation, graphics, moved, id,
                                                        output / "exported", 3, false, {1, 2, 3});
        check(before == after && before.size() == 4,
              "Source-hidden relocated export changed structural variant Game pixels");
        const Json receipt{{"scene", id},
                           {"scene_path", scene_path},
                           {"exact_image_pairs", before.size()},
                           {"coverage", coverage},
                           {"source6_local_removal_parenting_and_chaining_checked", true},
                           {"scope", "Production Game and relocated source-hidden export on "
                                     "software Vulkan; no Windows or physical acceptance"}};
        std::ofstream(output / "receipt.json") << receipt.dump(2) << '\n';
        std::cout << receipt.dump() << '\n';
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
