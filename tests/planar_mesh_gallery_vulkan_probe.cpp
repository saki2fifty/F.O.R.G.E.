#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "fixtures/planar_mesh_gallery.hpp"
#include "gallery_game_capture.hpp"
#include "runtime_package.hpp"
#include <cmath>
#include <iostream>

using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void coverage(const test_gallery_capture::Pixels& pixels) {
    using Point = std::array<double, 2>;
    const std::array<std::vector<Point>, 3> contours{
        std::vector<Point>{{-3, -1}, {-1, -1}, {-1, 1}, {-3, 1}},
        std::vector<Point>{{1, -1}, {3, -1}, {3, 1}, {1, 1}, {0, 0}},
        std::vector<Point>{{.75, -2.25}, {0, -1}, {-.75, -2.25}}};
    const auto background = pixels.front();
    unsigned checked_inside = 0, checked_outside = 0;
    for (unsigned y = 0; y < 360; ++y)
        for (unsigned x = 0; x < 640; ++x) {
            const Point p{(double(x) + .5 - 320) / 60, (180 - double(y) - .5) / 60};
            bool inside = false, near = false;
            for (const auto& contour : contours) {
                bool contained = true;
                for (std::size_t i = 0; i < contour.size(); ++i) {
                    const auto a = contour[i], b = contour[(i + 1) % contour.size()];
                    const double dx = b[0] - a[0], dy = b[1] - a[1];
                    const double distance =
                        (dx * (p[1] - a[1]) - dy * (p[0] - a[0])) / std::hypot(dx, dy);
                    contained &= distance > 0;
                    near |= std::abs(distance) < .05;
                }
                inside |= contained;
            }
            if (near)
                continue;
            const bool visible = pixels[y * 640 + x] != background;
            check(visible == inside, "Planar Game coverage differs from independent XY contours");
            if (inside)
                ++checked_inside;
            else
                ++checked_outside;
        }
    check(checked_inside > 15000 && checked_outside > 100000,
          "Planar coverage oracle did not inspect enough interior and exterior pixels");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need Gallery and isolated planar render output");
        const auto out = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(out);
        const auto source = out / "source", package = out / "export", moved = out / "relocated";
        check(!std::filesystem::exists(source), "Planar render output must be fresh");
        std::filesystem::copy(argv[1], source, std::filesystem::copy_options::recursive);
        if (!std::filesystem::exists(source / test_planar_gallery::scene_path))
            test_planar_gallery::author(source);
        Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
        Diligent::RefCntAutoPtr<Diligent::IDeviceContext> graphics;
        Diligent::EngineVkCreateInfo info;
        info.Features.GeometryShaders = Diligent::DEVICE_FEATURE_STATE_ENABLED;
        Diligent::GetEngineFactoryVk()->CreateDeviceAndContextsVk(info, &device, &graphics);
        check(device && graphics, "Planar Vulkan device unavailable");
        DiligentPresentation presentation(device);
        const auto id = AssetId::parse(test_planar_gallery::scene_uuid);
        const auto before = test_gallery_capture::render(presentation, graphics, source, id,
                                                         out / "authored", 3, false, {1, 2, 3});
        coverage(before[0]);
        const std::array roots{id};
        {
            ProjectLease lease(source);
            prepare_runtime_content_catalog(lease, roots, {});
        }
        package_runtime_content(source, package, roots, {"windows", "d3d12"});
        std::filesystem::rename(package, moved);
        std::filesystem::rename(source, out / "source-hidden");
        for (const auto* path : test_planar_gallery::mesh_paths)
            check(!std::filesystem::exists(moved / path),
                  "Editable planar source leaked into export");
        check(!std::filesystem::exists(moved / test_planar_gallery::material_path),
              "Editable material source leaked into export");
        const auto after = test_gallery_capture::render(presentation, graphics, moved, id,
                                                        out / "exported", 3, false, {1, 2, 3});
        check(before == after, "Relocated planar Game pixels differ after source removal");
        std::cout << "Planar quad/polygon/clockwise-source Game, independent XY coverage and "
                     "source-hidden relocation PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
