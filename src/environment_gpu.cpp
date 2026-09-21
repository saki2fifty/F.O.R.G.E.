#include "environment_gpu.hpp"
#include <bit>
namespace forge {
namespace {
std::uint64_t cube_bytes(const Diligent::TextureDesc& desc) {
    std::uint64_t total = 0;
    const unsigned levels = desc.MipLevels ? desc.MipLevels : std::bit_width(desc.Width);
    for (unsigned mip = 0; mip < levels; ++mip) {
        const auto side = std::max(1u, desc.Width >> mip);
        total += std::uint64_t(side) * side * 6 * 8; // Native RGBA16F output.
    }
    return total;
}
} // namespace
std::uint64_t EnvironmentRealization::bytes(const TextureData& source) const {
    validate_texture(source);
    if (!presentation || !diffuse_size || !specular_size ||
        diffuse_size > presentation->device()->GetAdapterInfo().Texture.MaxTexture2DDimension ||
        specular_size > presentation->device()->GetAdapterInfo().Texture.MaxTexture2DDimension)
        throw std::runtime_error("Environment convolution size exceeds the device profile");
    if ((source.dimension != TextureDimension::Cube && source.dimension != TextureDimension::D2) ||
        (source.semantic != TextureSemantic::Color && source.semantic != TextureSemantic::HdrColor))
        throw std::runtime_error("Environment requires a color cube or equirectangular 2D texture");
    // Includes the source upload and all output mips. The inherited owner reserves
    // this before allocation and retains failed reservations until fence completion.
    using PBR = Diligent::PBR_Renderer;
    return source.byte_size() +
           cube_bytes(PBR::GetIrradianceCubeDesc("", PBR::IrradianceCubeFmt, diffuse_size)) +
           2 * cube_bytes(
                   PBR::GetPrefilteredEnvMapDesc("", PBR::PrefilteredEnvMapFmt, specular_size));
}
GpuEnvironment EnvironmentRealization::upload(Diligent::IRenderDevice* device,
                                              Diligent::IDeviceContext* context,
                                              const TextureData& source) const {
    (void)bytes(source);
    if (device != presentation->device())
        throw std::runtime_error("Environment convolution device differs from presentation owner");
    auto& pbr = presentation->pbr(context);
    GpuEnvironment candidate;
    candidate.source = upload_texture(device, source);
    candidate.diffuse = pbr.CreateIrradianceCube(context, "FORGE environment irradiance",
                                                 pbr.IrradianceCubeFmt, diffuse_size);
    candidate.specular = pbr.CreatePrefilteredEnvMap(context, "FORGE environment GGX",
                                                     pbr.PrefilteredEnvMapFmt, specular_size);
    candidate.sheen = pbr.CreatePrefilteredEnvMap(context, "FORGE environment Charlie",
                                                  pbr.PrefilteredEnvMapFmt, specular_size);
    if (!candidate.source || !candidate.diffuse || !candidate.specular || !candidate.sheen)
        throw std::runtime_error("Environment convolution allocation failed");
    Diligent::PBR_Renderer::PrecomputeCubemapsAttribs args;
    args.pEnvironmentMapSRV =
        candidate.source->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    args.pIrradianceCube = candidate.diffuse;
    args.pPrefilteredEnvMap = candidate.specular;
    args.pPrefilteredSheenEnvMap = candidate.sheen;
    args.NumDiffuseSamples = diffuse_samples;
    args.NumSpecularSamples = specular_samples;
    // Cooked rows begin at image top: positive Y for equirectangular input.
    args.SphereMapRow0IsNegativeY = false;
    pbr.PrecomputeCubemaps(context, args);
    return candidate;
}
} // namespace forge
