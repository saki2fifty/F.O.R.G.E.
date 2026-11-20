#include "particle_path.hpp"
#include "particle_render_instances.hpp"
#include "particle_ribbon.hpp"
#include "render_sort.hpp"
#include <algorithm>
#include <cmath>
#include <forge/engine_assets.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void reject(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid particle path admitted");
}
ParticleEffectData straight() {
    ParticleEffectData data;
    data.version = 2;
    data.mesh = engine_primitive(0);
    data.emission = ParticleEmission::Burst;
    data.capacity = 64;
    data.burst_count = 3;
    data.spawn_rate = 8;
    data.lifetime = 2;
    data.cone_angle = 0;
    data.speed_min = data.speed_max = 2;
    data.acceleration = {0, 0, 0};
    data.size_start = 1;
    data.size_end = .5;
    return data;
}
void straight_paths() {
    const auto data = straight();
    for (double time :
         {0., .125, 1., std::nextafter(2., 0.), 2., 2.5, std::nextafter(3., 0.), 3., 1e9}) {
        const auto paths = sample_particle_paths(data, time, 17, 1, 8);
        const bool visible = time > 0 && time < 3;
        check(paths.paths.size() == (visible ? 3u : 0u),
              "Burst trail lost birth, tail-at-death or final-tail membership");
        for (const auto& path : paths.paths) {
            check(path.count >= 2 && path.count <= 9 &&
                      path.first + path.count <= paths.points.size(),
                  "Path range exceeds its contiguous bounded points");
            const auto points = std::span(paths.points).subspan(path.first, path.count);
            check(points.front().age == std::max(0., time - 1) &&
                      points.back().age == std::min(time, 2.),
                  "Path endpoints do not retain the original lifetime normalization");
            double previous = -1;
            for (const auto& point : points) {
                check(point.birth == path.birth && point.age > previous && point.age >= 0 &&
                          point.age <= 2 && point.position[0] == 0 &&
                          point.position[1] == 2 * point.age && point.position[2] == 0 &&
                          point.velocity == std::array{0., 2., 0.} &&
                          point.size == std::lerp(1., .5, point.age / 2),
                      "Straight path differs from independent constant-velocity equations");
                previous = point.age;
            }
        }
        check(paths.segment_count() <= 24, "Path sampler exceeded declared segment reservation");
    }
}
void shared_births() {
    auto data = straight();
    data.version = 6;
    data.cone_angle = 65;
    data.speed_min = .2;
    data.speed_max = 3;
    data.acceleration = {.25, -2, .75};
    data.direction = {1, 2, -1};
    data.shape_size = {2, 3, 4};
    data.shape_radius = 3;
    data.size_curve = {{0, .2, false}, {.4, 2, true}, {1, .1, false}};
    data.color_gradient = {
        {0, {1, .2f, .5f}, true}, {.6, {.1f, 1, .4f}, false}, {1, {.5f, .1f, 1}, false}};
    data.opacity_curve = {{0, 0, false}, {.2, 1, true}, {1, .25, false}};
    for (unsigned shape = 0; shape < 4; ++shape)
        for (double drag : {0., 1e-12, .5, 64.})
            for (double time : {.03125, .5, 1.5, 2.5})
                for (unsigned seed : {17u, std::numeric_limits<unsigned>::max()}) {
                    data.shape = ParticleSpawnShape(shape);
                    data.drag = drag;
                    const auto retained = data;
                    const auto paths = sample_particle_paths(data, time, seed, 1, 8);
                    for (const auto& path : paths.paths)
                        for (const auto& point :
                             std::span(paths.points).subspan(path.first, path.count)) {
                            if (point.age == data.lifetime)
                                continue;
                            const auto heads = sample_particle_effect(data, point.age, seed);
                            check(point == heads.at(std::size_t(path.birth)),
                                  "Historical path rerolled a birth or changed motion/appearance");
                        }
                    check(data == retained, "Detached paths mutated authored effect values");
                }
}
void continuous_membership() {
    auto data = straight();
    data.emission = ParticleEmission::Continuous;
    for (double time : {0., std::nextafter(.125, 0.), .125, std::nextafter(.125, 1.), 1., 3.,
                        std::nextafter(1e9, 0.), 1e9}) {
        const auto paths = sample_particle_paths(data, time, 17, 1, 8);
        const auto phase = time * 8;
        const auto last = std::uint64_t(std::floor(phase));
        std::vector<std::uint64_t> expected, actual;
        for (auto ordinal = last > 64 ? last - 64 : 0; ordinal < last; ++ordinal) {
            const auto age = phase - double(ordinal + 1);
            if (age > 0 && age < 24)
                expected.push_back(ordinal);
        }
        for (const auto& path : paths.paths)
            actual.push_back(path.birth);
        check(expected == actual, "Continuous tail membership drifted at an emission boundary");
        check(paths.segment_count() <= 64 * 8, "Continuous path budget lost its reservation");
    }
}
void admission() {
    auto data = straight();
    for (double time : {-1., 1e9 + 1, std::numeric_limits<double>::quiet_NaN()})
        reject([&] { (void)sample_particle_paths(data, time, 17, 1, 8); });
    for (double duration : {0., .0001, 30.1, std::numeric_limits<double>::infinity(),
                            std::numeric_limits<double>::quiet_NaN()})
        reject([&] { (void)sample_particle_paths(data, 1, 17, duration, 8); });
    for (unsigned segments : {0u, 17u})
        reject([&] { (void)sample_particle_paths(data, 1, 17, 1, segments); });
    reject([&] { (void)sample_particle_paths(data, 1, 17, 1, 8, 7); });
    data.capacity = 1024;
    reject([&] { (void)sample_particle_paths(data, 1, 17, 1, 16); });
    data = straight();
    data.emission = ParticleEmission::Continuous;
    data.capacity = 16;
    reject([&] { (void)sample_particle_paths(data, 0, 17, 1, 8); });
    data = straight();
    data.version = 3;
    reject([&] { (void)sample_particle_paths(data, 1, 17, 1, 8); });
}
void trail_values() {
    auto data = straight();
    const auto old = encode_particle_effect(data);
    check(!old.contains("trail"), "Legacy value encoding acquired trail settings");
    data.version = 10;
    data.mesh = engine_primitive(8);
    data.trail = ParticleTrailSettings{1, 8};
    data.opacity_start = .1;
    data.opacity_end = .9;
    const auto encoded = encode_particle_effect(data);
    check(encoded.at("profile") == "cpu_trail1" && decode_particle_effect(encoded) == data &&
              particle_instance_capacity(data) == 512 && particle_uses_sorted_alpha(data) &&
              particle_uses_opacity(data) && !particle_uses_gpu(data),
          "Trail value profile lost codec, appearance or segment reservation");
    const auto paths =
        sample_particle_paths(data, 1, 17, data.trail->duration, data.trail->segments);
    check(paths.segment_count() == 24 && paths.points.front().opacity == .1 &&
              paths.points.back().opacity == .5,
          "Trail values changed original lifetime appearance");
    for (auto segments : {-1, 0, 17}) {
        auto bad = encoded;
        bad["trail"]["segments"] = segments;
        reject([&] { (void)decode_particle_effect(bad); });
    }
    for (auto duration : {0., .0001, 30.1}) {
        auto bad = encoded;
        bad["trail"]["duration"] = duration;
        reject([&] { (void)decode_particle_effect(bad); });
    }
    for (auto key : {"segments", "duration"}) {
        auto bad = encoded;
        bad["trail"].erase(key);
        reject([&] { (void)decode_particle_effect(bad); });
        bad = encoded;
        bad["trail"][key] = true;
        reject([&] { (void)decode_particle_effect(bad); });
    }
    auto bad = encoded;
    bad["trail"]["segments"] = 2.5;
    reject([&] { (void)decode_particle_effect(bad); });
    bad = encoded;
    bad["trail"]["future"] = 1;
    reject([&] { (void)decode_particle_effect(bad); });
    bad = encoded;
    bad["mesh"] = engine_primitive(0);
    reject([&] { (void)decode_particle_effect(bad); });
    bad = old;
    bad["trail"] = encoded.at("trail");
    reject([&] { (void)decode_particle_effect(bad); });
    data.capacity = 1024;
    data.trail->segments = 16;
    reject([&] { (void)particle_instance_capacity(data); });
    data = decode_particle_effect(encoded);
    data.emission = ParticleEmission::Continuous;
    data.capacity = 16;
    reject([&] { validate_particle_effect(data); });
    data = decode_particle_effect(encoded);
    data.version = 6;
    reject([&] { validate_particle_effect(data); });
    data.version = 10;
    data.trail.reset();
    reject([&] { validate_particle_effect(data); });
}
void ribbon_geometry() {
    auto data = straight();
    data.version = 10;
    data.mesh = engine_primitive(8);
    data.trail = ParticleTrailSettings{1, 8};
    data.opacity_curve = {{0, 0, false}, {.4, 1, true}, {1, .25, false}};
    data.acceleration = {.75, -2, .5};
    data.size_curve = {{0, .1, false}, {.5, 2, false}, {1, .25, false}};
    const auto paths = sample_particle_paths(data, 1.5, 17, 1, 8);
    AffineTransform reflected, flattened, large, side, roll;
    reflected.m = {-2, .3, 0, 10, 0, 3, .4, -2, 0, 0, .5, 7};
    flattened.m = {0, .3, 0, 10, 0, 3, .4, -2, 0, 0, .5, 7};
    large = reflected;
    for (auto& lane : large.m)
        lane *= 1e24;
    side.m = {0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0};
    roll.m = {.8, -.6, 0, 0, .6, .8, 0, 0, 0, 0, 1, 0};
    const std::array<float, 3> tint{.25f, .5f, 1};
    for (const auto& world : {AffineTransform{}, reflected, flattened, large})
        for (const auto& camera_world : {AffineTransform{}, side, roll})
            for (unsigned basis : {0u, 1u})
                for (unsigned flips = 0; flips < 4; ++flips)
                    for (double size : {.01, 16.}) {
                        Camera values;
                        values.basis = basis;
                        values.flip_x = bool(flips & 1);
                        values.flip_y = bool(flips & 2);
                        const auto view = camera_view(values, camera_world, 320, 180);
                        const auto geometry =
                            particle_ribbon_segments(paths, world, view, size, tint);
                        const auto envelope = particle_effect_bounds(
                            data, MeshBounds{{-.5f, -.5f, 0}, {.5f, .5f, 0}}, world, size);
                        check(geometry.size() == paths.segment_count(),
                              "Ribbon geometry lost a nonstationary path segment");
                        const auto scale = std::hypot(world.m[0], world.m[4], world.m[8]);
                        const ParticleRibbonSegment* previous = nullptr;
                        for (const auto& segment : geometry) {
                            const auto& path = paths.paths.at(std::size_t(segment.birth));
                            const auto& first = paths.points.at(path.first + segment.segment);
                            const auto& last = paths.points.at(path.first + segment.segment + 1);
                            check(segment.vertices.first == world.point(first.position) &&
                                      segment.vertices.last == world.point(last.position),
                                  "Ribbon geometry changed the emitter-local trajectory");
                            for (const auto& [edge, point] :
                                 {std::pair{segment.vertices.first_side, first},
                                  std::pair{segment.vertices.last_side, last}}) {
                                const auto width = std::hypot(edge[0], edge[1], edge[2]);
                                const auto expected = point.size * size * scale;
                                check(std::abs(width - expected) <= 1e-12 * std::max(1., expected),
                                      "Ribbon width ignored point size or emitter scale");
                                const auto facing = edge[0] * view.forward[0] +
                                                    edge[1] * view.forward[1] +
                                                    edge[2] * view.forward[2];
                                check(std::abs(facing) <= 1e-12 * std::max(1., expected),
                                      "Ribbon side does not lie across the admitted camera view");
                            }
                            if (previous && previous->birth == segment.birth)
                                check(previous->vertices.last == segment.vertices.first &&
                                          previous->vertices.last_side ==
                                              segment.vertices.first_side &&
                                          previous->vertices.color[1] ==
                                              segment.vertices.color[0] &&
                                          previous->vertices.opacity[1] ==
                                              segment.vertices.opacity[0] &&
                                          previous->vertices.texture_v[1] ==
                                              segment.vertices.texture_v[0],
                                      "Neighboring ribbon segments have mismatched joint data");
                            for (unsigned axis = 0; axis < 3; ++axis) {
                                check(segment.vertices.color[0][axis] ==
                                              first.color[axis] * tint[axis] &&
                                          segment.vertices.color[1][axis] ==
                                              last.color[axis] * tint[axis],
                                      "Ribbon endpoint tint lost its lifetime color");
                                const std::array corners{segment.vertices.first[axis] -
                                                             .5 * segment.vertices.first_side[axis],
                                                         segment.vertices.first[axis] +
                                                             .5 * segment.vertices.first_side[axis],
                                                         segment.vertices.last[axis] -
                                                             .5 * segment.vertices.last_side[axis],
                                                         segment.vertices.last[axis] +
                                                             .5 * segment.vertices.last_side[axis]};
                                for (double corner : corners)
                                    check(corner >= segment.bounds.minimum[axis] &&
                                              corner <= segment.bounds.maximum[axis] &&
                                              corner >= envelope.minimum[axis] &&
                                              corner <= envelope.maximum[axis],
                                          "Ribbon bounds clipped an actual shared-edge corner");
                                for (unsigned end = 0; end < 2; ++end)
                                    for (float edge : {-.5f, .5f}) {
                                        const auto center = end ? segment.vertices.last[axis]
                                                                : segment.vertices.first[axis];
                                        const auto side = end ? segment.vertices.last_side[axis]
                                                              : segment.vertices.first_side[axis];
                                        const auto corner =
                                            view.position[axis] +
                                            double(float(center - view.position[axis]) +
                                                   float(side) * edge);
                                        check(corner >= segment.bounds.minimum[axis] &&
                                                  corner <= segment.bounds.maximum[axis],
                                              "Ribbon bounds clipped a device-float corner");
                                    }
                            }
                            check(segment.vertices.texture_v[0] >= 0 &&
                                      segment.vertices.texture_v[1] >
                                          segment.vertices.texture_v[0] &&
                                      segment.vertices.texture_v[1] <= 1,
                                  "Ribbon texture coordinate does not span its path");
                            check(segment.vertices.opacity[0] == float(first.opacity) &&
                                      segment.vertices.opacity[1] == float(last.opacity),
                                  "Ribbon endpoint opacity lost its original lifetime values");
                            previous = &segment;
                        }
                    }
    auto invalid = paths;
    invalid.paths.front().count = 18;
    reject([&] { (void)particle_ribbon_segments(invalid, {}, CameraView{}); });
    invalid = paths;
    invalid.points.front().birth = 99;
    reject([&] { (void)particle_ribbon_segments(invalid, {}, CameraView{}); });
    invalid = paths;
    invalid.points[1].age = invalid.points[0].age;
    reject([&] { (void)particle_ribbon_segments(invalid, {}, CameraView{}); });
    invalid = paths;
    invalid.points.front().opacity = std::numeric_limits<double>::quiet_NaN();
    reject([&] { (void)particle_ribbon_segments(invalid, {}, CameraView{}); });
    auto bad_world = reflected;
    bad_world.m[0] = std::numeric_limits<double>::infinity();
    reject([&] { (void)particle_ribbon_segments(paths, bad_world, CameraView{}); });
}
} // namespace
int main() {
    try {
        straight_paths();
        shared_births();
        continuous_membership();
        admission();
        trail_values();
        ribbon_geometry();
        std::cout
            << "Analytic path membership, shared seeded births, tail decay and budgets pass\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
