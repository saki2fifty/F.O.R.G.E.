#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "texture_bc6.hpp"
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>
using namespace Diligent;
namespace {
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
constexpr auto shader_source = R"(
Texture2D<float4> Encoded;
RWStructuredBuffer<uint4> Result;
[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID) {
    uint width,height; Encoded.GetDimensions(width,height);
    if(id.x<width&&id.y<height)
        Result[id.y*width+id.x]=f32tof16(Encoded.Load(int3(id.xy,0)));
}
)";
using Block = std::array<std::byte, 16>;
std::vector<Block> blocks() {
    std::mt19937 random(0x6b6311u);
    std::vector<Block> result;
    for (unsigned code :
         {0u, 1u, 2u, 6u, 10u, 14u, 18u, 22u, 26u, 30u, 3u, 7u, 11u, 15u, 19u, 23u, 27u, 31u})
        for (unsigned partition = 0; partition < 32; ++partition)
            for (unsigned variant = 0; variant < 16; ++variant) {
                Block block;
                for (auto& byte : block)
                    byte = std::byte(random() & 255);
                const unsigned mask = code < 2 ? 3u : 31u;
                block[0] = std::byte((std::to_integer<unsigned>(block[0]) & ~mask) | code);
                if ((code & 3) != 3)
                    for (unsigned bit = 0; bit < 5; ++bit) {
                        const unsigned at = 77 + bit;
                        const unsigned value = std::to_integer<unsigned>(block[at / 8]);
                        block[at / 8] = std::byte((value & ~(1u << (at % 8))) |
                                                  (((partition >> bit) & 1u) << (at % 8)));
                    }
                result.push_back(block);
            }
    // Independently assembled single-subset extremes. These include signed
    // negative infinity and negative zero, which random endpoints rarely hit.
    const Block negative_infinity{std::byte(15),  std::byte(0), std::byte(0), std::byte(0),
                                  std::byte(128), std::byte(0), std::byte(2), std::byte(8)};
    const Block negative_zero{std::byte(239), std::byte(255), std::byte(255),
                              std::byte(255), std::byte(135), std::byte(31),
                              std::byte(126), std::byte(248), std::byte(1)};
    for (unsigned i = 0; i < 128; ++i)
        result.push_back(i % 2 ? negative_zero : negative_infinity);
    return result;
}
void compare(IRenderDevice* device, IDeviceContext* context, IShader* shader,
             const std::vector<Block>& source, bool sign, const std::filesystem::path& root) {
    constexpr unsigned columns = 128;
    const unsigned width = columns * 4, height = unsigned(source.size() / columns) * 4;
    check(source.size() % columns == 0, "Oracle block grid has a partial row");
    TextureDesc texture;
    texture.Name = "FORGE BC6 independent native decoder oracle";
    texture.Type = RESOURCE_DIM_TEX_2D;
    texture.Width = width;
    texture.Height = height;
    texture.Format = sign ? TEX_FORMAT_BC6H_SF16 : TEX_FORMAT_BC6H_UF16;
    texture.Usage = USAGE_IMMUTABLE;
    texture.BindFlags = BIND_SHADER_RESOURCE;
    TextureSubResData plane{source.data(), columns * 16};
    TextureData initial{&plane, 1};
    RefCntAutoPtr<ITexture> encoded;
    device->CreateTexture(texture, &initial, &encoded);
    check(bool(encoded), "BC6 oracle format is unavailable on this Vulkan adapter");
    BufferDesc desc;
    desc.Name = "FORGE BC6 oracle half words";
    desc.Size = std::uint64_t(width) * height * 16;
    desc.Mode = BUFFER_MODE_STRUCTURED;
    desc.ElementByteStride = 16;
    desc.BindFlags = BIND_UNORDERED_ACCESS;
    RefCntAutoPtr<IBuffer> output, staging;
    device->CreateBuffer(desc, nullptr, &output);
    desc.Name = "FORGE BC6 oracle staging";
    desc.Mode = BUFFER_MODE_UNDEFINED;
    desc.ElementByteStride = 0;
    desc.BindFlags = BIND_NONE;
    desc.Usage = USAGE_STAGING;
    desc.CPUAccessFlags = CPU_ACCESS_READ;
    device->CreateBuffer(desc, nullptr, &staging);
    check(output && staging, "BC6 oracle buffers unavailable");
    ComputePipelineStateCreateInfo info;
    info.PSODesc.Name = "FORGE BC6 texture load to binary16 oracle";
    info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    info.pCS = shader;
    RefCntAutoPtr<IPipelineState> pipeline;
    device->CreateComputePipelineState(info, &pipeline);
    check(bool(pipeline), "BC6 oracle compute pipeline unavailable");
    RefCntAutoPtr<IShaderResourceBinding> bindings;
    pipeline->CreateShaderResourceBinding(&bindings, true);
    auto* input = bindings->GetVariableByName(SHADER_TYPE_COMPUTE, "Encoded");
    auto* result = bindings->GetVariableByName(SHADER_TYPE_COMPUTE, "Result");
    check(input && result, "BC6 oracle resource reflection unavailable");
    input->Set(encoded->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    result->Set(output->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));
    context->SetPipelineState(pipeline);
    context->CommitShaderResources(bindings, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->DispatchCompute(DispatchComputeAttribs{(width + 7) / 8, (height + 7) / 8, 1});
    context->CopyBuffer(output, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging, 0, desc.Size,
                        RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->WaitForIdle();
    std::vector<std::uint32_t> observed(std::size_t(width) * height * 4);
    void* mapped = nullptr;
    context->MapBuffer(staging, MAP_READ, MAP_FLAG_DO_NOT_WAIT, mapped);
    check(mapped != nullptr, "BC6 oracle staging map unavailable");
    std::memcpy(observed.data(), mapped, observed.size() * sizeof(std::uint32_t));
    context->UnmapBuffer(staging, MAP_READ);
    context->FinishFrame();
    std::ofstream mismatch(root / (sign ? "signed-mismatches.txt" : "unsigned-mismatches.txt"));
    std::size_t errors = 0;
    for (std::size_t b = 0; b < source.size(); ++b) {
        const auto expected = forge::asset_detail::decode_bc6_block(source[b], sign);
        for (unsigned pixel = 0; pixel < 16; ++pixel) {
            const auto x = (b % columns) * 4 + pixel % 4, y = (b / columns) * 4 + pixel / 4;
            for (unsigned c = 0; c < 4; ++c) {
                const unsigned wanted = c == 3 ? 0x3c00 : expected[pixel * 3 + c];
                const unsigned actual = observed[(y * width + x) * 4 + c];
                if (wanted == actual)
                    continue;
                if (errors++ < 24) {
                    mismatch << "block=" << b << " pixel=" << pixel << " channel=" << c
                             << " expected=" << std::hex << wanted << " observed=" << actual
                             << " bytes=";
                    for (auto byte : source[b])
                        mismatch << std::setw(2) << std::setfill('0')
                                 << std::to_integer<unsigned>(byte);
                    mismatch << std::dec << '\n';
                }
            }
        }
    }
    std::cout << (sign ? "signed" : "unsigned") << " BC6 blocks=" << source.size()
              << " texels=" << std::uint64_t(width) * height << " mismatches=" << errors << '\n';
    check(errors == 0, "BC6 core differs from actual native texture-load binary16 values");
}
void run(const std::filesystem::path& root) {
    std::ifstream stream(root / "decode.spv", std::ios::binary);
    std::vector<char> code{std::istreambuf_iterator<char>(stream), {}};
    check(!code.empty() && code.size() % 4 == 0, "BC6 oracle SPIR-V unavailable");
    RefCntAutoPtr<IRenderDevice> device;
    RefCntAutoPtr<IDeviceContext> context;
    EngineVkCreateInfo info;
    GetEngineFactoryVk()->CreateDeviceAndContextsVk(info, &device, &context);
    check(device && context, "BC6 oracle Vulkan device unavailable");
    std::cout << "BC6 oracle adapter: " << device->GetAdapterInfo().Description << '\n';
    ShaderCreateInfo shader;
    shader.Desc.Name = "FORGE BC6 binary16 native sampler oracle";
    shader.Desc.ShaderType = SHADER_TYPE_COMPUTE;
    shader.EntryPoint = "main";
    shader.ByteCode = code.data();
    shader.ByteCodeSize = code.size();
    RefCntAutoPtr<IShader> compute;
    device->CreateShader(shader, &compute);
    check(bool(compute), "BC6 oracle shader unavailable");
    const auto source = blocks();
    compare(device, context, compute, source, false, root);
    compare(device, context, compute, source, true, root);
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Usage: BC6 oracle emit|draw directory");
        const auto root = std::filesystem::u8path(argv[2]);
        if (std::string_view(argv[1]) == "emit") {
            std::ofstream file(root / "decode.hlsl");
            file << shader_source;
            check(bool(file), "BC6 oracle shader write failed");
        } else if (std::string_view(argv[1]) == "draw")
            run(root);
        else
            throw std::runtime_error("Unknown BC6 oracle operation");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
