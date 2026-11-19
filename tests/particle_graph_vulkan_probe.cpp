#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"
#include "particle_gpu.hpp"
#include "particle_gpu_reference.hpp"
#include "particle_graph_shader.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <tuple>
using namespace forge;
using namespace Diligent;
namespace {
using Json = nlohmann::json;
void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid graph device input accepted");
}
struct Program {
    Json graph = create_particle_graph();
    Json node(const char* type, const char* value_type = nullptr) {
        auto n = create_material_graph_node(type, {0, 0});
        if (value_type)
            n["data"]["type"] = value_type;
        graph["nodes"].push_back(n);
        return n;
    }
    void connect(const Json& from, const Json& to, const char* port) {
        graph["edges"].push_back({{"id", GraphEdgeId::generate()},
                                  {"from", {{"node", from.at("id")}, {"port", "out"}}},
                                  {"to", {{"node", to.at("id")}, {"port", port}}}});
    }
};
Program program(bool overflow) {
    Program p;
    const auto out = p.graph["nodes"][0];
    if (overflow) {
        for (const auto& [type, port, width] : {std::tuple{"vector3", "position", 3},
                                                {"scalar", "size", 1},
                                                {"color3", "color", 3}}) {
            auto constant = p.node("constant", type);
            constant["data"]["value"] = Json::array();
            for (int i = 0; i < width; ++i)
                constant["data"]["value"].push_back(3e38);
            p.graph["nodes"].back() = constant;
            const auto add = p.node("add", type);
            p.connect(constant, add, "a");
            p.connect(constant, add, "b");
            p.connect(add, out, port);
        }
        return p;
    }
    const auto position = p.node("particle_position"), origin = p.node("particle_origin"),
               velocity = p.node("particle_velocity"), age = p.node("particle_age"),
               age01 = p.node("particle_age01"), size = p.node("particle_size"),
               color = p.node("particle_color"), random = p.node("particle_random"),
               sine = p.node("sine"), wave = p.node("add"), convert = p.node("convert", "vector3");
    p.graph["nodes"].back()["data"]["from"] = "scalar";
    const auto a = p.node("add", "vector3"), b = p.node("add", "vector3"),
               c = p.node("add", "vector3"), scale = p.node("one_minus"),
               sized = p.node("multiply");
    p.connect(age, sine, "value");
    p.connect(sine, wave, "a");
    p.connect(random, wave, "b");
    p.connect(wave, convert, "value");
    p.connect(position, a, "a");
    p.connect(origin, a, "b");
    p.connect(velocity, b, "a");
    p.connect(convert, b, "b");
    p.connect(a, c, "a");
    p.connect(b, c, "b");
    p.connect(c, out, "position");
    p.connect(age01, scale, "value");
    p.connect(scale, sized, "a");
    p.connect(size, sized, "b");
    p.connect(sized, out, "size");
    p.connect(color, out, "color");
    return p;
}
void compare(DiligentPresentation& presentation, IDeviceContext* context, const Program& p,
             bool overflow) {
    const auto source = particle_graph_compute_source(p.graph);
    ShaderCreateInfo shader;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.ShaderCompiler = SHADER_COMPILER_DXC;
    shader.EntryPoint = "main";
    shader.Desc.Name = "FORGE generated particle graph code probe";
    shader.Desc.ShaderType = SHADER_TYPE_COMPUTE;
    shader.Source = source.c_str();
    shader.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();
    RefCntAutoPtr<IShader> native;
    // Generated-code acceptance only; product runtime still realizes cooked DXBC.
    presentation.shader(shader, &native);
    check(bool(native), "Generated particle graph did not compile on the native device");
    const ParticleGpuGraphLimits limits{{.4f, .6f, .8f, .15f}};
    ParticleGpuInstances instances(presentation, 64, native, limits);
    ParticleGpuValues values;
    values.births = {64, 0xfffffff0u, 2, 173};
    values.modes[0] = 1; // Box: nonzero origin proves the graph context is per birth.
    values.timing = {.8f, .01f, 2, .2f};
    values.axis = {0, 1, 0, 1};
    values.side = {1, 0, 0, 1};
    values.up = {0, 0, 1, 1};
    values.acceleration = {0, -2, 0, 2};
    values.shape = {1, 2, 3, 0};
    values.sizes = {.1f, .2f, 0, 0};
    values.tint = {.5f, 1, 2, 0};
    values.colors = {ParticleGpuRow{.7f, .8f, .9f, 0}, ParticleGpuRow{.1f, .2f, .3f, 0}};
    values.emitter = {ParticleGpuRow{-2, .3f, 0, 1}, ParticleGpuRow{0, 3, .4f, -2},
                      ParticleGpuRow{0, 0, .5f, 3}};
    instances.evaluate(context, values);
    check(instances.count() == 64, "Particle graph did not publish the current submission");
    BufferDesc desc;
    desc.Name = "FORGE particle graph output readback";
    desc.Size = 64 * 7 * sizeof(ParticleGpuRow);
    desc.Usage = USAGE_STAGING;
    desc.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<IBuffer> staging;
    presentation.device()->CreateBuffer(desc, nullptr, &staging);
    check(bool(staging), "Graph readback allocation failed");
    context->CopyBuffer(instances.buffer(), 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging,
                        0, desc.Size, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->WaitForIdle();
    {
        MapHelper<ParticleGpuRow> mapped(context, staging, MAP_READ, MAP_FLAG_DO_NOT_WAIT);
        check(static_cast<ParticleGpuRow*>(mapped), "Graph readback mapping failed");
        for (unsigned id = 0; id < 64; ++id) {
            const auto ordinal = ((std::uint64_t(values.births[2]) << 32) | values.births[1]) + id;
            particle_gpu_test::Random random(values.births[3], ordinal);
            (void)random.unit();
            (void)random.unit();
            (void)random.unit();
            std::array<double, 3> origin;
            for (unsigned i = 0; i < 3; ++i)
                origin[i] = (random.unit() - .5) * values.shape[i];
            const auto birth_random = random.unit();
            const double age =
                             std::max(0., double(values.timing[0]) - id * double(values.timing[1])),
                         drag = values.timing[3], alpha = -std::expm1(-drag * age) / drag,
                         beta = (age - alpha) / drag, u = age / values.timing[2];
            std::array<double, 3> position;
            for (unsigned i = 0; i < 3; ++i) {
                const double initial = i == 1 ? 1 : 0,
                             base = origin[i] + initial * alpha + values.acceleration[i] * beta,
                             velocity =
                                 initial * std::exp(-drag * age) + values.acceleration[i] * alpha;
                position[i] = std::clamp(
                    overflow ? base : base + origin[i] + velocity + std::sin(age) + birth_random,
                    -double(limits.extent_size[i]), double(limits.extent_size[i]));
            }
            const auto base_size = values.sizes[0] + (values.sizes[1] - values.sizes[0]) * u;
            const auto size = std::clamp(overflow ? base_size : base_size * (1 - u), 0.,
                                         double(limits.extent_size[3])) *
                              values.acceleration[3];
            for (unsigned row = 0; row < 3; ++row) {
                double translation = values.emitter[row][3];
                for (unsigned column = 0; column < 3; ++column) {
                    translation += values.emitter[row][column] * position[column];
                    check(std::abs(mapped[id * 7 + row][column] -
                                   values.emitter[row][column] * size) < 2e-5,
                          "Graph size output differs from the independent reference");
                }
                check(std::abs(mapped[id * 7 + row][3] - translation) < 4e-5,
                      "Graph position/context/bounds differ from the independent reference");
                const auto color =
                    (values.colors[0][row] + (values.colors[1][row] - values.colors[0][row]) * u) *
                    values.tint[row];
                check(std::abs(mapped[id * 7 + 6][row] - color) < 2e-5,
                      "Graph color output or nonfinite fallback differs");
            }
            for (unsigned row = 0; row < 7; ++row)
                for (float value : mapped[id * 7 + row])
                    check(std::isfinite(value),
                          "Particle graph published a nonfinite instance row");
        }
    }
    auto bad = values;
    bad.emitter[0][0] = std::numeric_limits<float>::max();
    rejects([&] { instances.evaluate(context, bad); });
    check(instances.count() == 0, "Rejected graph envelope retained an old submission count");
    auto invalid_limits = limits;
    invalid_limits.extent_size[0] = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { ParticleGpuInstances invalid(presentation, 64, native, invalid_limits); });
    rejects([&] { ParticleGpuInstances invalid(presentation, 64, nullptr, limits); });
}
} // namespace
int main() {
    try {
        EngineVkCreateInfo create;
        create.Features.ComputeShaders = DEVICE_FEATURE_STATE_ENABLED;
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        check(device && context, "Graph native compute device unavailable");
        DiligentPresentation presentation(device);
        for (bool overflow : {false, true})
            compare(presentation, context, program(overflow), overflow);
        context->WaitForIdle();
        std::cout << "PARTICLE_GRAPH_NATIVE cases=2 particles=128 backend=software-Vulkan "
                     "generated-code-only\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
