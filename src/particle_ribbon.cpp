#include "particle_ribbon.hpp"
#include "particle_render_instances.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
namespace forge {
namespace {
Double3 subtract(Double3 a, Double3 b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Double3 cross(Double3 a, Double3 b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Double3 unit(Double3 value) {
    const auto length = std::hypot(value[0], value[1], value[2]);
    if (!std::isfinite(length))
        throw std::runtime_error("Particle ribbon direction exceeds finite range");
    if (length)
        for (auto& lane : value)
            lane /= length;
    return value;
}
void finite(Double3 value) {
    for (auto lane : value)
        if (!std::isfinite(lane))
            throw std::runtime_error("Particle ribbon geometry is nonfinite");
}
} // namespace
std::vector<ParticleRibbonSegment> particle_ribbon_segments(const ParticlePathSamples& paths,
                                                            const AffineTransform& emitter,
                                                            const CameraView& camera, double size,
                                                            std::array<float, 3> tint) {
    validate_particle_instance_parameters(size, tint);
    for (auto lane : emitter.m)
        if (!std::isfinite(lane))
            throw std::runtime_error("Particle ribbon emitter is nonfinite");
    (void)particle_billboard_world(AffineTransform{}, camera);
    const auto scale = std::hypot(emitter.m[0], emitter.m[4], emitter.m[8]);
    if (!std::isfinite(scale) || paths.paths.size() > 1024 ||
        paths.points.size() < paths.paths.size() || paths.segment_count() > particle_scene_capacity)
        throw std::runtime_error("Particle ribbon geometry exceeds its bounded profile");
    std::vector<ParticleRibbonSegment> result;
    result.reserve(paths.segment_count());
    std::size_t consumed = 0;
    std::optional<std::uint64_t> previous_birth;
    for (const auto& path : paths.paths) {
        if (path.first != consumed || path.count < 2 ||
            path.count > particle_path_segment_limit + 1 ||
            (previous_birth && path.birth <= *previous_birth) || path.first > paths.points.size() ||
            path.count > paths.points.size() - path.first)
            throw std::runtime_error("Particle ribbon path range is invalid");
        previous_birth = path.birth;
        consumed += path.count;
        const auto points = std::span(paths.points).subspan(path.first, path.count);
        std::array<Double3, particle_path_segment_limit + 1> centers{}, sides{};
        for (std::size_t i = 0; i < points.size(); ++i) {
            if (points[i].birth != path.birth)
                throw std::runtime_error("Particle ribbon point belongs to another birth");
            if (!std::isfinite(points[i].age) || points[i].age < 0 ||
                (i && points[i].age <= points[i - 1].age))
                throw std::runtime_error("Particle ribbon point age is invalid");
            validate_particle_instance_parameters(1, points[i].color);
            if (!std::isfinite(points[i].opacity) || points[i].opacity < 0 || points[i].opacity > 1)
                throw std::runtime_error("Particle ribbon opacity is invalid");
            centers[i] = emitter.point(points[i].position);
            finite(centers[i]);
        }
        for (std::size_t i = 0; i < points.size(); ++i) {
            const auto incoming = i ? unit(subtract(centers[i], centers[i - 1])) : Double3{};
            const auto outgoing =
                i + 1 < points.size() ? unit(subtract(centers[i + 1], centers[i])) : Double3{};
            auto direction = unit(Double3{incoming[0] + outgoing[0], incoming[1] + outgoing[1],
                                          incoming[2] + outgoing[2]});
            if (direction == Double3{})
                direction = outgoing != Double3{} ? outgoing : incoming;
            auto side = unit(cross(direction, camera.forward));
            const auto previous = i ? unit(sides[i - 1]) : camera.right;
            if (side == Double3{})
                side = previous;
            if (std::inner_product(side.begin(), side.end(), previous.begin(), 0.) < 0)
                for (auto& lane : side)
                    lane = -lane;
            const auto width = points[i].size * size * scale;
            if (!std::isfinite(width) || width < 0)
                throw std::runtime_error("Particle ribbon width is invalid");
            for (unsigned axis = 0; axis < 3; ++axis)
                sides[i][axis] = side[axis] * width;
            finite(sides[i]);
        }
        for (std::size_t i = 1; i < points.size(); ++i) {
            const auto delta = subtract(centers[i], centers[i - 1]);
            const auto direction = unit(delta);
            if (direction == Double3{})
                continue;
            auto normal = unit(
                cross(unit(Double3{sides[i - 1][0] + sides[i][0], sides[i - 1][1] + sides[i][1],
                                   sides[i - 1][2] + sides[i][2]}),
                      direction));
            for (auto& lane : normal)
                lane = -lane;
            if (normal == Double3{})
                for (unsigned axis = 0; axis < 3; ++axis)
                    normal[axis] = -camera.forward[axis];
            ParticleRibbonSegment segment;
            segment.birth = path.birth;
            segment.segment = unsigned(i - 1);
            segment.vertices.first = centers[i - 1];
            segment.vertices.last = centers[i];
            segment.vertices.first_side = sides[i - 1];
            segment.vertices.last_side = sides[i];
            segment.vertices.normal = normal;
            for (unsigned axis = 0; axis < 3; ++axis) {
                segment.world.m[4 * axis + 3] =
                    std::midpoint(segment.vertices.first[axis], segment.vertices.last[axis]);
                segment.vertices.color[0][axis] = points[i - 1].color[axis] * tint[axis];
                segment.vertices.color[1][axis] = points[i].color[axis] * tint[axis];
                const std::array corners{
                    segment.vertices.first[axis] - .5 * segment.vertices.first_side[axis],
                    segment.vertices.first[axis] + .5 * segment.vertices.first_side[axis],
                    segment.vertices.last[axis] - .5 * segment.vertices.last_side[axis],
                    segment.vertices.last[axis] + .5 * segment.vertices.last_side[axis]};
                for (double corner : corners)
                    if (!std::isfinite(corner))
                        throw std::runtime_error("Particle ribbon corner is nonfinite");
                // Bounds cover camera-relative float conversion and the shader's
                // endpoint + side*x arithmetic, including large world origins.
                const double padding =
                    8 * std::numeric_limits<float>::epsilon() *
                    (std::abs(segment.vertices.first[axis] - camera.position[axis]) +
                     std::abs(segment.vertices.last[axis] - camera.position[axis]) +
                     std::abs(segment.vertices.first_side[axis]) +
                     std::abs(segment.vertices.last_side[axis]));
                if (!std::isfinite(padding))
                    throw std::runtime_error("Particle ribbon device bounds exceed finite range");
                segment.bounds.minimum[axis] =
                    std::nextafter(*std::min_element(corners.begin(), corners.end()) - padding,
                                   -std::numeric_limits<double>::infinity());
                segment.bounds.maximum[axis] =
                    std::nextafter(*std::max_element(corners.begin(), corners.end()) + padding,
                                   std::numeric_limits<double>::infinity());
            }
            finite(segment.bounds.minimum);
            finite(segment.bounds.maximum);
            segment.vertices.opacity = {float(points[i - 1].opacity), float(points[i].opacity)};
            const auto age_span = points.back().age - points.front().age;
            segment.vertices.texture_v = {(points[i - 1].age - points.front().age) / age_span,
                                          (points[i].age - points.front().age) / age_span};
            result.push_back(segment);
        }
    }
    if (consumed != paths.points.size())
        throw std::runtime_error("Particle ribbon points have no admitted path");
    return result;
}
} // namespace forge
