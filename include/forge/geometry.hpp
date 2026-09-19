#pragma once
#include <array>
#include <cmath>
#include <forge/primitive_catalog.hpp>
#include <forge/scene.hpp>
#include <limits>
#include <optional>
#include <set>
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
struct PrimitiveVertex {
    Float3 position, normal;
};
inline const std::array<std::vector<PrimitiveVertex>, primitive_count>& primitive_meshes() {
    static const auto meshes = [] {
        std::array<std::vector<PrimitiveVertex>, primitive_count> result;
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
        // Shared bounded procedural generators. Existing four meshes above stay byte-identical.
        auto unit = [](Float3 p) {
            const auto n = std::sqrt(geom_dot(p, p));
            for (auto& v : p)
                v /= n;
            return p;
        };
        auto face = [&](unsigned kind, Float3 a, Float3 b, Float3 c) {
            auto n = geom_cross(geom_sub(b, a), geom_sub(c, a));
            if (geom_dot(n, n) < 1e-14f)
                return;
            n = unit(n);
            triangle(kind, {a, n}, {b, n}, {c, n});
        };
        // Surfaces of revolution, with normals from their radial/vertical profile.
        auto lathe = [&](unsigned kind, const std::vector<Float3>& profile, unsigned sides = 32) {
            for (std::size_t j = 1; j < profile.size(); ++j)
                for (unsigned i = 0; i < sides; ++i) {
                    auto vertex = [&](std::size_t row, unsigned col) {
                        float angle = 6.28318530718f * col / sides;
                        const auto& p = profile[row];
                        Float3 n = unit({profile[j][1] - profile[j - 1][1],
                                         profile[j - 1][0] - profile[j][0], 0});
                        return PrimitiveVertex{
                            {p[0] * std::cos(angle), p[1], p[0] * std::sin(angle)},
                            {n[0] * std::cos(angle), n[1], n[0] * std::sin(angle)}};
                    };
                    auto a = vertex(j - 1, i), b = vertex(j - 1, i + 1), c = vertex(j, i + 1),
                         d = vertex(j, i);
                    auto emit = [&](PrimitiveVertex x, PrimitiveVertex y, PrimitiveVertex z) {
                        auto cross = geom_cross(geom_sub(y.position, x.position),
                                                geom_sub(z.position, x.position));
                        if (geom_dot(cross, cross) < 1e-14f)
                            return;
                        if (geom_dot(cross, x.normal) < 0)
                            std::swap(y, z);
                        triangle(kind, x, y, z);
                    };
                    emit(a, b, c);
                    emit(a, c, d);
                }
        };
        lathe(6, {{0, -.5f, 0}, {.5f, -.5f, 0}, {0, .5f, 0}});
        lathe(7, {{0, -.5f, 0}, {.5f, -.5f, 0}, {.25f, .5f, 0}, {0, .5f, 0}});
        lathe(9, {{.5f, 0, 0}, {0, 0, 0}});
        lathe(10, {{.5f, 0, 0}, {.25f, 0, 0}});
        lathe(20, {{.3f, -.5f, 0}, {.5f, -.5f, 0}, {.5f, .5f, 0}, {.3f, .5f, 0}, {.3f, -.5f, 0}});
        std::vector<Float3> capsule;
        for (int i = 0; i <= 8; ++i) {
            float a = -1.57079632679f + i * 1.57079632679f / 8;
            capsule.push_back({.25f * std::cos(a), -.25f + .25f * std::sin(a), 0});
        }
        for (int i = 0; i <= 8; ++i) {
            float a = i * 1.57079632679f / 8;
            capsule.push_back({.25f * std::cos(a), .25f + .25f * std::sin(a), 0});
        }
        lathe(5, capsule);
        std::vector<Float3> hemi{{0, 0, 0}, {.5f, 0, 0}};
        for (int i = 1; i <= 12; ++i) {
            float a = i * 1.57079632679f / 12;
            hemi.push_back({.5f * std::cos(a), .5f * std::sin(a), 0});
        }
        lathe(18, hemi);
        std::vector<Float3> torus;
        for (int i = 0; i <= 16; ++i) {
            float a = i * 6.28318530718f / 16;
            torus.push_back({.35f + .15f * std::cos(a), .15f * std::sin(a), 0});
        }
        lathe(11, torus);
        face(8, {-.5f, -.5f, 0}, {.5f, -.5f, 0}, {.5f, .5f, 0});
        face(8, {-.5f, -.5f, 0}, {.5f, .5f, 0}, {-.5f, .5f, 0});
        // Convex hulls of small explicit point sets: one flat normal per supporting face.
        auto hull = [&](unsigned kind, const std::vector<Float3>& points) {
            std::set<std::vector<unsigned>> planes;
            for (unsigned a = 0; a < points.size(); ++a)
                for (unsigned b = a + 1; b < points.size(); ++b)
                    for (unsigned c = b + 1; c < points.size(); ++c) {
                        auto n = geom_cross(geom_sub(points[b], points[a]),
                                            geom_sub(points[c], points[a]));
                        if (geom_dot(n, n) < 1e-10f)
                            continue;
                        n = unit(n);
                        bool plus = false, minus = false;
                        std::vector<unsigned> coplanar;
                        for (unsigned i = 0; i < points.size(); ++i) {
                            float d = geom_dot(n, geom_sub(points[i], points[a]));
                            plus |= d > 1e-5f;
                            minus |= d < -1e-5f;
                            if (std::abs(d) <= 1e-5f)
                                coplanar.push_back(i);
                        }
                        if (plus && minus)
                            continue;
                        if (!planes.insert(coplanar).second)
                            continue;
                        if (plus)
                            for (auto& v : n)
                                v = -v;
                        Float3 center{};
                        for (auto i : coplanar)
                            for (unsigned axis = 0; axis < 3; ++axis)
                                center[axis] += points[i][axis] / coplanar.size();
                        auto u = unit(geom_sub(points[coplanar[0]], center)), v = geom_cross(n, u);
                        std::sort(coplanar.begin(), coplanar.end(), [&](unsigned i, unsigned j) {
                            auto x = geom_sub(points[i], center), y = geom_sub(points[j], center);
                            return std::atan2(geom_dot(x, v), geom_dot(x, u)) <
                                   std::atan2(geom_dot(y, v), geom_dot(y, u));
                        });
                        for (unsigned i = 1; i + 1 < coplanar.size(); ++i)
                            face(kind, points[coplanar[0]], points[coplanar[i]],
                                 points[coplanar[i + 1]]);
                    }
        };
        hull(12, {{-.5f, -.5f, -.5f},
                  {.5f, -.5f, -.5f},
                  {.5f, -.5f, .5f},
                  {-.5f, -.5f, .5f},
                  {0, .5f, 0}});
        hull(13, {{.5f, .5f, .5f}, {.5f, -.5f, -.5f}, {-.5f, .5f, -.5f}, {-.5f, -.5f, .5f}});
        hull(14, {{.5f, 0, 0}, {-.5f, 0, 0}, {0, .5f, 0}, {0, -.5f, 0}, {0, 0, .5f}, {0, 0, -.5f}});
        for (auto [kind, sides] : {std::pair{15u, 3u}, std::pair{16u, 6u}}) {
            std::vector<Float3> points;
            for (int sign : {-1, 1})
                for (unsigned i = 0; i < sides; ++i) {
                    float a = i * 6.28318530718f / sides;
                    points.push_back({.5f * std::cos(a), .5f * sign, .5f * std::sin(a)});
                }
            hull(kind, points);
        }
        hull(17, {{-.5f, -.5f, -.5f},
                  {.5f, -.5f, -.5f},
                  {-.5f, -.5f, .5f},
                  {.5f, -.5f, .5f},
                  {-.5f, .5f, .5f},
                  {.5f, .5f, .5f}});
        std::vector<Float3> ico;
        const float phi = 1.61803398875f;
        for (int a : {-1, 1})
            for (int b : {-1, 1})
                for (auto p : {Float3{0, float(a), b * phi}, Float3{float(a), b * phi, 0},
                               Float3{b * phi, 0, float(a)}}) {
                    p = unit(p);
                    for (auto& v : p)
                        v *= .5f;
                    ico.push_back(p);
                }
        hull(19, ico);
        auto low = std::move(result[19]);
        result[19].clear();
        auto spherical = [&](Float3 p) {
            p = unit(p);
            return PrimitiveVertex{{p[0] * .5f, p[1] * .5f, p[2] * .5f}, p};
        };
        for (std::size_t i = 0; i < low.size(); i += 3) {
            auto a = low[i].position, b = low[i + 1].position, c = low[i + 2].position;
            auto midpoint = [&](Float3 p, Float3 q) {
                for (unsigned j = 0; j < 3; ++j)
                    p[j] += q[j];
                return spherical(p);
            };
            auto ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
            triangle(19, spherical(a), ab, ca);
            triangle(19, ab, spherical(b), bc);
            triangle(19, ca, bc, spherical(c));
            triangle(19, ab, bc, ca);
        }
        return result;
    }();
    return meshes;
}
inline std::pair<Float3, Float3> object_bounds(const Json& entity) {
    ObjectTransform transform(entity);
    if (primitive_kind(entity) == no_primitive)
        return {transform.position, transform.position};
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
