#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "particle_ribbon_render_tests.hpp"
#include <iostream>
using namespace forge;
using namespace Diligent;
int main(int argc, char** argv) {
    try {
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo create;
        create.Features.GeometryShaders = DEVICE_FEATURE_STATE_ENABLED;
        create.Features.ComputeShaders = DEVICE_FEATURE_STATE_ENABLED;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        test_bloom::check(device && context, "Ribbon native device unavailable");
        DiligentPresentation presentation(device);
        if (argc == 3 && std::string_view(argv[1]) == "--scene")
            test_particle_ribbon::scene_test(presentation, context,
                                             std::filesystem::absolute(argv[2]));
        else {
            test_bloom::check(argc == 1, "Choose the numeric ribbon probe or --scene scratch");
            test_particle_ribbon::execute(presentation, context);
        }
        context->Flush();
        context->WaitForIdle();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
