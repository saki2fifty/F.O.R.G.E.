#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "fixtures/curved_mesh_gallery.hpp"
#include "gallery_planar_coverage.hpp"
#include "runtime_package.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

using namespace forge;
int main(int argc, char** argv) {
    try {
        using namespace test_curved_mesh_gallery;
        check(argc == 3, "Need Gallery and isolated curved Mesh output");
        const auto out = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(out);
        const auto source = out / "source", package = out / "export", moved = out / "relocated";
        std::filesystem::copy(argv[1], source, std::filesystem::copy_options::recursive);
        if (!std::filesystem::exists(source / scene_path))
            author(source);
        verify_sources(source);
        const auto id = read(source / scene_path).at("asset_id").get<AssetId>();
        Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
        Diligent::RefCntAutoPtr<Diligent::IDeviceContext> graphics;
        Diligent::EngineVkCreateInfo info;
        info.Features.GeometryShaders = Diligent::DEVICE_FEATURE_STATE_ENABLED;
        Diligent::GetEngineFactoryVk()->CreateDeviceAndContextsVk(info, &device, &graphics);
        check(device && graphics, "Curved Vulkan device unavailable");
        DiligentPresentation presentation(device);
        const auto before = test_gallery_capture::render(presentation, graphics, source, id,
                                                         out / "authored", 3, false, {1, 2, 3});
        std::vector<test_gallery_capture::PlanarSilhouette> shapes;
        test_gallery_capture::PlanarSilhouette ellipse;
        ellipse.position = places[0];
        ellipse.ellipse = std::array<double, 3>{1.8, 1, 0};
        shapes.push_back(ellipse);
        ellipse.position = places[1];
        ellipse.ellipse = std::array<double, 3>{1.5, 1.5, .65};
        shapes.push_back(ellipse);
        shapes.push_back({{reference_curve()}, places[2]});
        const auto oracle = test_gallery_capture::planar_coverage(before[0], shapes, 7);
        check(oracle.at("opening_pixels").get<unsigned>() > 6000,
              "Opening pixels were not checked");
        const std::array roots{id};
        {
            ProjectLease lease(source);
            prepare_runtime_content_catalog(lease, roots, {});
        }
        package_runtime_content(source, package, roots, {"windows", "d3d12"});
        std::filesystem::rename(package, moved);
        std::filesystem::rename(source, out / "source-hidden");
        for (const auto* path : mesh_paths)
            check(!std::filesystem::exists(moved / path),
                  "Editable curved source leaked into export");
        const auto after = test_gallery_capture::render(presentation, graphics, moved, id,
                                                        out / "exported", 3, false, {1, 2, 3});
        check(before.size() == 4 && before == after, "Source-hidden curved Game pixels changed");
        const Json receipt{{"scene", id},
                           {"scene_path", scene_path},
                           {"meshes", 3},
                           {"exact_image_pairs", before.size()},
                           {"coverage", oracle},
                           {"saved_source_factory_exact", true},
                           {"scope",
                            "Actual production Game/render/export and independent contour "
                            "coverage on software Vulkan; no Windows or physical acceptance"}};
        std::ofstream(out / "receipt.json") << receipt.dump(2) << '\n';
        std::cout << receipt.dump() << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
