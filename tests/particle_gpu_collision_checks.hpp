#pragma once
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "gpu_readback_diagnostics.hpp"
#include "particle_gpu_camera_reference.hpp"
#include "particle_gpu_collision_shaders.hpp"
#include "particle_gpu_reference.hpp"
#include <forge/engine_assets.hpp>
#include <iostream>
#include <limits>
namespace forge::particle_plane_test {
using namespace Diligent;
inline void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
struct alignas(16) Input {
    std::array<float, 4> origin{}, velocity{}, force{};
};
inline void check_boundaries(IRenderDevice* device, IDeviceContext* context,
                             DiligentPresentation& presentation) {
    ShaderCreateInfo shader;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.EntryPoint = "main";
    shader.Desc.Name = "FORGE isolated plane motion proof";
    shader.Desc.ShaderType = SHADER_TYPE_COMPUTE;
    const std::string source = std::string(particle_gpu_plane_math) + R"(
cbuffer PlaneInput {float4 OriginAge,VelocityDrag,Force;};
RWStructuredBuffer<float4> Result;
[numthreads(1,1,1)]void main(uint3 id:SV_DispatchThreadID){
    Result[0]=CollidedPosition(OriginAge.xyz,VelocityDrag.xyz,Force.xyz,VelocityDrag.w,OriginAge.w);
    Result[1]=Result[2]=float4(0,0,0,0);
})";
    shader.Source = source.c_str();
    RefCntAutoPtr<IShader> native;
    presentation.shader(shader, &native);
    check(bool(native), "Plane shader unavailable");
    ComputePipelineStateCreateInfo info;
    info.PSODesc.Name = "FORGE isolated plane motion proof";
    info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    info.pCS = native;
    RefCntAutoPtr<IPipelineState> pipeline;
    presentation.compute(info, &pipeline);
    check(bool(pipeline), "Plane pipeline unavailable");
    RefCntAutoPtr<IShaderResourceBinding> binding;
    pipeline->CreateShaderResourceBinding(&binding, true);
    BufferDesc desc;
    desc.Name = "FORGE isolated plane parameters";
    desc.Size = sizeof(Input);
    desc.Usage = USAGE_DYNAMIC;
    desc.BindFlags = BIND_UNIFORM_BUFFER;
    desc.CPUAccessFlags = CPU_ACCESS_WRITE;
    RefCntAutoPtr<IBuffer> parameters;
    device->CreateBuffer(desc, nullptr, &parameters);
    check(bool(parameters), "Plane parameters unavailable");
    binding->GetVariableByName(SHADER_TYPE_COMPUTE, "PlaneInput")->Set(parameters);
    RefCntAutoPtr<IBuffer> plane_values;
    desc.Size = sizeof(ParticleGpuCollisionValues);
    device->CreateBuffer(desc, nullptr, &plane_values);
    check(bool(plane_values), "Boundary plane uniform missing");
    binding->GetVariableByName(SHADER_TYPE_COMPUTE, "PlaneCollisionValues")->Set(plane_values);

    desc.Name = "FORGE isolated plane output";
    desc.Size = 48;
    desc.Usage = USAGE_DEFAULT;
    desc.BindFlags = BIND_UNORDERED_ACCESS;
    desc.CPUAccessFlags = CPU_ACCESS_NONE;
    desc.Mode = BUFFER_MODE_STRUCTURED;
    desc.ElementByteStride = 16;
    RefCntAutoPtr<IBuffer> output, staging;
    device->CreateBuffer(desc, nullptr, &output);
    check(bool(output), "Plane output unavailable");
    binding->GetVariableByName(SHADER_TYPE_COMPUTE, "Result")
        ->Set(output->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));
    desc.Name = "FORGE isolated plane test readback";
    desc.Usage = USAGE_STAGING;
    desc.BindFlags = BIND_NONE;
    desc.CPUAccessFlags = CPU_ACCESS_READ;
    desc.Mode = BUFFER_MODE_UNDEFINED;
    desc.ElementByteStride = 0;
    device->CreateBuffer(desc, nullptr, &staging);
    check(bool(staging), "Plane test readback unavailable");
    unsigned cases = 0;
    for (unsigned orientation : {0u, 1u, 2u})
        for (float drag : {0.f, 4.f, 64.f})
            for (unsigned limit : {0u, 1u, 4u, 16u})
                for (float restitution : {0.f, .5f, 1.f})
                    for (float age :
                         {0.f, std::nextafter(.5f, 0.f), .5f, std::nextafter(.5f, 1.f), 1.f, 30.f})
                        for (unsigned mode = 0; mode < 6; mode++) {
                            if ((mode == 2 && drag != 0) || (mode == 3 && drag != 4))
                                continue;
                            const std::array n = orientation == 0   ? std::array{0.f, 1.f, 0.f}
                                                 : orientation == 1 ? std::array{1.f, 2.f, -.5f}
                                                                    : std::array{1.f, 3.f, 2.f};
                            const std::array tangent = orientation == 0 ? std::array{1.f, 0.f, 0.f}
                                                       : orientation == 1
                                                           ? std::array{2.f, -1.f, 0.f}
                                                           : std::array{2.f, 0.f, -1.f};
                            const float distance = mode == 2 || mode == 3 ? .25f
                                                   : mode == 4            ? 1.f
                                                   : mode == 5            ? -.0001f
                                                                          : 0.f;
                            const float speed = mode == 2 || mode == 3 || mode == 4 ? -1.f : 0.f;
                            const float force = mode == 0 || mode == 4 ? -4.f
                                                : mode == 2            ? 2.f
                                                                       : 0.f;
                            Input input;
                            ParticleMotionState initial;
                            for (unsigned i = 0; i < 3; i++) {
                                input.origin[i] = n[i] * distance;
                                input.velocity[i] = n[i] * speed + (mode == 1 ? tangent[i] : 0);
                                input.force[i] = n[i] * force;
                                initial.position[i] = input.origin[i];
                                initial.velocity[i] = input.velocity[i];
                            }
                            input.origin[3] = age;
                            input.velocity[3] = drag;
                            ParticleCollisionPlane plane;
                            plane.normal = {n[0], n[1], n[2]};
                            plane.offset = plane.radius = 0;
                            plane.restitution = restitution;
                            plane.friction = .25;
                            plane.bounce_limit = limit;
                            const auto prepared = prepare_particle_collision_plane(plane);
                            const auto packet = prepare_particle_gpu_collision_values(plane);
                            const double uncertainty =
                                8 * std::numeric_limits<float>::epsilon() * (1 + double(age));
                            const auto lo = evaluate_particle_plane_motion(
                                initial, {input.force[0], input.force[1], input.force[2]}, drag,
                                std::max(0., double(age) - uncertainty), prepared);
                            const auto hi = evaluate_particle_plane_motion(
                                initial, {input.force[0], input.force[1], input.force[2]}, drag,
                                std::min(30., double(age) + uncertainty), prepared);
                            {
                                MapHelper<ParticleGpuCollisionValues> mapped(
                                    context, plane_values, MAP_WRITE, MAP_FLAG_DISCARD);
                                *mapped = packet;
                            }
                            {
                                MapHelper<Input> mapped(context, parameters, MAP_WRITE,
                                                        MAP_FLAG_DISCARD);
                                *mapped = input;
                            }
                            context->SetPipelineState(pipeline);
                            context->CommitShaderResources(
                                binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                            DispatchComputeAttribs dispatch;
                            dispatch.ThreadGroupCountX = dispatch.ThreadGroupCountY =
                                dispatch.ThreadGroupCountZ = 1;
                            context->DispatchCompute(dispatch);
                            context->CopyBuffer(output, 0,
                                                RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging,
                                                0, 48, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                            context->WaitForIdle();
                            std::array<std::array<float, 4>, 3> actual;
                            {
                                MapHelper<std::array<float, 4>> mapped(context, staging, MAP_READ,
                                                                       MAP_FLAG_DO_NOT_WAIT);
                                const auto operation =
                                    "Plane readback map failed; case=" + std::to_string(cases);
                                test_gpu::require_readback(
                                    bool(static_cast<std::array<float, 4>*>(mapped)), device,
                                    operation.c_str());
                                for (unsigned i = 0; i < 3; i++)
                                    actual[i] = mapped[i];
                            }
                            auto fail = [&](const char* why) {
                                throw std::runtime_error(nlohmann::json{
                                    {"reason", why},
                                    {"case", cases},
                                    {"orientation", orientation},
                                    {"mode", mode},
                                    {"drag", drag},
                                    {"age", age},
                                    {"rest", restitution},
                                    {"limit", limit},
                                    {"actual", actual},
                                    {"lo_position", lo.state.position},
                                    {"hi_position", hi.state.position},
                                    {"lo_dead", bool(lo.death_age)},
                                    {"hi_dead", bool(hi.death_age)},
                                    {"time_uncertainty",
                                     uncertainty}}.dump());
                            };
                            const bool dead = actual[0][3] == 0;
                            if (actual[0][3] != 0 && actual[0][3] != 1)
                                fail("Invalid private motion alive flag");
                            if ((lo.death_age && !dead) || (!hi.death_age && dead))
                                fail("Death admission outside float-time boundary");
                            for (unsigned i = 0; i < 3; i++) {
                                const double tolerance =
                                    1e-4 + 32 * std::numeric_limits<float>::epsilon() *
                                               (std::abs(initial.position[i]) +
                                                std::abs(lo.state.position[i]) +
                                                std::abs(hi.state.position[i]));
                                if (!std::isfinite(actual[0][i]) ||
                                    actual[0][i] <
                                        std::min(lo.state.position[i], hi.state.position[i]) -
                                            tolerance ||
                                    actual[0][i] >
                                        std::max(lo.state.position[i], hi.state.position[i]) +
                                            tolerance)
                                    fail("Position outside bounded float reference interval");
                            }
                            context->FinishFrame();
                            device->ReleaseStaleResources();
                            ++cases;
                        }
    context->WaitForIdle();
    std::cout << "PRIVATE_GPU_PLANE_BOUNDARIES cases=" << cases
              << " CPU-reference/contacts/drag/hidden-crossings/tangency/"
                 "bounded-rebounds passed\n";
}
inline void check_runtime(IRenderDevice* device, IDeviceContext* context,
                          DiligentPresentation& presentation) {
    BufferDesc desc;
    desc.Name = "FORGE owner collision test readback";
    desc.Size = 1024 * 7 * sizeof(ParticleGpuRow);
    desc.Usage = USAGE_STAGING;
    desc.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<IBuffer> staging;
    device->CreateBuffer(desc, nullptr, &staging);
    check(bool(staging), "Owner collision readback unavailable");
    ParticleGpuInstances ordinary_gpu(presentation, 1024);
    ParticleGpuInstances particles(presentation, 1024, ParticleGpuMotion::PlaneCollision);
    unsigned cases = 0;
    std::uint64_t rows = 0, killed = 0;
    for (unsigned response : {0u, 1u, 2u})
        for (float drag : {0.f, .5f, 64.f})
            for (auto emission : {ParticleEmission::Burst, ParticleEmission::Continuous})
                for (unsigned count : {65u, 1024u})
                    for (auto shape : {ParticleSpawnShape::Point, ParticleSpawnShape::Box,
                                       ParticleSpawnShape::Sphere, ParticleSpawnShape::Disc})
                        for (double time : {0., .125, .5, 1., 2.5, 3.999, 1000000000., 1.}) {
                            ParticleEffectData effect;
                            effect.version = 20;
                            effect.mesh = engine_primitive(0);
                            effect.capacity = 1024;
                            effect.emission = emission;
                            effect.burst_count = count;
                            effect.spawn_rate = 32;
                            effect.lifetime = 4;
                            effect.direction = {-.3, -1, .2};
                            effect.cone_angle = 25;
                            effect.speed_min = 1;
                            effect.speed_max = 4;
                            effect.acceleration = {0, -4, 0};
                            effect.drag = drag;
                            effect.shape = shape;
                            effect.shape_size = {2, 2, 2};
                            effect.shape_radius = 1;
                            effect.size_start = effect.size_end = 1;
                            effect.color_start = effect.color_end = {.25, .5, .75};
                            ParticleCollisionPlane collision;
                            collision.normal = response == 0   ? std::array{0., 1., 0.}
                                               : response == 1 ? std::array{1., 2., -.5}
                                                               : std::array{1., 3., 2.};
                            collision.offset = -2;
                            collision.radius = response == 0 ? 0 : response == 1 ? .25 : 1.5;
                            collision.restitution = response == 0 ? 0 : response == 1 ? .5 : 1;
                            collision.friction = response == 0 ? 0 : response == 1 ? .25 : 1;
                            collision.bounce_limit = response == 0 ? 0 : response == 1 ? 4 : 16;
                            const auto packet = prepare_particle_gpu_collision_values(collision);
                            effect.collision = collision;
                            const auto plan =
                                prepare_particle_gpu_values(effect, time, 41, {}, CameraView{});
                            auto free_effect = effect;
                            free_effect.version = 3;
                            free_effect.collision.reset();
                            const auto free_plan = prepare_particle_gpu_values(
                                free_effect, time, 41, {}, CameraView{});
                            ordinary_gpu.evaluate(context, free_plan);
                            const auto ordinary =
                                particle_gpu_test::read_particles(context, ordinary_gpu, staging);
                            particles.evaluate(context, plan, nullptr, false, nullptr, &packet);
                            check(particles.count() == ordinary_gpu.count(),
                                  "Motion owner changed bounded birth-row count");
                            const auto actual =
                                particle_gpu_test::read_particles(context, particles, staging);
                            for (unsigned id = 0; id < actual.size(); id++) {
                                const float age =
                                    std::max(0.f, plan.timing[0] - float(id) * plan.timing[1]);
                                const auto initial = particle_gpu_test::spawn_state(plan, id);
                                const auto reference = evaluate_particle_plane_motion(
                                    initial,
                                    {plan.acceleration[0], plan.acceleration[1],
                                     plan.acceleration[2]},
                                    plan.timing[3], age,
                                    prepare_particle_collision_plane(collision));
                                auto fail = [&](const char* reason) {
                                    throw std::runtime_error(nlohmann::json{
                                        {"reason", reason},
                                        {"case", cases},
                                        {"id", id},
                                        {"response", response},
                                        {"shape", unsigned(shape)},
                                        {"drag", drag},
                                        {"time", time},
                                        {"age", age},
                                        {"actual", actual[id]},
                                        {"position", reference.state.position},
                                        {"death", reference.death_age.value_or(
                                                      -1)}}.dump());
                                };
                                const bool dead = bool(reference.death_age);
                                killed += dead;
                                if ((actual[id][0][0] == 0) != dead)
                                    fail("Killed birth geometry differs");
                                for (unsigned r = 0; r < 3; r++) {
                                    if (!std::isfinite(actual[id][r][3]) ||
                                        std::abs(actual[id][r][3] - reference.state.position[r]) >
                                            1e-3 * (1 + std::abs(reference.state.position[r])))
                                        fail("Birth collision position differs");
                                    for (unsigned c = 0; c < 3; c++)
                                        if (actual[id][r][c] != (dead ? 0 : ordinary[id][r][c]))
                                            fail("Collision changed live geometry");
                                }
                                for (unsigned r = 3; r < 7; r++)
                                    if (actual[id][r] != ordinary[id][r])
                                        fail("Collision changed normals/RGB/flags");
                                ++rows;
                            }
                            context->FinishFrame();
                            device->ReleaseStaleResources();
                            ++cases;
                        }
    // Count publication must fail closed and recover without another emitter
    // owner.
    ParticleEffectData checked;
    checked.version = 3;
    checked.mesh = engine_primitive(0);
    checked.capacity = 128;
    checked.spawn_rate = 16;
    checked.lifetime = 2;
    const auto plan = prepare_particle_gpu_values(checked, .5, 9, {}, CameraView{});
    const auto packet = prepare_particle_gpu_collision_values(ParticleCollisionPlane{});
    auto refused = [&](auto action) {
        bool caught = false;
        try {
            action();
        } catch (const std::exception&) {
            caught = true;
        }
        check(caught, "Invalid motion operation accepted");
    };
    particles.evaluate(context, plan, nullptr, false, nullptr, &packet);
    check(particles.count() > 0, "Runtime plane initial submission missing");
    refused([&] { particles.evaluate(context, plan); });
    check(particles.count() == 0, "Missing plane packet retained stale count");
    refused([&] { ordinary_gpu.evaluate(context, plan, nullptr, false, nullptr, &packet); });
    check(ordinary_gpu.count() == 0, "Legacy motion accepted plane data or stale count");
    auto invalid = packet;
    invalid.response[2] = 17;
    refused([&] { particles.evaluate(context, plan, nullptr, false, nullptr, &invalid); });
    check(particles.count() == 0, "Bad plane response retained count");
    auto timing = plan;
    timing.timing[0] = std::numeric_limits<float>::quiet_NaN();
    refused([&] { particles.evaluate(context, timing, nullptr, false, nullptr, &packet); });
    check(particles.count() == 0, "Bad plane time retained count");
    refused(
        [&] { ParticleGpuInstances wrong(presentation, 128, static_cast<ParticleGpuMotion>(99)); });
    particles.evaluate(context, plan, nullptr, false, nullptr, &packet);
    check(particles.count() == plan.births[0], "Plane owner failed valid recovery");
    context->WaitForIdle();
    std::cout << "GPU_OWNER_SHAPES_PLANE cases=" << cases << " rows=" << rows
              << " killed=" << killed << " original47/RNG/window/geometry/appearance passed\n";
}
} // namespace forge::particle_plane_test
