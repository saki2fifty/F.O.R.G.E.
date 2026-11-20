#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "particle_gpu_material_checks.hpp"
#include "particle_gpu_material_scene_checks.hpp"
#include "particle_gpu_order_checks.hpp"
#include "particle_gpu_sorted_render_checks.hpp"
int main(int argc, char** argv) {
    try {
        const bool numeric = argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--numeric");
        const bool scene = argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--scene");
        const bool materials = argc == 2 && std::string_view(argv[1]) == "--materials";
        const bool material_scene = argc == 3 && std::string_view(argv[1]) == "--materials-scene";
        if (!numeric && !scene && !materials && !material_scene)
            throw std::runtime_error(
                "Choose --numeric, --scene, --materials or --materials-scene <scratch>");
        Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
        Diligent::RefCntAutoPtr<Diligent::IDeviceContext> context;
        Diligent::EngineVkCreateInfo create;
        create.Features.ComputeShaders = Diligent::DEVICE_FEATURE_STATE_ENABLED;
        Diligent::GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        forge::particle_order_test::check(device && context, "Native GPU ordering unavailable");
        forge::DiligentPresentation presentation(device);
        if (material_scene)
            forge::test_particle_palette_scene::exercise(presentation, context, argv[2]);
        if (materials)
            forge::test_particle_palette::exercise(presentation, context);
        if (numeric)
            forge::particle_order_test::execute(presentation, context);
        if (scene)
            forge::particle_sorted_test::execute(presentation, context,
                                                 std::filesystem::current_path() /
                                                     "particle-gpu-sorted-scene");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
