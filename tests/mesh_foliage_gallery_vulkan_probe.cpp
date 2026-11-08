#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "fixtures/mesh_foliage_gallery.hpp"
#include "gallery_game_capture.hpp"
#include "runtime_package.hpp"
#include <iostream>

using namespace forge;
int main(int argc, char** argv) {
    try {
        test_mesh_foliage_gallery::check(argc == 3, "Need Gallery and Mesh meadow render output");
        const auto out = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto source = out / "source", exported = out / "export", moved = out / "relocated";
        std::filesystem::create_directories(out);
        std::filesystem::copy(argv[1], source, std::filesystem::copy_options::recursive);
        if (!std::filesystem::exists(source / test_mesh_foliage_gallery::scene_path))
            test_mesh_foliage_gallery::author(source);
        const auto document =
            test_mesh_foliage_gallery::read(source / test_mesh_foliage_gallery::scene_path);
        const auto id = document.at("asset_id").get<AssetId>();
        const std::array roots{id};
        Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
        Diligent::RefCntAutoPtr<Diligent::IDeviceContext> graphics;
        Diligent::EngineVkCreateInfo info;
        info.Features.GeometryShaders = Diligent::DEVICE_FEATURE_STATE_ENABLED;
        Diligent::GetEngineFactoryVk()->CreateDeviceAndContextsVk(info, &device, &graphics);
        test_mesh_foliage_gallery::check(device && graphics,
                                         "Mesh meadow Vulkan device unavailable");
        DiligentPresentation presentation(device);
        const auto before = test_gallery_capture::render(presentation, graphics, source, id,
                                                         out / "authored", 21, false, {1, 2, 3});
        {
            ProjectLease lease(source);
            prepare_runtime_content_catalog(lease, roots, {});
        }
        package_runtime_content(source, exported, roots, {"windows", "d3d12"});
        std::filesystem::rename(exported, moved);
        std::filesystem::rename(source, out / "source-hidden");
        test_mesh_foliage_gallery::check(
            !std::filesystem::exists(moved / test_mesh_foliage_gallery::mesh_path),
            "Editable ramp source leaked into runtime export");
        const auto after = test_gallery_capture::render(presentation, graphics, moved, id,
                                                        out / "exported", 21, false, {1, 2, 3});
        test_mesh_foliage_gallery::check(before == after, "Relocated Mesh meadow pixels changed");
        std::cout << "Mesh surface Gallery:256 saved ordinary blades, independent plane/history, "
                     "production Game and exact source-hidden relocation pass\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
