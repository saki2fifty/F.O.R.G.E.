#include "render_backend.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#if defined(_WIN32)
#include <d3d12.h>
#endif
#if defined(FORGE_VULKAN_LIMIT_QUERY)
#define VK_NO_PROTOTYPES
// Native interface declarations require Vulkan types first.
// clang-format off
#include <vulkan/vulkan.h>
#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngineVulkan/interface/RenderDeviceVk.h"
#include <dlfcn.h>
// clang-format on
#endif
namespace forge {
using namespace Diligent;
SamplerBackendLimits sampler_backend_limits(IRenderDevice* device) {
    if (!device)
        throw std::runtime_error("Sampler capability query requires a render device");
#if defined(_WIN32)
    if (device->GetDeviceInfo().Type == RENDER_DEVICE_TYPE_D3D12)
        return {D3D12_MIP_LOD_BIAS_MIN, D3D12_MIP_LOD_BIAS_MAX, SamplerBorderProfile::UnitRange};
#endif
#if defined(FORGE_VULKAN_LIMIT_QUERY)
    if (device->GetDeviceInfo().Type == RENDER_DEVICE_TYPE_VULKAN) {
        // Read-only capability exception: no native resource or handle escapes
        // this adapter. Avoid Vulkan/volk global-symbol collisions by resolving
        // through the loader's instance entry point.
        struct Loader {
            void* handle = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
            ~Loader() {
                if (handle)
                    dlclose(handle);
            }
        } loader;
        if (!loader.handle)
            throw std::runtime_error("Vulkan sampler limit query cannot open the Vulkan loader");
        const auto get_proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            dlsym(loader.handle, "vkGetInstanceProcAddr"));
        RefCntAutoPtr<IRenderDeviceVk> native{device, IID_RenderDeviceVk};
        if (!get_proc || !native)
            throw std::runtime_error("Vulkan sampler limit query is unavailable");
        const auto query = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
            get_proc(native->GetVkInstance(), "vkGetPhysicalDeviceProperties"));
        if (!query)
            throw std::runtime_error("Vulkan physical device properties query is unavailable");
        VkPhysicalDeviceProperties properties{};
        query(native->GetVkPhysicalDevice(), &properties);
        const auto bias = properties.limits.maxSamplerLodBias;
        if (!std::isfinite(bias) || bias < 0)
            throw std::runtime_error("Vulkan reported an invalid sampler LOD bias limit");
        return {-bias, bias, SamplerBorderProfile::BlackOrWhite};
    }
#endif
    // Other hosts need their own exact-source proof before these optional
    // states can be enabled. Default zero-bias/no-border samplers remain valid.
    return {};
}
void prepare_renderer_sampler(const SamplerProperties& properties,
                              const SamplerBackendLimits& limits, SamplerDesc& desc) {
    const auto require = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    require(desc.MaxAnisotropy >= 1 && desc.MaxAnisotropy <= properties.MaxAnisotropy,
            "Sampler anisotropy exceeds this device's capability");
    require(std::isfinite(desc.MipLODBias) &&
                (desc.MipLODBias == 0 || properties.LODBiasSupported) &&
                desc.MipLODBias >= limits.minimum_lod_bias &&
                desc.MipLODBias <= limits.maximum_lod_bias,
            "Sampler LOD bias is outside this backend's validated device range");
    require(std::isfinite(desc.MinLOD) && std::isfinite(desc.MaxLOD) && desc.MaxLOD >= desc.MinLOD,
            "Invalid sampler LOD range");
    const bool border = desc.AddressU == TEXTURE_ADDRESS_BORDER ||
                        desc.AddressV == TEXTURE_ADDRESS_BORDER ||
                        desc.AddressW == TEXTURE_ADDRESS_BORDER;
    if (!border) {
        // An unused border has no sampling effect. Normalize only this native
        // descriptor; preserve the complete authored sampler state.
        std::fill(std::begin(desc.BorderColor), std::end(desc.BorderColor), 0.f);
        return;
    }
    require(properties.BorderSamplingModeSupported &&
                limits.border != SamplerBorderProfile::Unavailable,
            "This backend has no validated border-sampling support");
    for (const auto value : desc.BorderColor)
        require(std::isfinite(value) && value >= 0 && value <= 1,
                "Sampler border color is outside this backend's supported range");
    if (limits.border == SamplerBorderProfile::BlackOrWhite) {
        const auto* c = desc.BorderColor;
        require((c[0] == 0 && c[1] == 0 && c[2] == 0 && (c[3] == 0 || c[3] == 1)) ||
                    (c[0] == 1 && c[1] == 1 && c[2] == 1 && c[3] == 1),
                "Pinned Vulkan samplers require transparent black, opaque black or opaque white "
                "borders");
    }
}
} // namespace forge
