#pragma once
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "particle_gpu.hpp"
#include "particle_gpu_reference.hpp"
#include "particle_motion.hpp"
#include "particle_ribbon.hpp"
#include <algorithm>
#include <cmath>
#include <forge/engine_assets.hpp>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
namespace forge::particle_trail_test {
using namespace Diligent;
inline void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
inline ParticleSample appearance(ParticleEffectData data, const ParticleGpuValues& values,
                                 const ParticleGpuOpacityValues& opacity, float at) {
    data.version = 6;
    data.trail.reset();
    data.collision.reset();
    data.program.reset();
    data.emission = ParticleEmission::Burst;
    data.capacity = data.burst_count = 1;
    data.lifetime = 1;
    data.size_start = values.sizes[0];
    data.size_end = values.sizes[1];
    data.color_start = {values.colors[0][0], values.colors[0][1], values.colors[0][2]};
    data.color_end = {values.colors[1][0], values.colors[1][1], values.colors[1][2]};
    data.opacity_start = opacity.endpoints[0];
    data.opacity_end = opacity.endpoints[1];
    data.size_curve.clear();
    data.color_gradient.clear();
    data.opacity_curve.clear();
    for (unsigned i = 0; i < values.modes[1]; ++i) {
        const auto& stop = values.size_stops[i];
        data.size_curve.push_back({stop[0], stop[1], stop[2] != 0});
    }
    for (unsigned i = 0; i < values.modes[2]; ++i) {
        const auto& stop = values.color_stops[i];
        data.color_gradient.push_back(
            {stop[0], {stop[1], stop[2], stop[3]}, (values.modes[3] & (1u << i)) != 0});
    }
    for (unsigned i = 0; i < opacity.count; ++i) {
        const auto& stop = opacity.stops[i];
        data.opacity_curve.push_back({stop[0], stop[1], stop[2] != 0});
    }
    const float u = std::clamp(at / values.timing[2], 0.f, 1.f);
    auto result = u < 1 ? sample_particle_effect(data, u, 0, 1).front()
                        : sample_particle_paths(data, 1, 0, .5, 1).points.back();
    result.age = at;
    return result;
}
inline void cases(DiligentPresentation& presentation, IDeviceContext* context, IBuffer* staging) {
    auto* device = presentation.device();
    std::map<std::pair<float, unsigned>, std::unique_ptr<ParticleGpuInstances>> realizations;
    unsigned tests = 0, nonempty = 0, empty = 0;
    std::uint64_t slots = 0, live = 0, joins = 0;
    double maximum_error = 0;
    for (unsigned scenario : {0u, 1u, 2u, 3u})
        for (unsigned births : {65u, 512u})
            for (unsigned shape = 0; shape < 4; ++shape)
                for (auto emission : {ParticleEmission::Continuous, ParticleEmission::Burst})
                    for (float drag : {0.f, .5f, 64.f})
                        for (unsigned segments : {1u, 4u, 16u})
                            for (unsigned style : {0u, 1u})
                                for (double phase :
                                     {0., .0625, .25, .5, 1., 1.25, 1000000000., 2., .25}) {
                                    if (births == 512 && emission == ParticleEmission::Continuous)
                                        continue;
                                    const double lifetime = scenario == 1   ? .01
                                                            : scenario == 2 ? 15
                                                            : scenario == 3 ? 30
                                                                            : 2;
                                    const float duration = scenario == 1   ? .001f
                                                           : scenario == 2 ? 15.f
                                                           : scenario == 3 ? 30.f
                                                                           : .5f;
                                    const double seconds =
                                        phase == 1000000000. ? phase : phase * lifetime;
                                    ParticleEffectData data;
                                    data.version = 25;
                                    data.trail = ParticleTrailSettings{duration, segments};
                                    data.mesh = engine_primitive(8);
                                    data.capacity = 512;
                                    data.burst_count = births;
                                    data.spawn_rate = scenario >= 2 ? 8 : 32;
                                    data.lifetime = lifetime;
                                    data.emission = emission;
                                    data.direction = {1, 1, .25};
                                    data.cone_angle = 25;
                                    data.speed_min = .5;
                                    data.speed_max = 3;
                                    data.shape = ParticleSpawnShape(shape);
                                    data.shape_size = {1, 2, .5};
                                    data.shape_radius = .75;
                                    data.acceleration = {.25, -3, .5};
                                    data.drag = drag;
                                    data.size_start = .125;
                                    data.size_end = .5;
                                    data.color_start = {.25f, .5f, .75f};
                                    data.color_end = {.75f, .25f, .125f};
                                    data.opacity_start = .125;
                                    data.opacity_end = .875;
                                    if (style) {
                                        data.size_curve = {
                                            {0, .125, true}, {.25, .5, false}, {1, .25, false}};
                                        data.color_gradient = {{0, {1, .25f, .5f}, false},
                                                               {.5, {.25f, 1, .5f}, true},
                                                               {1, {.5f, .25f, 1}, false}};
                                        data.opacity_curve = {
                                            {0, .25, true}, {.5, .75, false}, {1, .125, false}};
                                    }
                                    Camera settings;
                                    AffineTransform camera_pose;
                                    camera_pose.m = {.8, 0,     .6,  1e12, 0,  1,
                                                     0,  -1e12, -.6, 0,    .8, 0};
                                    const auto camera = camera_view(settings, camera_pose, 64, 64);
                                    AffineTransform emitter;
                                    emitter.m = {-1, .125, 0, 1e12, .25, 1, 0, -1e12, 0, 0, .5, 3};
                                    const auto plan = prepare_particle_gpu_values(
                                        data, seconds, 41, emitter, camera, 1.5, {.5, .75, 1});
                                    const auto opacity = prepare_particle_gpu_opacity_values(data);
                                    const auto key = std::pair{duration, segments};
                                    auto& realization = realizations[key];
                                    if (!realization)
                                        realization = std::make_unique<ParticleGpuInstances>(
                                            presentation, 512,
                                            ParticleTrailSettings{duration, segments});
                                    realization->evaluate(context, plan, &camera, true, &opacity);
                                    const unsigned count = plan.births[0] * segments;
                                    check(realization->count() == count,
                                          "Actual runtime publishes an incorrect segment count");
                                    if (count) {
                                        context->CopyBuffer(
                                            realization->buffer(), 0,
                                            RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging, 0,
                                            count * 7 * sizeof(ParticleGpuRow),
                                            RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                                        context->WaitForIdle();
                                    }
                                    std::vector<std::array<ParticleGpuRow, 7>> actual(count);
                                    if (count) {
                                        MapHelper<ParticleGpuRow> mapped(context, staging, MAP_READ,
                                                                         MAP_FLAG_DO_NOT_WAIT);
                                        check(bool(static_cast<ParticleGpuRow*>(mapped)),
                                              "Analytic trail readback unavailable");
                                        for (unsigned i = 0; i < count; ++i)
                                            std::copy_n(&mapped[i * 7], 7, actual[i].begin());
                                    }
                                    AffineTransform relative_emitter;
                                    for (unsigned row = 0; row < 3; ++row)
                                        for (unsigned c = 0; c < 4; ++c)
                                            relative_emitter.m[row * 4 + c] = plan.emitter[row][c];
                                    auto relative_camera = camera;
                                    relative_camera.position = {};
                                    for (unsigned id = 0; id < plan.births[0]; ++id) {
                                        const auto state = particle_gpu_test::spawn_state(plan, id);
                                        ParticlePathSamples paths;
                                        const float head = std::max(
                                            0.f, plan.timing[0] - float(id) * plan.timing[1]);
                                        for (unsigned point = 0; point <= segments; ++point) {
                                            const float at = std::clamp(
                                                head - duration * float(segments - point) /
                                                           float(segments),
                                                0.f, plan.timing[2]);
                                            if (!paths.points.empty() &&
                                                at <= paths.points.back().age)
                                                continue;
                                            auto sample = appearance(data, plan, opacity, at);
                                            sample.birth = 0;
                                            const auto motion = evaluate_particle_motion(
                                                state,
                                                {plan.acceleration[0], plan.acceleration[1],
                                                 plan.acceleration[2]},
                                                plan.timing[3], at);
                                            sample.position = motion.position;
                                            paths.points.push_back(sample);
                                        }
                                        std::vector<ParticleRibbonSegment> reference;
                                        if (paths.points.size() > 1) {
                                            paths.paths.push_back({0, 0, paths.points.size()});
                                            reference = particle_ribbon_segments(
                                                paths, relative_emitter, relative_camera,
                                                plan.acceleration[3],
                                                {plan.tint[0], plan.tint[1], plan.tint[2]});
                                        }
                                        for (unsigned segment = 0; segment < segments; ++segment) {
                                            const auto& got = actual[id * segments + segment];
                                            auto fail = [&](const char* reason, unsigned row,
                                                            unsigned lane, double expected) {
                                                throw std::runtime_error(nlohmann::json{
                                                    {"reason", reason},
                                                    {"test", tests},
                                                    {"scenario", scenario},
                                                    {"births", births},
                                                    {"shape", shape},
                                                    {"emission", unsigned(emission)},
                                                    {"drag", drag},
                                                    {"segments", segments},
                                                    {"style", style},
                                                    {"seconds", seconds},
                                                    {"id", id},
                                                    {"segment", segment},
                                                    {"row", row},
                                                    {"lane", lane},
                                                    {"expected", expected},
                                                    {"actual", got[row][lane]},
                                                    {"head", head}}.dump());
                                            };
                                            for (unsigned row = 0; row < 7; ++row)
                                                for (unsigned lane = 0; lane < 4; ++lane)
                                                    if (!std::isfinite(got[row][lane]))
                                                        fail("Nonfinite analytic output", row, lane,
                                                             0);
                                            if (got[6][3] != 4)
                                                fail("Wrong ribbon flag", 6, 3, 4);
                                            if (segment >= reference.size()) {
                                                for (unsigned row = 0; row < 7; ++row)
                                                    for (unsigned lane = 0; lane < 4; ++lane)
                                                        if (got[row][lane] !=
                                                            (row == 6 && lane == 3 ? 4.f : 0.f))
                                                            fail("Inactive reserved slot has "
                                                                 "visible geometry",
                                                                 row, lane, 0);
                                                ++empty;
                                                continue;
                                            }
                                            const auto& v = reference[segment].vertices;
                                            std::array<std::array<double, 4>, 7> expected{};
                                            for (unsigned lane = 0; lane < 3; ++lane) {
                                                expected[0][lane] = v.first[lane];
                                                expected[1][lane] = v.last[lane];
                                                expected[2][lane] = v.color[0][lane];
                                                expected[3][lane] = v.first_side[lane];
                                                expected[4][lane] = v.last_side[lane];
                                                expected[5][lane] = v.normal[lane];
                                                expected[6][lane] = v.color[1][lane];
                                            }
                                            expected[0][3] = v.opacity[0];
                                            expected[1][3] = v.opacity[1];
                                            expected[3][3] = v.texture_v[0];
                                            expected[4][3] = v.texture_v[1];
                                            expected[6][3] = 4;
                                            for (unsigned row = 0; row < 7; ++row)
                                                for (unsigned lane = 0; lane < 4; ++lane) {
                                                    const double error =
                                                        std::abs(got[row][lane] -
                                                                 expected[row][lane]) /
                                                        (1 + std::abs(expected[row][lane]));
                                                    maximum_error = std::max(maximum_error, error);
                                                    if (error > 2e-4)
                                                        fail("Analytic ribbon differs from "
                                                             "independent CPU "
                                                             "owners",
                                                             row, lane, expected[row][lane]);
                                                }
                                            if (segment > 0) {
                                                const auto& previous =
                                                    actual[id * segments + segment - 1];
                                                check(previous[1] == got[0] &&
                                                          previous[4] == got[3],
                                                      "Analytic shared join split");
                                                ++joins;
                                            }
                                            ++live;
                                        }
                                    }
                                    slots += count;
                                    nonempty += count > 0;
                                    context->FinishFrame();
                                    device->ReleaseStaleResources();
                                    ++tests;
                                }
    std::cout << nlohmann::json{{"cases", tests},
                                {"nonempty_cases", nonempty},
                                {"reserved_segments", slots},
                                {"live_segments", live},
                                {"inactive_slots", empty},
                                {"shared_joins", joins},
                                {"largest_relative_error", maximum_error}}
                     .dump()
              << '\n';
}
inline unsigned refusal_index = 0;
template <class F> void rejected(F&& action) {
    const auto index = refusal_index++;
    bool failed = false;
    try {
        action();
    } catch (const std::exception&) {
        failed = true;
    }
    if (!failed)
        throw std::runtime_error("Malformed private ribbon request accepted at refusal " +
                                 std::to_string(index));
}
inline void collapsed_segments(DiligentPresentation& presentation, IDeviceContext* context,
                               IBuffer* staging) {
    const auto camera = camera_view(Camera{}, {}, 64, 64);
    const ParticleTrailSettings trail{.001, 16};
    ParticleGpuInstances gpu(presentation, 1, trail);
    ParticleEffectData effect;
    effect.version = 23;
    effect.mesh = engine_primitive(8);
    effect.trail = trail;
    effect.emission = ParticleEmission::Burst;
    effect.capacity = effect.burst_count = 1;
    effect.lifetime = 1;
    effect.cone_angle = 0;
    effect.speed_min = effect.speed_max = std::sqrt(2.);
    effect.acceleration = {-2, -2, 0};
    effect.size_start = .5;
    effect.size_end = .1;
    AffineTransform world;
    world.m[11] = 3;
    unsigned cases = 0, collapsed = 0;
    for (double offset : {0., 16.})
        for (int direction = -8; direction <= 8; ++direction)
            for (int tick = -256; tick <= 256; ++tick) {
                world.m[3] = world.m[7] = offset;
                effect.direction = {
                    1, 1 + direction * double(std::numeric_limits<float>::epsilon()), 0};
                const double time = .5005 + tick / 1048576.;
                const auto values = prepare_particle_gpu_values(effect, time, 41, world, camera);
                gpu.evaluate(context, values, &camera);
                context->CopyBuffer(gpu.buffer(), 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                    staging, 0, gpu.count() * 7 * sizeof(ParticleGpuRow),
                                    RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                context->WaitForIdle();
                {
                    MapHelper<ParticleGpuRow> mapped(context, staging, MAP_READ,
                                                     MAP_FLAG_DO_NOT_WAIT);
                    check(bool(static_cast<ParticleGpuRow*>(mapped)),
                          "Collapsed ribbon rows unavailable");
                    for (unsigned segment = 0; segment < gpu.count(); ++segment) {
                        const auto* rows = &mapped[segment * 7];
                        if (rows[0][0] != rows[1][0] || rows[0][1] != rows[1][1] ||
                            rows[0][2] != rows[1][2])
                            continue;
                        ++collapsed;
                        // An independently evaluated geometric invariant: a zero-length
                        // path segment must not acquire area from differing endpoint sides.
                        const std::array<double, 3> area{
                            double(rows[3][1]) * rows[4][2] - double(rows[3][2]) * rows[4][1],
                            double(rows[3][2]) * rows[4][0] - double(rows[3][0]) * rows[4][2],
                            double(rows[3][0]) * rows[4][1] - double(rows[3][1]) * rows[4][0]};
                        check(std::hypot(area[0], area[1], area[2]) == 0,
                              "Coincident ribbon centers produced nonzero triangle area");
                    }
                }
                context->FinishFrame();
                presentation.device()->ReleaseStaleResources();
                ++cases;
            }
    check(collapsed > 0, "Ribbon fixture missed float-collapsed intervals");
    std::cout << "GPU_RIBBON_COLLAPSE cases=" << cases << " coincident_slots=" << collapsed << '\n';
}
inline void lifecycle(DiligentPresentation& presentation, IDeviceContext* context,
                      IBuffer* staging) {
    for (unsigned capacity : {0u, 1025u})
        rejected(
            [&] { ParticleGpuInstances gpu(presentation, capacity, ParticleTrailSettings{}); });
    for (unsigned segments : {0u, 17u})
        rejected([&] {
            ParticleGpuInstances gpu(presentation, 1, ParticleTrailSettings{.5, segments});
        });
    for (double duration : {0., .0005, 31., std::numeric_limits<double>::infinity(),
                            std::numeric_limits<double>::quiet_NaN()})
        rejected(
            [&] { ParticleGpuInstances gpu(presentation, 1, ParticleTrailSettings{duration, 1}); });
    rejected([&] { ParticleGpuInstances gpu(presentation, 1024, ParticleTrailSettings{.5, 16}); });
    ParticleGpuInstances maximum(presentation, 512, ParticleTrailSettings{30, 16});
    check(maximum.count() == 0 && maximum.buffer() == nullptr,
          "Lazy trail realization published storage prematurely");
    ParticleGpuInstances gpu(presentation, 512, ParticleTrailSettings{.5, 16});
    Camera settings;
    const auto camera = camera_view(settings, {}, 64, 64);
    // Independently rounded life and duration can sum below the rounded head
    // age of a birth admitted in double precision immediately before expiry.
    ParticleEffectData boundary;
    boundary.version = 23;
    boundary.mesh = engine_primitive(8);
    boundary.emission = ParticleEmission::Burst;
    boundary.capacity = boundary.burst_count = 1;
    boundary.lifetime = 4.6525955968881965;
    boundary.trail = ParticleTrailSettings{6.398235780722425, 4};
    const auto boundary_values = prepare_particle_gpu_values(
        boundary, std::nextafter(boundary.lifetime + boundary.trail->duration, 0.), 41, {}, camera);
    check(boundary_values.births[0] == 1 &&
              boundary_values.timing[0] >
                  boundary_values.timing[2] + float(boundary.trail->duration),
          "Rounded lifetime boundary fixture no longer exercises independent input rounding");
    ParticleGpuInstances boundary_gpu(presentation, 1, *boundary.trail);
    boundary_gpu.evaluate(context, boundary_values, &camera);
    check(boundary_gpu.count() == 4, "Admitted boundary birth lost its reserved ribbon slots");
    ParticleEffectData data;
    data.version = 16;
    data.mesh = engine_primitive(8);
    data.capacity = 512;
    data.burst_count = 65;
    data.emission = ParticleEmission::Burst;
    const auto values = prepare_particle_gpu_values(data, .5, 41, {}, camera);
    const auto opacity = prepare_particle_gpu_opacity_values(data);
    auto recover = [&] {
        gpu.evaluate(context, values, &camera, true, &opacity);
        check(gpu.count() == 65 * 16, "Runtime ribbon recovery failed");
    };
    auto refusal = [&](const ParticleGpuValues& candidate, const CameraView* selected) {
        rejected([&] { gpu.evaluate(context, candidate, selected, true, &opacity); });
        check(gpu.count() == 0, "Refused ribbon input retained a stale published count");
        recover();
    };
    recover();
    refusal(values, nullptr);
    auto invalid = values;
    invalid.births[0] = 513;
    refusal(invalid, &camera);
    for (unsigned lane = 0; lane < 4; ++lane) {
        invalid = values;
        invalid.timing[lane] = std::numeric_limits<float>::quiet_NaN();
        refusal(invalid, &camera);
        invalid = values;
        invalid.timing[lane] = -1;
        refusal(invalid, &camera);
    }
    invalid = values;
    invalid.timing[0] = 61;
    refusal(invalid, &camera);
    invalid = values;
    invalid.timing[2] = 31;
    refusal(invalid, &camera);
    invalid = values;
    invalid.timing[2] = 0;
    refusal(invalid, &camera);
    invalid = values;
    invalid.timing[3] = 65;
    refusal(invalid, &camera);
    invalid = values;
    invalid.modes[1] = 1;
    refusal(invalid, &camera);
    invalid = values;
    invalid.modes[2] = 17;
    refusal(invalid, &camera);
    auto bad_camera = camera;
    bad_camera.forward[0] = std::numeric_limits<double>::quiet_NaN();
    refusal(values, &bad_camera);
    const auto collision = prepare_particle_gpu_collision_values(ParticleCollisionPlane{});
    rejected([&] { gpu.evaluate(context, values, &camera, true, &opacity, &collision); });
    check(gpu.count() == 0, "Unexpected plane packet retained a stale ribbon count");
    recover();
    auto bad_opacity = opacity;
    bad_opacity.endpoints[0] = -1;
    rejected([&] { gpu.evaluate(context, values, &camera, true, &bad_opacity); });
    check(gpu.count() == 0, "Invalid opacity retained a stale ribbon count");
    recover();
    gpu.evaluate(context, values, &camera);
    context->CopyBuffer(gpu.buffer(), 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging, 0,
                        7 * sizeof(ParticleGpuRow), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->WaitForIdle();
    {
        MapHelper<ParticleGpuRow> mapped(context, staging, MAP_READ, MAP_FLAG_DO_NOT_WAIT);
        check(bool(static_cast<ParticleGpuRow*>(mapped)), "Identity trail readback unavailable");
        check(mapped[0][3] == 1 && mapped[1][3] == 1 && mapped[6][3] == 4,
              "Ordinary projection overwrote the ribbon packet");
    }
    invalid = values;
    invalid.births[0] = 0;
    invalid.timing[0] = 1e9f;
    gpu.evaluate(context, invalid, &camera, true, &opacity);
    check(gpu.count() == 0, "Empty expired birth window published stale ribbon slots");
    recover();
    context->WaitForIdle();
    std::cout << "Private ribbon capacity, finite input, camera, plane/opacity refusal, cleared "
                 "count and recovery passed\n";
}

inline void execute(DiligentPresentation& presentation, IDeviceContext* context) {
    BufferDesc desc;
    desc.Name = "FORGE trail numeric test readback";
    desc.Size = particle_scene_capacity * mesh_static_instance_rows * sizeof(ParticleGpuRow);
    desc.Usage = USAGE_STAGING;
    desc.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<IBuffer> staging;
    presentation.device()->CreateBuffer(desc, nullptr, &staging);
    check(bool(staging), "Trail numeric staging unavailable");
    cases(presentation, context, staging);
    collapsed_segments(presentation, context, staging);
    lifecycle(presentation, context, staging);
    context->WaitForIdle();
}
} // namespace forge::particle_trail_test
