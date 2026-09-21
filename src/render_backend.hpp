#pragma once
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
namespace forge {
// Compiler/declaration policy is the only backend-dependent part of shared
// shader preparation. Resources, signatures and synchronization remain Diligent.
bool emulated_resource_arrays(const Diligent::RenderDeviceInfo&);
void prepare_renderer_shader(const Diligent::RenderDeviceInfo&, Diligent::ShaderCreateInfo&);
enum class SamplerBorderProfile { Unavailable, UnitRange, BlackOrWhite };
struct SamplerBackendLimits {
    float minimum_lod_bias = 0, maximum_lod_bias = 0;
    SamplerBorderProfile border = SamplerBorderProfile::Unavailable;
};
// Pinned Diligent exposes anisotropy/support flags, but not numeric bias limits
// or the Vulkan border palette. Keep these exceptions at the backend boundary.
SamplerBackendLimits sampler_backend_limits(Diligent::IRenderDevice*);
void prepare_renderer_sampler(const Diligent::SamplerProperties&, const SamplerBackendLimits&,
                              Diligent::SamplerDesc&);
} // namespace forge
