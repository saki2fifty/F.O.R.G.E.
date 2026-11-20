#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "particle_gpu_collision_checks.hpp"
using namespace forge;
using namespace Diligent;
int main() {
    try {
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo create;
        create.Features.ComputeShaders = DEVICE_FEATURE_STATE_ENABLED;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        particle_plane_test::check(device && context, "Native plane compute unavailable");
        DiligentPresentation presentation(device);
        particle_plane_test::check_boundaries(device, context, presentation);
        particle_plane_test::check_runtime(device, context, presentation);
        context->WaitForIdle();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
