#pragma once
#include <array>
#include <cmath>
#include <forge/scene.hpp>
#include <limits>
#include <optional>
namespace forge {
using Float3 = std::array<float, 3>;
inline Float3 geom_sub(Float3 a, Float3 b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
inline float geom_dot(Float3 a, Float3 b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline Float3 geom_cross(Float3 a, Float3 b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
inline Float3 read_xyz(const Json& components, const char* name, Float3 fallback) {
    if (!components.contains(name))
        return fallback;
    const auto& p = components.at(name);
    return {p.at("x"), p.at("y"), p.at("z")};
}
struct ObjectTransform {
    Float3 position{}, scale{1, 1, 1};
    std::array<Float3, 3> axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    AffineTransform affine;
    bool derived = false;
    explicit ObjectTransform(const Json& entity) {
        if (entity.contains("world_affine")) {
            if (!entity.value("spatial_resolved", false))
                throw std::runtime_error("Unresolved world transform");
            affine = {entity.at("world_affine").get<std::array<double, 12>>()};
            derived = true;
            for (unsigned i = 0; i < 3; ++i) {
                position[i] = float(affine.m[4 * i + 3]);
                for (unsigned j = 0; j < 3; ++j)
                    axes[i][j] = float(affine.m[4 * j + i]);
            }
            return;
        }
        const auto& c = entity.at("components");
        if (c.contains("forge.local_translation"))
            throw std::runtime_error("Canonical geometry requires an evaluated WorldTransform");
        position = read_xyz(c, "forge.position", {});
        scale = read_xyz(c, "forge.scale", {1, 1, 1});
        auto angles = read_xyz(c, "forge.rotation", {});
        for (auto& a : angles)
            a = std::remainder(a, 360.0f) * 0.0174532925199433f;
        const float cx = std::cos(angles[0]), sx = std::sin(angles[0]), cy = std::cos(angles[1]),
                    sy = std::sin(angles[1]), cz = std::cos(angles[2]), sz = std::sin(angles[2]);
        axes = {{{cz * cy, sz * cy, -sy},
                 {cz * sy * sx - sz * cx, sz * sy * sx + cz * cx, cy * sx},
                 {cz * sy * cx + sz * sx, sz * sy * cx - cz * sx, cy * cx}}};
    }
    Float3 point(Float3 p) const {
        if (derived) {
            auto p2 = affine.point({p[0], p[1], p[2]});
            return {float(p2[0]), float(p2[1]), float(p2[2])};
        }
        auto value = position;
        for (unsigned i = 0; i < 3; ++i)
            for (unsigned j = 0; j < 3; ++j)
                value[j] += axes[i][j] * scale[i] * p[i];
        return value;
    }
    Float3 inverse_vector(Float3 p) const {
        if (derived) {
            auto p2 = inverse(affine).vector({p[0], p[1], p[2]});
            return {float(p2[0]), float(p2[1]), float(p2[2])};
        }
        return {geom_dot(axes[0], p) / scale[0], geom_dot(axes[1], p) / scale[1],
                geom_dot(axes[2], p) / scale[2]};
    }
};
inline unsigned primitive_kind(const Json& entity) {
    const auto& c = entity.at("components");
    return c.contains("forge.primitive") ? c.at("forge.primitive").at("kind").get<unsigned>() : 0;
}
inline constexpr const char* primitive_names[] = {"Cube", "Sphere", "Cylinder", "Plane"};
struct PrimitiveVertex {
    Float3 position, normal;
};
inline const std::array<std::vector<PrimitiveVertex>, 4>& primitive_meshes() {
    static const auto meshes = [] {
        std::array<std::vector<PrimitiveVertex>, 4> result;
        auto triangle = [&](unsigned kind, PrimitiveVertex a, PrimitiveVertex b,
                            PrimitiveVertex c) {
            result[kind].insert(result[kind].end(), {a, b, c});
        };
        for (unsigned axis = 0; axis < 3; ++axis)
            for (int sign : {-1, 1}) {
                std::array<Float3, 4> p{};
                const int u = (axis + 1) % 3, v = (axis + 2) % 3;
                const float uv[4][2] = {{-.5f, -.5f}, {.5f, -.5f}, {.5f, .5f}, {-.5f, .5f}};
                Float3 n{};
                n[axis] = float(sign);
                for (unsigned i = 0; i < 4; ++i) {
                    p[i][axis] = .5f * sign;
                    p[i][u] = uv[i][0];
                    p[i][v] = uv[i][1];
                }
                triangle(0, {p[0], n}, {p[1], n}, {p[2], n});
                triangle(0, {p[0], n}, {p[2], n}, {p[3], n});
            }
        auto sphere_vertex = [](int latitude, int longitude) {
            const float a = latitude * 3.141592653589793f / 12,
                        b = longitude * 6.283185307179586f / 24;
            Float3 n{std::sin(a) * std::cos(b), std::cos(a), std::sin(a) * std::sin(b)};
            return PrimitiveVertex{{n[0] * .5f, n[1] * .5f, n[2] * .5f}, n};
        };
        for (int y = 0; y < 12; ++y)
            for (int x = 0; x < 24; ++x) {
                auto a = sphere_vertex(y, x), b = sphere_vertex(y, x + 1),
                     c = sphere_vertex(y + 1, x + 1), d = sphere_vertex(y + 1, x);
                triangle(1, a, b, c);
                triangle(1, a, c, d);
            }
        for (int segment = 0; segment < 32; ++segment) {
            const float a = segment * 6.283185307179586f / 32,
                        b = (segment + 1) * 6.283185307179586f / 32;
            Float3 n1{std::cos(a), 0, std::sin(a)}, n2{std::cos(b), 0, std::sin(b)};
            PrimitiveVertex lo1{{n1[0] * .5f, -.5f, n1[2] * .5f}, n1},
                lo2{{n2[0] * .5f, -.5f, n2[2] * .5f}, n2};
            auto hi1 = lo1, hi2 = lo2;
            hi1.position[1] = hi2.position[1] = .5f;
            triangle(2, lo1, lo2, hi2);
            triangle(2, lo1, hi2, hi1);
            for (int sign : {-1, 1}) {
                Float3 normal{0, float(sign), 0};
                triangle(2, {{0, .5f * sign, 0}, normal},
                         {{n1[0] * .5f, .5f * sign, n1[2] * .5f}, normal},
                         {{n2[0] * .5f, .5f * sign, n2[2] * .5f}, normal});
            }
        }
        const Float3 up{0, 1, 0};
        triangle(3, {{-.5f, 0, -.5f}, up}, {{.5f, 0, -.5f}, up}, {{.5f, 0, .5f}, up});
        triangle(3, {{-.5f, 0, -.5f}, up}, {{.5f, 0, .5f}, up}, {{-.5f, 0, .5f}, up});
        return result;
    }();
    return meshes;
}
inline std::pair<Float3, Float3> object_bounds(const Json& entity) {
    ObjectTransform transform(entity);
    Float3 lo, hi;
    lo.fill(std::numeric_limits<float>::max());
    hi.fill(std::numeric_limits<float>::lowest());
    for (const auto& vertex : primitive_meshes().at(primitive_kind(entity))) {
        const auto p = transform.point(vertex.position);
        for (unsigned i = 0; i < 3; ++i) {
            lo[i] = std::min(lo[i], p[i]);
            hi[i] = std::max(hi[i], p[i]);
        }
    }
    return {lo, hi};
}
inline std::optional<float> object_hit(const Json& entity, Float3 eye, Float3 ray, float clip_near,
                                       float limit) {
    const ObjectTransform transform(entity);
    eye = transform.inverse_vector(geom_sub(eye, transform.position));
    ray = transform.inverse_vector(ray);
    float enter = clip_near, leave = limit;
    for (unsigned axis = 0; axis < 3; ++axis) {
        if (std::abs(ray[axis]) < 1e-12f) {
            if (std::abs(eye[axis]) > .5f)
                return {};
        } else {
            float a = (-.5f - eye[axis]) / ray[axis], b = (.5f - eye[axis]) / ray[axis];
            if (a > b)
                std::swap(a, b);
            enter = std::max(enter, a);
            leave = std::min(leave, b);
        }
    }
    if (enter > leave)
        return {};
    std::optional<float> result;
    const auto& mesh = primitive_meshes().at(primitive_kind(entity));
    for (std::size_t i = 0; i < mesh.size(); i += 3) {
        const auto edge1 = geom_sub(mesh[i + 1].position, mesh[i].position),
                   edge2 = geom_sub(mesh[i + 2].position, mesh[i].position);
        const auto p = geom_cross(ray, edge2);
        const float determinant = geom_dot(edge1, p);
        if (std::abs(determinant) < 1e-12f)
            continue;
        const auto delta = geom_sub(eye, mesh[i].position);
        const float u = geom_dot(delta, p) / determinant;
        if (u < -1e-5f || u > 1.00001f)
            continue;
        const auto q = geom_cross(delta, edge1);
        const float v = geom_dot(ray, q) / determinant;
        if (v < -1e-5f || u + v > 1.00001f)
            continue;
        const float t = geom_dot(edge2, q) / determinant;
        if (t >= clip_near && t < limit) {
            limit = t;
            result = t;
        }
    }
    return result;
}
// Materialize only supported inherited render components into a temporary preview.
// Authored documents retain their explicit overrides and unknown fields.
} // namespace forge
