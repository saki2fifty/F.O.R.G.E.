#pragma once
#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "particle_gpu.hpp"
#include "particle_gpu_order.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <forge/engine_assets.hpp>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <numeric>
#include <stdexcept>
namespace forge::particle_order_test {
using namespace Diligent;
using Row = std::array<float, 4>;
using Packet = std::array<Row, 7>;
inline void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Fixture {
    using SourceSlice = ParticleGpuOrder::Source;
    ParticleGpuOrder order;
    RefCntAutoPtr<IBuffer> staging;
    unsigned count = 0, padded = 1;
    Fixture(DiligentPresentation& presentation) : order(presentation) {
        check(!order.buffer() && !order.count(), "Lazy sort resource published storage");
        BufferDesc desc;
        desc.Name = "FORGE product order test readback";
        desc.Size = 8192 * sizeof(Packet);
        desc.Usage = USAGE_STAGING;
        desc.CPUAccessFlags = CPU_ACCESS_READ;
        presentation.device()->CreateBuffer(desc, nullptr, &staging);
        check(bool(staging), "Product sort readback unavailable");
    }
    void evaluate_device(IDeviceContext* context, std::span<const SourceSlice> sources,
                         Row forward) {
        CameraView camera{};
        for (unsigned a = 0; a < 3; ++a)
            camera.forward[a] = forward[a];
        order.evaluate(context, sources, camera, {});
        count = order.count();
        padded = std::bit_ceil(std::max(count, 1u));
    }
    std::vector<Packet> read(IDeviceContext* context) {
        if (!count)
            return {};
        context->CopyBuffer(order.buffer(), 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging,
                            0, padded * sizeof(Packet), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->WaitForIdle();
        MapHelper<Packet> mapped(context, staging, MAP_READ, MAP_FLAG_DO_NOT_WAIT);
        check(bool(static_cast<Packet*>(mapped)), "Product sorted rows unavailable");
        return {static_cast<Packet*>(mapped), static_cast<Packet*>(mapped) + padded};
    }
    void lifecycle(IRenderDevice* device, IDeviceContext* context) {
        Packet packet{};
        packet[6][3] = 4;
        BufferDesc desc;
        desc.Name = "FORGE order lifecycle source";
        desc.Size = sizeof(Packet);
        desc.Usage = USAGE_DEFAULT;
        desc.Mode = BUFFER_MODE_STRUCTURED;
        desc.ElementByteStride = sizeof(Row);
        desc.BindFlags = BIND_SHADER_RESOURCE;
        BufferData initial{&packet, sizeof(packet)};
        RefCntAutoPtr<IBuffer> source;
        device->CreateBuffer(desc, &initial, &source);
        check(bool(source), "Lifecycle source unavailable");
        CameraView camera{};
        camera.forward = {0, 0, 1};
        const std::array valid{SourceSlice{source, 1}};
        const auto rejects = [&](auto action) {
            order.evaluate(context, valid, camera, {});
            check(order.count() == 1, "Sort recovery failed");
            bool failed = false;
            try {
                action();
            } catch (const std::exception&) {
                failed = true;
            }
            check(failed && !order.count(), "Malformed sort source retained a stale count");
        };
        rejects([&] { order.evaluate(nullptr, valid, camera, {}); });
        rejects([&] {
            std::array<SourceSlice, 65> many{};
            order.evaluate(context, many, camera, {});
        });
        rejects([&] {
            std::array bad{SourceSlice{source, 8193}};
            order.evaluate(context, bad, camera, {});
        });
        rejects([&] {
            std::array bad{SourceSlice{source, 2}};
            order.evaluate(context, bad, camera, {});
        });
        rejects([&] {
            std::array bad{SourceSlice{nullptr, 1}};
            order.evaluate(context, bad, camera, {});
        });
        rejects([&] {
            std::array bad{SourceSlice{order.buffer(), 1}};
            order.evaluate(context, bad, camera, {});
        });
        rejects([&] {
            auto bad = camera;
            bad.forward[1] = std::numeric_limits<double>::quiet_NaN();
            order.evaluate(context, valid, bad, {});
        });
        rejects([&] {
            order.evaluate(context, valid, camera, {0, std::numeric_limits<float>::infinity(), 0});
        });
        order.clear();
        check(!order.count() && !order.buffer(), "Sort clear retained published state");
        order.evaluate(context, {}, camera, {});
        check(!order.count() && !order.buffer(), "Empty sort allocated stale geometry");
        order.evaluate(context, valid, camera, {});
        check(order.count() == 1 && order.buffer(), "Cleared sort did not recover");
    }
};
inline float depth(const Packet& p, const Row& f) {
    std::array<float, 3> center;
    if (p[6][3] == 4)
        for (unsigned a = 0; a < 3; ++a)
            center[a] = .5f * p[0][a] + .5f * p[1][a];
    else
        for (unsigned a = 0; a < 3; ++a)
            center[a] = p[a][3];
    return center[0] * f[0] + center[1] * f[1] + center[2] * f[2];
}

inline Packet packet(unsigned index, unsigned variant) {
    const float x = float(int(index % 17) - 8) / 4, y = float(int(index % 11) - 5) / 8;
    const float z = variant == 0   ? float(int((index * 37) % 31) - 15) / 4
                    : variant == 1 ? float(index % 4)
                    : variant == 2 ? (index % 2 ? -0.f : 0.f)
                                   : std::bit_cast<float>(0x40000000u + index % 127);
    Packet result{};
    if (index % 2) {
        result[0] = {x, y, z, .25f};
        result[1] = {x + .25f, y + .5f, z, .5f};
        result[2] = {.25f, .5f, .75f, 0};
        result[3] = {.5f, 0, 0, 0};
        result[4] = {.25f, 0, 0, 1};
        result[5] = {0, 0, -1, 0};
        result[6] = {.75f, .25f, .5f, 4};
    } else {
        result[0] = {1, 0, 0, x};
        result[1] = {0, 1, 0, y};
        result[2] = {0, 0, 1, z};
        result[3] = {1, 0, 0, 0};
        result[4] = {0, 1, 0, 0};
        result[5] = {0, 0, 1, 0};
        result[6] = {.25f, .5f, .75f, 2};
    }
    // An unused lane is an immutable payload marker, independent of GPU keys.
    result[5][3] = float(index);
    return result;
}

inline void synthetic_cases(Fixture& fixture, IRenderDevice* device, IDeviceContext* context) {
    BufferDesc desc;
    desc.Name = "FORGE synthetic order input";
    desc.Size = 8192 * sizeof(Packet);
    desc.Mode = BUFFER_MODE_STRUCTURED;
    desc.ElementByteStride = sizeof(Row);
    desc.Usage = USAGE_DEFAULT;
    desc.BindFlags = BIND_SHADER_RESOURCE;
    RefCntAutoPtr<IBuffer> source;
    device->CreateBuffer(desc, nullptr, &source);
    check(bool(source), "Synthetic order source unavailable");
    unsigned cases = 0;
    std::size_t slots = 0;
    for (unsigned count :
         {0u, 1u, 2u, 3u, 7u, 31u, 64u, 65u, 127u, 257u, 1023u, 1024u, 4097u, 8191u, 8192u})
        for (unsigned variant = 0; variant < 4; ++variant)
            for (const Row forward : {Row{0, 0, 1, 0}, Row{0, 0, -1, 0}, Row{.5f, .5f, 0, 0}}) {
                std::vector<Packet> input;
                for (unsigned i = 0; i < count; ++i)
                    input.push_back(packet(i, variant));
                if (count)
                    context->UpdateBuffer(source, 0, count * sizeof(Packet), input.data(),
                                          RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                const std::array sources{Fixture::SourceSlice{source, count}};
                std::vector<unsigned> expected(count);
                std::iota(expected.begin(), expected.end(), 0);
                std::stable_sort(expected.begin(), expected.end(), [&](unsigned a, unsigned b) {
                    return depth(input[a], forward) > depth(input[b], forward);
                });
                fixture.evaluate_device(context, sources, forward);
                const auto output = fixture.read(context);
                for (unsigned i = 0; i < count; ++i)
                    check(std::memcmp(&output[i], &input[expected[i]], sizeof(Packet)) == 0,
                          "Stable GPU order or packet bytes differ from CPU reference");
                for (unsigned i = count; i < output.size(); ++i) {
                    Packet empty{};
                    empty[6][3] = 4;
                    check(output[i] == empty, "GPU order padding retained stale geometry");
                }
                slots += output.size();
                ++cases;
                context->FinishFrame();
                device->ReleaseStaleResources();
            }
    std::cout << "GPU_ORDER_KEYS cases=" << cases << " slots=" << slots << '\n';
}
inline void mesh_center_cases(Fixture& fixture, IRenderDevice* device, IDeviceContext* context) {
    std::array<Packet, 2> input{};
    for (unsigned i = 0; i < 2; ++i) {
        input[i][0] = {1, 0, 0, 0};
        input[i][1] = {0, 1, 0, 0};
        input[i][2] = {0, 0, i ? .125f : 1.f, i ? 3.25f : 3.f};
        input[i][6] = {float(i), 1, 1, 2};
    }
    BufferDesc desc;
    desc.Name = "FORGE non-origin Mesh order input";
    desc.Size = sizeof(input);
    desc.Usage = USAGE_DEFAULT;
    desc.Mode = BUFFER_MODE_STRUCTURED;
    desc.ElementByteStride = sizeof(Row);
    desc.BindFlags = BIND_SHADER_RESOURCE;
    BufferData data{input.data(), sizeof(input)};
    RefCntAutoPtr<IBuffer> source;
    device->CreateBuffer(desc, &data, &source);
    check(bool(source), "Non-origin Mesh sort source unavailable");
    const std::array sources{Fixture::SourceSlice{source, 2}};
    CameraView camera{};
    camera.forward = {0, 0, 1};
    for (bool offset : {false, true}) {
        fixture.order.evaluate(context, sources, camera, {0, 0, offset ? 1.f : 0.f});
        fixture.count = 2;
        fixture.padded = 2;
        const auto output = fixture.read(context);
        const unsigned first = offset ? 0 : 1;
        check(std::memcmp(&output[0], &input[first], sizeof(Packet)) == 0 &&
                  std::memcmp(&output[1], &input[1 - first], sizeof(Packet)) == 0,
              "GPU order used the origin instead of transformed Mesh-part center");
        context->FinishFrame();
        device->ReleaseStaleResources();
    }
}

inline void execute(DiligentPresentation& presentation, IDeviceContext* context) {
    auto* device = presentation.device();
    Fixture proof(presentation);
    synthetic_cases(proof, device, context);
    BufferDesc desc;
    desc.Name = "FORGE unsorted producer test readback";
    desc.Size = 8192 * sizeof(Packet);
    desc.Usage = USAGE_STAGING;
    desc.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<IBuffer> unsorted;
    device->CreateBuffer(desc, nullptr, &unsorted);
    check(bool(unsorted), "Unsorted producer test readback unavailable");
    unsigned cases = 0;
    std::size_t packets_checked = 0;
    // The reference reads producer output only in this fixture. The gather/
    // sort path itself consumes the original device buffers without readback.
    for (unsigned geometry = 0; geometry < 3; ++geometry)
        for (unsigned count : {1u, 65u, 1024u})
            for (bool reflected : {false, true})
                for (bool reverse : {false, true}) {
                    ParticleEffectData data;
                    data.version = geometry == 2 ? 23 : geometry == 1 ? 8 : 3;
                    data.mesh = engine_primitive(8);
                    data.emission = ParticleEmission::Burst;
                    data.capacity = data.burst_count = count;
                    data.lifetime = 4;
                    data.direction = {0, 1, 0};
                    data.cone_angle = 45;
                    data.speed_min = .25;
                    data.speed_max = 1;
                    data.acceleration = {0, -.25, 0};
                    data.drag = .15;
                    data.shape = ParticleSpawnShape::Sphere;
                    data.shape_radius = .5;
                    if (geometry == 2)
                        data.trail = ParticleTrailSettings{.5, 4};
                    std::array<std::unique_ptr<ParticleGpuInstances>, 2> producers;
                    for (auto& producer : producers)
                        producer =
                            data.trail
                                ? std::make_unique<ParticleGpuInstances>(presentation, count,
                                                                         *data.trail)
                                : std::make_unique<ParticleGpuInstances>(presentation, count);
                    Camera settings;
                    AffineTransform camera_pose;
                    camera_pose.m[3] = 1e12;
                    camera_pose.m[7] = -1e12;
                    if (reverse) {
                        camera_pose.m[0] = camera_pose.m[10] = -1;
                        camera_pose.m[11] = 6;
                    }
                    const auto camera = camera_view(settings, camera_pose, 64, 64);
                    const Row forward{float(camera.forward[0]), float(camera.forward[1]),
                                      float(camera.forward[2]), 0};
                    for (double time : {0., .5, 3.999, 4.25, 4.5}) {
                        std::array<Fixture::SourceSlice, 2> slices;
                        unsigned total = 0;
                        for (unsigned source = 0; source < 2; ++source) {
                            AffineTransform world;
                            world.m[0] = reflected ? -1 : 1;
                            world.m[1] = .25;
                            world.m[3] = 1e12 + (source ? .25 : -.25);
                            world.m[7] = -1e12;
                            world.m[11] = 3 + (source ? .25 : 0);
                            const auto values =
                                prepare_particle_gpu_values(data, time, 41 + source, world, camera);
                            producers[source]->evaluate(context, values,
                                                        geometry ? &camera : nullptr, true);
                            auto& slice = slices[source];
                            slice = {producers[source]->buffer(), producers[source]->count()};
                            if (slice.count)
                                context->CopyBuffer(
                                    slice.buffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                    unsorted, total * sizeof(Packet), slice.count * sizeof(Packet),
                                    RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                            total += slice.count;
                        }
                        context->WaitForIdle();
                        std::vector<Packet> input;
                        if (total) {
                            MapHelper<Packet> mapped(context, unsorted, MAP_READ,
                                                     MAP_FLAG_DO_NOT_WAIT);
                            check(bool(static_cast<Packet*>(mapped)),
                                  "Unsorted reference rows unavailable");
                            input.assign(static_cast<Packet*>(mapped),
                                         static_cast<Packet*>(mapped) + total);
                        }
                        std::vector<unsigned> expected(total);
                        std::iota(expected.begin(), expected.end(), 0);
                        std::stable_sort(
                            expected.begin(), expected.end(), [&](unsigned a, unsigned b) {
                                return depth(input[a], forward) > depth(input[b], forward);
                            });
                        proof.evaluate_device(context, slices, forward);
                        check(proof.count == total, "Device gather changed reserved source counts");
                        const auto output = proof.read(context);
                        for (unsigned i = 0; i < total; ++i)
                            if (std::memcmp(&output[i], &input[expected[i]], sizeof(Packet)))
                                throw std::runtime_error(nlohmann::json{
                                    {"reason", "Actual device-produced packets lost stable "
                                               "order or bytes"},
                                    {"geometry", geometry},
                                    {"capacity", count},
                                    {"reflected", reflected},
                                    {"reverse", reverse},
                                    {"time", time},
                                    {"slot", i},
                                    {"expected_index",
                                     expected[i]}}.dump());
                        packets_checked += output.size();
                        ++cases;
                        context->FinishFrame();
                        device->ReleaseStaleResources();
                    }
                }
    proof.lifecycle(device, context);

    mesh_center_cases(proof, device, context);
    context->WaitForIdle();
    std::cout << "GPU_ORDER_PRODUCERS cases=" << cases << " slots=" << packets_checked
              << "; lifecycle and Mesh center checks passed\n";
}
} // namespace forge::particle_order_test
