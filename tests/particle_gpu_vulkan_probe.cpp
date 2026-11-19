#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "engine_render_resource.hpp"
#include "mesh_draw.hpp"
#include "particle_gpu.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <forge/engine_assets.hpp>
#include <iostream>
#include <limits>
using namespace forge;
using namespace Diligent;
namespace {
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid GPU particle input accepted");
}
struct Random {
    std::uint32_t state;
    Random(std::uint32_t seed, std::uint64_t ordinal) {
        std::uint32_t hash = 2166136261u;
        for (unsigned byte = 0; byte < 12; ++byte) {
            const auto value = byte < 4 ? std::uint8_t(seed >> (byte * 8))
                                        : std::uint8_t(ordinal >> ((byte - 4) * 8));
            hash = (hash ^ value) * 16777619u;
        }
        state = hash % 2147483646u + 1u;
    }
    double unit() {
        state = std::uint32_t(std::uint64_t(state) * 48271u % 2147483647u);
        return double(state >> 7) / 16777216.;
    }
};
std::array<ParticleGpuRow, 7> reference(const ParticleGpuValues& p, unsigned id) {
    const auto ordinal = ((std::uint64_t(p.births[2]) << 32) | p.births[1]) + id;
    Random random(p.births[3], ordinal);
    const double c = 1 - random.unit() * (1 - p.axis[3]), s = std::sqrt(std::max(0., 1 - c * c)),
                 phi = random.unit() * 6.2831853071795864769;
    const double speed = p.side[3] + random.unit() * (p.up[3] - p.side[3]);
    std::array<double, 3> origin{}, initial{};
    for (unsigned i = 0; i < 3; ++i)
        initial[i] =
            speed * (p.axis[i] * c + p.side[i] * s * std::cos(phi) + p.up[i] * s * std::sin(phi));
    if (p.modes[0] == 1) {
        for (unsigned i = 0; i < 3; ++i)
            origin[i] = (random.unit() - .5) * p.shape[i];
    } else if (p.modes[0] == 2) {
        const double radius = p.shape[3] * std::cbrt(random.unit()), y = 2 * random.unit() - 1,
                     radial = std::sqrt(std::max(0., 1 - y * y)),
                     angle = random.unit() * 6.2831853071795864769;
        origin = {radius * radial * std::cos(angle), radius * y, radius * radial * std::sin(angle)};
    } else if (p.modes[0] == 3) {
        const double radius = p.shape[3] * std::sqrt(random.unit()),
                     angle = random.unit() * 6.2831853071795864769;
        for (unsigned i = 0; i < 3; ++i)
            origin[i] = radius * (p.side[i] * std::cos(angle) + p.up[i] * std::sin(angle));
    }
    const double age = std::max(0., double(p.timing[0]) - id * double(p.timing[1])),
                 u = std::clamp(age / p.timing[2], 0., 1.);
    double alpha = age, beta = .5 * age * age;
    if (p.timing[3] > 0) {
        alpha = -std::expm1(-double(p.timing[3]) * age) / p.timing[3];
        beta = (age - alpha) / p.timing[3];
    }
    double size = p.sizes[0] + (p.sizes[1] - p.sizes[0]) * u;
    if (p.modes[1]) {
        size = p.size_stops[p.modes[1] - 1][1];
        for (unsigned i = 1; i < p.modes[1]; ++i)
            if (u < p.size_stops[i][0]) {
                const auto& a = p.size_stops[i - 1];
                const auto& b = p.size_stops[i];
                size = a[2] != 0
                           ? a[1]
                           : a[1] + (b[1] - a[1]) * std::clamp((u - a[0]) / (b[0] - a[0]), 0., 1.);
                break;
            }
    }
    size *= p.acceleration[3];
    std::array<double, 3> color{};
    for (unsigned i = 0; i < 3; ++i)
        color[i] = p.colors[0][i] + (p.colors[1][i] - p.colors[0][i]) * u;
    if (p.modes[2]) {
        for (unsigned channel = 0; channel < 3; ++channel)
            color[channel] = p.color_stops[p.modes[2] - 1][channel + 1];
        for (unsigned i = 1; i < p.modes[2]; ++i)
            if (u < p.color_stops[i][0]) {
                const auto& a = p.color_stops[i - 1];
                const auto& b = p.color_stops[i];
                for (unsigned channel = 0; channel < 3; ++channel)
                    color[channel] =
                        (p.modes[3] & (1u << (i - 1)))
                            ? a[channel + 1]
                            : a[channel + 1] + (b[channel + 1] - a[channel + 1]) *
                                                   std::clamp((u - a[0]) / (b[0] - a[0]), 0., 1.);
                break;
            }
    }
    double maximum = 0;
    for (const auto& row : p.emitter)
        for (unsigned i = 0; i < 3; ++i)
            maximum = std::max(maximum, std::abs(double(row[i])));
    std::array<ParticleGpuRow, 7> rows{};
    for (unsigned row = 0; row < 3; ++row) {
        double position = p.emitter[row][3];
        for (unsigned i = 0; i < 3; ++i) {
            rows[row][i] = float(p.emitter[row][i] * size);
            rows[row + 3][i] = maximum > 0 ? float(p.emitter[row][i] / maximum) : 0;
            position +=
                p.emitter[row][i] * (origin[i] + initial[i] * alpha + p.acceleration[i] * beta);
        }
        rows[row][3] = float(position);
        rows[6][row] = float(color[row] * p.tint[row]);
    }
    rows[6][3] = 1;
    return rows;
}
constexpr const char* draw_kernel = R"(
RWStructuredBuffer<float4> ParticleRows;
[numthreads(64,1,1)]void main(uint3 thread:SV_DispatchThreadID){
    uint id=thread.x;
    float x=-.875+float(id%8u)*.25,y=-.875+float(id/8u)*.25;
    [unroll]for(uint copy=0;copy<2;copy++){
        uint row=(copy*64u+id)*7u;
        ParticleRows[row]=float4(.0625,0,0,x);
        ParticleRows[row+1u]=float4(0,.0625,0,y);
        ParticleRows[row+2u]=float4(0,0,.0625,.5);
        ParticleRows[row+3u]=float4(1,0,0,0);
        ParticleRows[row+4u]=float4(0,1,0,0);
        ParticleRows[row+5u]=float4(0,0,1,0);
        ParticleRows[row+6u]=float4(.25,1,.5,1);
    }
}
)";
std::vector<std::uint8_t> read(IRenderDevice* device, IDeviceContext* context, ITexture* color) {
    auto desc = color->GetDesc();
    desc.Name = "FORGE instance contract readback";
    desc.Usage = USAGE_STAGING;
    desc.BindFlags = BIND_NONE;
    desc.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<ITexture> staging;
    device->CreateTexture(desc, nullptr, &staging);
    require(bool(staging), "Color readback unavailable");
    context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    context->CopyTexture({color, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging,
                          RESOURCE_STATE_TRANSITION_MODE_TRANSITION});
    context->WaitForIdle();
    MappedTextureSubresource mapped;
    context->MapTextureSubresource(staging, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr, mapped);
    require(mapped.pData, "Color readback map failed");
    std::vector<std::uint8_t> pixels(desc.Width * desc.Height * 4);
    for (unsigned y = 0; y < desc.Height; ++y)
        std::memcpy(pixels.data() + y * desc.Width * 4,
                    static_cast<const std::byte*>(mapped.pData) + y * mapped.Stride,
                    desc.Width * 4);
    context->UnmapTextureSubresource(staging, 0, 0);
    return pixels;
}
void draw_instance_contract(IRenderDevice* device, IDeviceContext* context,
                            DiligentPresentation& presentation) {
    const auto resource = asset_detail::engine_mesh_resource(engine_primitive(0));
    const auto mesh = upload_mesh(device, resource.mesh);
    MaterialData material;
    material.model = "forge.gltf.unlit.v1";
    MeshDraw draw(presentation, context, mesh.lods.front().parts.front(), material, {},
                  TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT);
    require(draw.supports_instances(), "Native static mesh does not support instancing");
    ShaderCreateInfo shader_info;
    shader_info.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader_info.ShaderCompiler = SHADER_COMPILER_DXC;
    shader_info.EntryPoint = "main";
    shader_info.Desc.Name = "FORGE compute-written static instance probe";
    shader_info.Desc.ShaderType = SHADER_TYPE_COMPUTE;
    shader_info.Source = draw_kernel;
    RefCntAutoPtr<IShader> shader;
    presentation.shader(shader_info, &shader);
    require(bool(shader), "Compute shader unavailable");
    ComputePipelineStateCreateInfo info;
    info.PSODesc.Name = "FORGE compute-written static instance probe";
    info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    info.pCS = shader;
    RefCntAutoPtr<IPipelineState> compute;
    presentation.compute(info, &compute);
    require(bool(compute), "Compute pipeline unavailable");
    RefCntAutoPtr<IShaderResourceBinding> binding;
    compute->CreateShaderResourceBinding(&binding, true);
    require(bool(binding), "Compute resources unavailable");
    BufferDesc buffer;
    buffer.Name = "FORGE compute-written static instance rows";
    buffer.Size = 2 * 64 * 7 * 16;
    buffer.Mode = BUFFER_MODE_STRUCTURED;
    buffer.ElementByteStride = 16;
    buffer.BindFlags = BIND_VERTEX_BUFFER | BIND_UNORDERED_ACCESS;
    RefCntAutoPtr<IBuffer> rows;
    device->CreateBuffer(buffer, nullptr, &rows);
    require(bool(rows), "Instance output unavailable");
    auto* output = binding->GetVariableByName(SHADER_TYPE_COMPUTE, "ParticleRows");
    require(output, "Instance shader binding unavailable");
    output->Set(rows->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));
    context->SetPipelineState(compute);
    context->CommitShaderResources(binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->DispatchCompute(DispatchComputeAttribs{1, 1, 1});
    TextureDesc target;
    target.Name = "FORGE instance contract color";
    target.Type = RESOURCE_DIM_TEX_2D;
    target.Width = target.Height = 128;
    target.Format = TEX_FORMAT_RGBA8_UNORM;
    target.BindFlags = BIND_RENDER_TARGET;
    RefCntAutoPtr<ITexture> color;
    device->CreateTexture(target, nullptr, &color);
    target.Name = "FORGE instance contract depth";
    target.Format = TEX_FORMAT_D32_FLOAT;
    target.BindFlags = BIND_DEPTH_STENCIL;
    RefCntAutoPtr<ITexture> depth;
    device->CreateTexture(target, nullptr, &depth);
    require(color && depth, "Instance targets unavailable");
    CameraView camera{};
    camera.right = {1, 0, 0};
    camera.up = {0, 1, 0};
    camera.forward = {0, 0, 1};
    camera.projection = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    camera.viewport = {0, 0, 128, 128};
    AffineTransform world;
    std::vector<MeshDraw::Instance> cpu;
    for (unsigned id = 0; id < 64; ++id) {
        AffineTransform transform;
        transform.m = {.0625, 0,     0,     -.875 + double(id % 8) * .25,
                       0,     .0625, 0,     -.875 + double(id / 8) * .25,
                       0,     0,     .0625, .5};
        cpu.push_back(
            {transform, std::array<float, 3>{.25f, 1, .5f}, nullptr, nullptr, nullptr, nullptr});
    }
    const auto submit = [&](const MeshDraw::InstanceBuffer* external,
                            std::span<const MeshDraw::Instance> values) {
        auto* view = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        auto* depth_view = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        context->SetRenderTargets(1, &view, depth_view, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float black[4]{0, 0, 0, 1};
        context->ClearRenderTarget(view, black, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(depth_view, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport viewport{0, 0, 128, 128, 0, 1};
        context->SetViewports(1, &viewport, 128, 128);
        const auto leader = values.empty() ? world : values.front().world;
        draw.draw(context, leader, camera, {}, nullptr, nullptr, nullptr, {}, nullptr, {}, nullptr,
                  values, {}, {}, nullptr, nullptr, nullptr, {}, nullptr, {}, {}, nullptr, {},
                  nullptr, external);
    };
    submit(nullptr, cpu);
    const auto reference = read(device, context, color);
    unsigned visible = 0;
    for (std::size_t i = 0; i < reference.size(); i += 4)
        if (reference[i] + reference[i + 1] + reference[i + 2] > 12)
            ++visible;
    require(visible > 512, "CPU reference mesh grid is not visible");
    for (std::uint64_t offset : {0u, 64u * 7u * 16u}) {
        const MeshDraw::InstanceBuffer packet{rows, offset, 64};
        submit(&packet, {});
        require(read(device, context, color) == reference,
                "Compute-written rows differ from CPU instance pixels");
    }
    for (const MeshDraw::InstanceBuffer packet :
         {MeshDraw::InstanceBuffer{nullptr, 0, 64}, MeshDraw::InstanceBuffer{rows, 0, 0},
          MeshDraw::InstanceBuffer{rows, 0, 65}, MeshDraw::InstanceBuffer{rows, 1, 64},
          MeshDraw::InstanceBuffer{rows, buffer.Size, 1}})
        rejects([&] { submit(&packet, {}); });
    const MeshDraw::InstanceBuffer packet{rows, 0, 64};
    rejects([&] { submit(&packet, cpu); });
    context->WaitForIdle();
    std::cout << "GPU_DRAW_CONTRACT {\"instances\":64,\"exact_display_pairs\":2,\"visible_pixels\":"
              << visible << ",\"invalid_packets_rejected\":6,\"backend\":\"software Vulkan\"}\n";
}
} // namespace
int main() {
    try {
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo create;
        create.Features.ComputeShaders = DEVICE_FEATURE_STATE_ENABLED;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        require(device && context, "Native compute unavailable");
        DiligentPresentation presentation(device);
        ParticleGpuInstances particles(presentation, 1024);
        require(particles.supported(), "Native compute not enabled");
        BufferDesc desc;
        desc.Name = "FORGE analytic particle contract readback";
        desc.Size = 1024 * 7 * sizeof(ParticleGpuRow);
        desc.Usage = USAGE_STAGING;
        desc.CPUAccessFlags = CPU_ACCESS_READ;
        RefCntAutoPtr<IBuffer> staging;
        device->CreateBuffer(desc, nullptr, &staging);
        require(bool(staging), "Analytic readback unavailable");
        ParticleEffectData data;
        data.version = 3;
        data.mesh = engine_primitive(0);
        data.capacity = 1024;
        data.burst_count = 128;
        data.spawn_rate = 24;
        data.lifetime = 3;
        data.direction = {1, 2, -2};
        data.shape_size = {4, 7, 3};
        data.shape_radius = 2;
        AffineTransform world;
        world.m = {-2, .3, 0, 10, 0, 3, .4, -2, 0, 0, .5, 7};
        CameraView camera{};
        camera.position = {10, 5, 7};
        std::uint64_t cases = 0, compared = 0;
        double worst_error = 0;
        for (bool curves : {false, true})
            for (auto shape : {ParticleSpawnShape::Point, ParticleSpawnShape::Box,
                               ParticleSpawnShape::Sphere, ParticleSpawnShape::Disc})
                for (double drag : {0., 1e-8, .001, .01, .1, .7})
                    for (auto emission : {ParticleEmission::Burst, ParticleEmission::Continuous})
                        for (auto seed : {0u, 0xffffffffu})
                            for (double time :
                                 {0., .5, 1.2, 1.5, std::nextafter(3., 0.), 3.5, 1000000000.}) {
                                data.shape = shape;
                                data.drag = drag;
                                data.emission = emission;
                                data.size_curve =
                                    curves ? std::vector<ParticleSizeStop>{{0, .1, false},
                                                                           {.4, 3, true},
                                                                           {1, .2, false}}
                                           : std::vector<ParticleSizeStop>{};
                                data.color_gradient =
                                    curves ? std::vector<ParticleColorStop>{{0, {1, 0, 0}, false},
                                                                            {.5, {0, 1, 0}, true},
                                                                            {1, {0, 0, 1}, false}}
                                           : std::vector<ParticleColorStop>{};
                                const auto plan = prepare_particle_gpu_values(
                                    data, time, seed, world, camera, 1.5, {1, .8f, .6f});
                                auto cpu = data;
                                cpu.version = 2;
                                const auto membership = sample_particle_effect(cpu, time, seed);
                                require(plan.births[0] == membership.size(),
                                        "GPU logical membership differs from authoritative double "
                                        "boundaries");
                                particles.evaluate(context, plan);
                                require(particles.count() == plan.births[0],
                                        "GPU publication changed its instance count");
                                ++cases;
                                if (!particles.count())
                                    continue;
                                const auto bytes = particles.count() * 7 * sizeof(ParticleGpuRow);
                                context->CopyBuffer(particles.buffer(), 0,
                                                    RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                                    staging, 0, bytes,
                                                    RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                                context->WaitForIdle();
                                {
                                    MapHelper<ParticleGpuRow> mapped(context, staging, MAP_READ,
                                                                     MAP_FLAG_DO_NOT_WAIT);
                                    require(static_cast<ParticleGpuRow*>(mapped),
                                            "Analytic particle map unavailable");
                                    for (unsigned id = 0; id < particles.count(); ++id) {
                                        const auto expected = reference(plan, id);
                                        for (unsigned row = 0; row < 7; ++row)
                                            for (unsigned column = 0; column < 4; ++column) {
                                                const double error =
                                                    std::abs(double(mapped[id * 7 + row][column]) -
                                                             expected[row][column]);
                                                worst_error = std::max(worst_error, error);
                                                if (!std::isfinite(mapped[id * 7 + row][column]) ||
                                                    error >
                                                        3e-4 + 3e-5 * std::abs(double(
                                                                          expected[row][column])))
                                                    throw std::runtime_error(
                                                        "Analytic GPU mismatch case=" +
                                                        std::to_string(cases) +
                                                        " particle=" + std::to_string(id) +
                                                        " row=" + std::to_string(row) +
                                                        " column=" + std::to_string(column) +
                                                        " error=" + std::to_string(error));
                                            }
                                        ++compared;
                                    }
                                }
                            }
        rejects([&] { ParticleGpuInstances invalid(presentation, 0); });
        rejects([&] { ParticleGpuInstances invalid(presentation, 1025); });
        auto bad = prepare_particle_gpu_values(data, .5, 1, world, camera);
        bad.births[0] = 1025;
        rejects([&] { particles.evaluate(context, bad); });
        require(particles.count() == 0, "Failed compute retained a published prior count");
        bad.births[0] = 1;
        bad.modes[1] = 17;
        rejects([&] { particles.evaluate(context, bad); });
        data.size_curve = {
            {0, .1, false}, {.5, .2, false}, {std::nextafter(.5, 1.), .3, false}, {1, .1, false}};
        rejects([&] { (void)prepare_particle_gpu_values(data, .5, 1, world, camera); });
        draw_instance_contract(device, context, presentation);
        context->WaitForIdle();
        std::cout << "GPU_ANALYTIC_CONTRACT {\"cases\":" << cases << ",\"particles\":" << compared
                  << ",\"maximum_absolute_error\":" << worst_error
                  << ",\"backend\":\"software Vulkan\"}\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
