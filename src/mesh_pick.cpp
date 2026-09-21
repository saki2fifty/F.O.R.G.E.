#include "mesh_pick.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace forge {
namespace {
using Clip = std::array<double, 4>;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
double plane(const Clip& p, unsigned index) {
    switch (index) {
    case 0:
        return p[3] + p[0];
    case 1:
        return p[3] - p[0];
    case 2:
        return p[3] + p[1];
    case 3:
        return p[3] - p[1];
    case 4:
        return p[2];
    default:
        return p[3] - p[2];
    }
}
Clip mix(const Clip& a, const Clip& b, double t) {
    Clip result;
    for (unsigned i = 0; i < 4; ++i)
        result[i] = std::lerp(a[i], b[i], t);
    return result;
}
Double3 screen(const Clip& p, const CameraView& view) {
    require(p[3] > 0, "Selection projection has no positive W");
    return {view.viewport.x + (p[0] / p[3] + 1) * .5 * view.viewport.width,
            view.viewport.y + (1 - p[1] / p[3]) * .5 * view.viewport.height, p[2] / p[3]};
}
double cross(double ax, double ay, double bx, double by) { return ax * by - ay * bx; }
} // namespace
std::optional<double> pick_mesh_part(const MeshPart& part, const MeshPartPose& part_pose,
                                     const MeshInstancePose& pose, const CameraView& view, double x,
                                     double y, MeshPickBudget& budget, double radius) {
    require(std::isfinite(x) && std::isfinite(y) && std::isfinite(radius) && radius >= 0,
            "Selection coordinates/radius must be finite");
    if (!view.viewport.width || !view.viewport.height || x < view.viewport.x ||
        y < view.viewport.y || x >= double(view.viewport.x) + view.viewport.width ||
        y >= double(view.viewport.y) + view.viewport.height)
        return {};
    const auto* positions = part.find("POSITION");
    require(positions && positions->components == 3, "Selection position stream is unavailable");
    const auto& base = std::get<std::vector<float>>(positions->values);
    require(base.size() == std::uint64_t(part.vertices) * 3,
            "Selection position stream has an invalid extent");
    require(part.morph_targets.size() == pose.morph_weights.size(),
            "Selection morph weights differ from the admitted pose");
    std::array<const std::vector<float>*, 256> deltas{};
    std::array<unsigned, 256> active{};
    unsigned active_count = 0;
    require(part.morph_targets.size() <= deltas.size(), "Selection morph count exceeds profile");
    for (unsigned t = 0; t < part.morph_targets.size(); ++t) {
        require(std::isfinite(pose.morph_weights[t]), "Selection morph weight is nonfinite");
        for (const auto& stream : part.morph_targets[t])
            if (stream.semantic == "POSITION") {
                require(!deltas[t], "Selection morph repeats its position stream");
                require(stream.components == 3, "Selection morph position width is invalid");
                deltas[t] = &std::get<std::vector<float>>(stream.values);
                require(deltas[t]->size() == base.size(), "Selection morph extent is invalid");
                if (pose.morph_weights[t] != 0)
                    active[active_count++] = t;
            }
    }
    const std::vector<std::uint32_t>* joints = nullptr;
    const std::vector<float>* weights = nullptr;
    if (pose.skinned) {
        const auto *j = part.find("JOINTS_0"), *w = part.find("WEIGHTS_0");
        require(part_pose.skin && j && w && j->components == 4 && w->components == 4,
                "Selection skin streams or palette are unavailable");
        joints = &std::get<std::vector<std::uint32_t>>(j->values);
        weights = &std::get<std::vector<float>>(w->values);
        require(joints->size() == std::uint64_t(part.vertices) * 4 &&
                    weights->size() == joints->size(),
                "Selection skin stream extent is invalid");
    }
    auto spend = [&] {
        require(budget.remaining != 0,
                "Mesh selection exceeds the per-click work budget; select from Hierarchy");
        --budget.remaining;
    };
    auto vertex = [&](std::uint32_t index) {
        spend();
        require(index < part.vertices, "Selection index exceeds admitted vertices");
        Double3 local{base[index * 3ull], base[index * 3ull + 1], base[index * 3ull + 2]};
        for (unsigned a = 0; a < active_count; ++a) {
            const auto t = active[a];
            spend();
            for (unsigned c = 0; c < 3; ++c)
                local[c] += double((*deltas[t])[index * 3ull + c]) * pose.morph_weights[t];
        }
        Double3 relative{};
        auto transform = [&](const AffineTransform& matrix, double weight) {
            for (unsigned r = 0; r < 3; ++r) {
                double value = matrix.m[r * 4 + 3] - view.position[r];
                for (unsigned c = 0; c < 3; ++c)
                    value += matrix.m[r * 4 + c] * local[c];
                relative[r] += value * weight;
            }
        };
        if (pose.skinned) {
            double sum = 0;
            for (unsigned k = 0; k < 4; ++k) {
                const double w = weights->at(index * 4ull + k);
                require(std::isfinite(w) && w >= 0, "Selection skin weight is invalid");
                sum += w;
            }
            require(sum > 0 && std::isfinite(sum), "Selection skin weight sum is invalid");
            for (unsigned k = 0; k < 4; ++k) {
                spend();
                transform(part_pose.skin->palette.at(joints->at(index * 4ull + k)),
                          weights->at(index * 4ull + k) / sum);
            }
        } else
            transform(pose.world, 1);
        Double3 camera{};
        for (unsigned c = 0; c < 3; ++c) {
            camera[0] += relative[c] * view.right[c];
            camera[1] += relative[c] * view.up[c];
            camera[2] += relative[c] * view.forward[c];
        }
        Clip clip{};
        for (unsigned r = 0; r < 4; ++r) {
            clip[r] = view.projection[r * 4 + 3];
            for (unsigned c = 0; c < 3; ++c)
                clip[r] += view.projection[r * 4 + c] * camera[c];
            require(std::isfinite(clip[r]), "Selection projection exceeds finite representation");
        }
        return clip;
    };
    std::optional<double> nearest;
    auto hit = [&](double depth) {
        if (std::isfinite(depth) && depth >= 0 && depth <= 1 && (!nearest || depth < *nearest))
            nearest = depth;
    };
    const unsigned stride = part.topology == MeshTopology::Triangles ? 3
                            : part.topology == MeshTopology::Lines   ? 2
                                                                     : 1;
    require(part.indices.size() % stride == 0, "Selection primitive indices are incomplete");
    for (std::size_t i = 0; i < part.indices.size(); i += stride) {
        if (stride == 1) {
            const auto a = vertex(part.indices[i]);
            bool inside = a[3] > 0;
            for (unsigned p = 0; p < 6; ++p)
                inside &= plane(a, p) >= 0;
            if (inside) {
                const auto v = screen(a, view);
                if (std::hypot(v[0] - x, v[1] - y) <= radius)
                    hit(v[2]);
            }
        } else if (stride == 2) {
            auto a = vertex(part.indices[i]), b = vertex(part.indices[i + 1]);
            bool inside = true;
            for (unsigned p = 0; p < 6; ++p) {
                const auto da = plane(a, p), db = plane(b, p);
                if (da < 0 && db < 0) {
                    inside = false;
                    break;
                }
                if ((da < 0) != (db < 0)) {
                    const auto clipped = mix(a, b, da / (da - db));
                    if (da < 0)
                        a = clipped;
                    else
                        b = clipped;
                }
            }
            if (inside && a[3] > 0 && b[3] > 0) {
                const auto u = screen(a, view), v = screen(b, view);
                const auto dx = v[0] - u[0], dy = v[1] - u[1], length = dx * dx + dy * dy;
                const auto t =
                    length > 0 ? std::clamp(((x - u[0]) * dx + (y - u[1]) * dy) / length, 0., 1.)
                               : 0.;
                if (std::hypot(x - std::lerp(u[0], v[0], t), y - std::lerp(u[1], v[1], t)) <=
                    radius)
                    hit(std::lerp(u[2], v[2], t));
            }
        } else {
            // Clip before dividing by W, including triangles crossing the eye.
            std::array<Clip, 12> polygon{}, output{};
            unsigned count = 3;
            for (unsigned k = 0; k < 3; ++k)
                polygon[k] = vertex(part.indices[i + k]);
            for (unsigned p = 0; p < 6 && count; ++p) {
                unsigned next = 0;
                auto append = [&](Clip value) {
                    require(next < output.size(),
                            "Selection clipping exceeded a convex triangle bound");
                    output[next++] = value;
                };
                for (unsigned k = 0; k < count; ++k) {
                    const auto &a = polygon[k], &b = polygon[(k + 1) % count];
                    const auto da = plane(a, p), db = plane(b, p);
                    if (da >= 0)
                        append(a);
                    if ((da < 0) != (db < 0))
                        append(mix(a, b, da / (da - db)));
                }
                polygon = output;
                count = next;
            }
            if (count < 3 || polygon[0][3] <= 0)
                continue;
            const auto a = screen(polygon[0], view);
            for (unsigned k = 1; k + 1 < count; ++k) {
                if (polygon[k][3] <= 0 || polygon[k + 1][3] <= 0)
                    continue;
                const auto b = screen(polygon[k], view), c = screen(polygon[k + 1], view);
                const auto bx = b[0] - a[0], by = b[1] - a[1], cx = c[0] - a[0], cy = c[1] - a[1];
                const auto area = cross(bx, by, cx, cy);
                const auto scale =
                    std::max({std::abs(bx), std::abs(by), std::abs(cx), std::abs(cy)});
                if (std::abs(area) <= 16 * std::numeric_limits<double>::epsilon() * scale * scale)
                    continue;
                const auto u = cross(x - a[0], y - a[1], cx, cy) / area;
                const auto v = cross(bx, by, x - a[0], y - a[1]) / area;
                if (u >= 0 && v >= 0 && u + v <= 1)
                    hit(a[2] + u * (b[2] - a[2]) + v * (c[2] - a[2]));
            }
        }
    }
    return nearest;
}
} // namespace forge
