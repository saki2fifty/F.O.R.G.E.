#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "fixtures/concave_mesh_gallery.hpp"
#include "gallery_game_capture.hpp"
#include "runtime_package.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

using namespace forge;
namespace {
Json coverage(const test_gallery_capture::Pixels& pixels) {
    using namespace test_concave_gallery;
    const auto background = pixels.front();
    unsigned interior = 0, exterior = 0;
    // Independent even/odd contour test; never inspect generated triangles.
    for (unsigned y = 0; y < 360; ++y)
        for (unsigned x = 0; x < 640; ++x) {
            const std::array<double, 2> world{(double(x) + .5 - 320) * (7. / 360),
                                              (180 - double(y) - .5) * (7. / 360)};
            bool inside = false, near = false;
            for (unsigned shape = 0; shape < outlines.size(); ++shape) {
                const auto& contour = outlines[shape];
                const std::array<double, 2> p{world[0] - places[shape][0],
                                              world[1] - places[shape][1]};
                bool contained = false;
                for (unsigned i = 0; i < contour.size(); ++i) {
                    const auto& a = contour[i];
                    const auto& b = contour[(i + 1) % contour.size()];
                    const double dx = b[0] - a[0], dy = b[1] - a[1];
                    const double t = std::clamp(
                        ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / (dx * dx + dy * dy), 0., 1.);
                    near |= std::hypot(p[0] - a[0] - t * dx, p[1] - a[1] - t * dy) < .04;
                    if ((a[1] > p[1]) != (b[1] > p[1]) && p[0] < a[0] + (p[1] - a[1]) * dx / dy)
                        contained = !contained;
                }
                inside |= contained;
            }
            if (near)
                continue;
            check((pixels[y * 640 + x] != background) == inside,
                  "Concave Game filled a notch or lost independently classified silhouette");
            if (inside)
                ++interior;
            else
                ++exterior;
        }
    check(interior > 20000 && exterior > 150000, "Concave oracle did not cover enough pixels");
    return {{"interior_pixels", interior}, {"exterior_pixels", exterior}};
}
} // namespace
int main(int argc, char** argv) {
    try {
        using namespace test_concave_gallery;
        check(argc == 3, "Need Gallery and isolated concave Mesh output");
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
        check(device && graphics, "Concave Vulkan device unavailable");
        DiligentPresentation presentation(device);
        const auto before = test_gallery_capture::render(presentation, graphics, source, id,
                                                         out / "authored", 3, false, {1, 2, 3});
        const auto oracle = coverage(before[0]);
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
                  "Editable concave source leaked into export");
        const auto after = test_gallery_capture::render(presentation, graphics, moved, id,
                                                        out / "exported", 3, false, {1, 2, 3});
        check(before.size() == 4 && before == after, "Source-hidden concave Game pixels changed");
        const Json receipt{{"scene", id},
                           {"scene_path", scene_path},
                           {"meshes", 3},
                           {"exact_image_pairs", before.size()},
                           {"coverage", oracle},
                           {"legacy_factory_source_exact", true},
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
