#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "particle_gpu_trail_checks.hpp"
#include "particle_ribbon_render_tests.hpp"
int main() {
    try {
        Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
        Diligent::RefCntAutoPtr<Diligent::IDeviceContext> context;
        Diligent::EngineVkCreateInfo create;
        create.Features.ComputeShaders = Diligent::DEVICE_FEATURE_STATE_ENABLED;
        Diligent::GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        forge::particle_trail_test::check(device && context, "Native ribbon compute unavailable");
        forge::DiligentPresentation presentation(device);
        forge::particle_trail_test::execute(presentation, context);
        forge::test_particle_ribbon::gpu_scene_test(
            presentation, context, std::filesystem::current_path() / "particle-gpu-ribbon-scene");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
