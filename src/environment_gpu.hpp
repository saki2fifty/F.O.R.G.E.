#pragma once
#include "gpu_residency.hpp"
#include "presentation_diligent.hpp"
namespace forge {
struct GpuEnvironment {
    Diligent::RefCntAutoPtr<Diligent::ITexture> source, diffuse, specular, sheen;
};
// A fixed policy per residency owner. This realizes the SAME TextureAsset CPU
// revision as reusable IBL resources; it introduces no persistent asset identity.
struct EnvironmentRealization {
    using Data = GpuEnvironment;
    DiligentPresentation* presentation{};
    unsigned diffuse_size = 64, specular_size = 256;
    unsigned diffuse_samples = 0, specular_samples = 0;
    std::uint64_t bytes(const TextureData&) const;
    Data upload(Diligent::IRenderDevice*, Diligent::IDeviceContext*, const TextureData&) const;
};
using EnvironmentResidency = GpuResidency<TextureAsset, EnvironmentRealization>;
using EnvironmentLease = GpuLease<TextureAsset, EnvironmentRealization>;
struct EnvironmentLighting {
    const GpuEnvironment* maps{};
    float intensity = 1;
    double rotation = 0; // Radians around world Y.
};
} // namespace forge
