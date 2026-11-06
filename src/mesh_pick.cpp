#include "mesh_pick.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace forge {
namespace {
using Clip = std::array<double, 4>;
template <bool Details> struct TriangleCoordinates {};
template <> struct TriangleCoordinates<true> {
    Double3 barycentric{};
};
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
void MeshPickBudget::spend() {
    require(remaining != 0, "Mesh selection exceeds the per-query work budget");
    --remaining;
}
namespace {
const std::vector<float>& selection_positions(const MeshPart& part) {
    const auto* positions = part.find("POSITION");
    require(positions && positions->components == 3, "Selection position stream is unavailable");
    return std::get<std::vector<float>>(positions->values);
}
} // namespace
MeshPickVertices::MeshPickVertices(const MeshPart& part, const MeshPartPose& part_pose,
                                   const MeshInstancePose& pose)
    : part_(part), part_pose_(part_pose), pose_(pose), base_(selection_positions(part)) {
    require(base_.size() == std::uint64_t(part.vertices) * 3,
            "Selection position stream has an invalid extent");
    require(part.morph_targets.size() == pose.morph_weights.size(),
            "Selection morph weights differ from the admitted pose");
    require(part.morph_targets.size() <= deltas_.size(), "Selection morph count exceeds profile");
    for (unsigned t = 0; t < part.morph_targets.size(); ++t) {
        require(std::isfinite(pose.morph_weights[t]), "Selection morph weight is nonfinite");
        for (const auto& stream : part.morph_targets[t])
            if (stream.semantic == "POSITION") {
                require(!deltas_[t], "Selection morph repeats its position stream");
                require(stream.components == 3, "Selection morph position width is invalid");
                deltas_[t] = &std::get<std::vector<float>>(stream.values);
                require(deltas_[t]->size() == base_.size(), "Selection morph extent is invalid");
                if (pose.morph_weights[t] != 0)
                    active_[active_count_++] = t;
            }
    }
    if (pose.skinned) {
        const auto *j = part.find("JOINTS_0"), *w = part.find("WEIGHTS_0");
        require(part_pose.skin && j && w && j->components == 4 && w->components == 4,
                "Selection skin streams or palette are unavailable");
        joints_ = &std::get<std::vector<std::uint32_t>>(j->values);
        weights_ = &std::get<std::vector<float>>(w->values);
        require(joints_->size() == std::uint64_t(part.vertices) * 4 &&
                    weights_->size() == joints_->size(),
                "Selection skin stream extent is invalid");
    }
}
Double3 MeshPickVertices::relative(std::uint32_t index, Double3 origin,
                                   MeshPickBudget& budget) const {
    budget.spend();
    require(index < part_.vertices, "Selection index exceeds admitted vertices");
    Double3 local{base_[index * 3ull], base_[index * 3ull + 1], base_[index * 3ull + 2]};
    for (unsigned a = 0; a < active_count_; ++a) {
        const auto t = active_[a];
        budget.spend();
        for (unsigned c = 0; c < 3; ++c)
            local[c] += double((*deltas_[t])[index * 3ull + c]) * pose_.morph_weights[t];
    }
    Double3 relative{};
    auto transform = [&](const AffineTransform& matrix, double weight) {
        for (unsigned r = 0; r < 3; ++r) {
            double value = matrix.m[r * 4 + 3] - origin[r];
            for (unsigned c = 0; c < 3; ++c)
                value += matrix.m[r * 4 + c] * local[c];
            relative[r] += value * weight;
        }
    };
    if (pose_.skinned) {
        double sum = 0;
        for (unsigned k = 0; k < 4; ++k) {
            const double w = weights_->at(index * 4ull + k);
            require(std::isfinite(w) && w >= 0, "Selection skin weight is invalid");
            sum += w;
        }
        require(sum > 0 && std::isfinite(sum), "Selection skin weight sum is invalid");
        for (unsigned k = 0; k < 4; ++k) {
            budget.spend();
            transform(part_pose_.skin->palette.at(joints_->at(index * 4ull + k)),
                      weights_->at(index * 4ull + k) / sum);
        }
    } else
        transform(pose_.world, 1);
    if (pose_.wind.enabled) {
        const auto delta = wind_displacement(pose_.wind, pose_.world.vector(local)[1]);
        relative[0] += delta[0];
        relative[2] += delta[1];
    }
    for (const auto value : relative)
        require(std::isfinite(value), "Selection position exceeds finite representation");
    return relative;
}
std::array<std::uint32_t, 3> mesh_triangle_indices(const MeshPart& part, std::size_t primitive) {
    const auto corners = part.indices.empty() ? part.vertices : part.indices.size();
    require(part.topology == MeshTopology::Triangles && corners % 3 == 0 && primitive < corners / 3,
            "Selection triangle is outside admitted geometry");
    std::array<std::uint32_t, 3> result;
    for (unsigned k = 0; k < 3; ++k) {
        result[k] = part.indices.empty() ? std::uint32_t(primitive * 3 + k)
                                         : part.indices[primitive * 3 + k];
        require(result[k] < part.vertices, "Selection index exceeds admitted vertices");
    }
    return result;
}
template <bool Details>
std::optional<MeshPickHit> pick_mesh_part_impl(const MeshPart& part, const MeshPartPose& part_pose,
                                               const MeshInstancePose& pose, const CameraView& view,
                                               double x, double y, MeshPickBudget& budget,
                                               double radius) {
    require(std::isfinite(x) && std::isfinite(y) && std::isfinite(radius) && radius >= 0,
            "Selection coordinates/radius must be finite");
    if (!view.viewport.width || !view.viewport.height || x < view.viewport.x ||
        y < view.viewport.y || x >= double(view.viewport.x) + view.viewport.width ||
        y >= double(view.viewport.y) + view.viewport.height)
        return {};
    const MeshPickVertices vertices(part, part_pose, pose);
    auto vertex = [&](std::uint32_t index) {
        const auto relative = vertices.relative(index, view.position, budget);
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
    std::optional<MeshPickHit> nearest;
    auto hit = [&](double depth, std::optional<MeshPickTriangle> triangle = {}) {
        if (std::isfinite(depth) && depth >= 0 && depth <= 1 &&
            (!nearest || depth < nearest->depth))
            nearest = MeshPickHit{depth, triangle};
    };
    const unsigned stride = part.topology == MeshTopology::Triangles ? 3
                            : part.topology == MeshTopology::Lines   ? 2
                                                                     : 1;
    const auto corners = part.indices.empty() ? part.vertices : part.indices.size();
    require(corners % stride == 0, "Selection primitive indices are incomplete");
    const auto index = [&](std::size_t corner) {
        return part.indices.empty() ? std::uint32_t(corner) : part.indices[corner];
    };
    for (std::size_t i = 0; i < corners; i += stride) {
        if (stride == 1) {
            const auto a = vertex(index(i));
            bool inside = a[3] > 0;
            for (unsigned p = 0; p < 6; ++p)
                inside &= plane(a, p) >= 0;
            if (inside) {
                const auto v = screen(a, view);
                if (std::hypot(v[0] - x, v[1] - y) <= radius)
                    hit(v[2]);
            }
        } else if (stride == 2) {
            auto a = vertex(index(i)), b = vertex(index(i + 1));
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
            struct ClippedVertex : TriangleCoordinates<Details> {
                Clip position{};
            };
            std::array<ClippedVertex, 12> polygon{}, output{};
            unsigned count = 3;
            for (unsigned k = 0; k < 3; ++k) {
                polygon[k].position = vertex(index(i + k));
                if constexpr (Details)
                    polygon[k].barycentric[k] = 1;
            }
            for (unsigned p = 0; p < 6 && count; ++p) {
                unsigned next = 0;
                auto append = [&](ClippedVertex value) {
                    require(next < output.size(),
                            "Selection clipping exceeded a convex triangle bound");
                    output[next++] = value;
                };
                for (unsigned k = 0; k < count; ++k) {
                    const auto &a = polygon[k], &b = polygon[(k + 1) % count];
                    const auto da = plane(a.position, p), db = plane(b.position, p);
                    if (da >= 0)
                        append(a);
                    if ((da < 0) != (db < 0)) {
                        const double t = da / (da - db);
                        ClippedVertex clipped;
                        clipped.position = mix(a.position, b.position, t);
                        if constexpr (Details)
                            for (unsigned axis = 0; axis < 3; ++axis)
                                clipped.barycentric[axis] =
                                    std::lerp(a.barycentric[axis], b.barycentric[axis], t);
                        append(clipped);
                    }
                }
                polygon = output;
                count = next;
            }
            if (count < 3 || polygon[0].position[3] <= 0)
                continue;
            const auto a = screen(polygon[0].position, view);
            for (unsigned k = 1; k + 1 < count; ++k) {
                if (polygon[k].position[3] <= 0 || polygon[k + 1].position[3] <= 0)
                    continue;
                const auto b = screen(polygon[k].position, view),
                           c = screen(polygon[k + 1].position, view);
                const auto bx = b[0] - a[0], by = b[1] - a[1], cx = c[0] - a[0], cy = c[1] - a[1];
                const auto area = cross(bx, by, cx, cy);
                const auto scale =
                    std::max({std::abs(bx), std::abs(by), std::abs(cx), std::abs(cy)});
                if (std::abs(area) <= 16 * std::numeric_limits<double>::epsilon() * scale * scale)
                    continue;
                const auto u = cross(x - a[0], y - a[1], cx, cy) / area;
                const auto v = cross(bx, by, x - a[0], y - a[1]) / area;
                if (u >= 0 && v >= 0 && u + v <= 1) {
                    const auto depth = a[2] + u * (b[2] - a[2]) + v * (c[2] - a[2]);
                    if constexpr (Details) {
                        const Double3 weights{(1 - u - v) / polygon[0].position[3],
                                              u / polygon[k].position[3],
                                              v / polygon[k + 1].position[3]};
                        const double sum = weights[0] + weights[1] + weights[2];
                        require(std::isfinite(sum) && sum > 0,
                                "Selection interpolation has invalid perspective weights");
                        MeshPickTriangle triangle;
                        triangle.primitive = i / 3;
                        for (unsigned axis = 0; axis < 3; ++axis)
                            triangle.barycentric[axis] =
                                (weights[0] * polygon[0].barycentric[axis] +
                                 weights[1] * polygon[k].barycentric[axis] +
                                 weights[2] * polygon[k + 1].barycentric[axis]) /
                                sum;
                        hit(depth, triangle);
                    } else
                        hit(depth);
                }
            }
        }
    }
    return nearest;
}
std::optional<MeshPickHit> pick_mesh_part_hit(const MeshPart& part, const MeshPartPose& part_pose,
                                              const MeshInstancePose& pose, const CameraView& view,
                                              double x, double y, MeshPickBudget& budget,
                                              double radius) {
    return pick_mesh_part_impl<true>(part, part_pose, pose, view, x, y, budget, radius);
}
std::optional<double> pick_mesh_part(const MeshPart& part, const MeshPartPose& part_pose,
                                     const MeshInstancePose& pose, const CameraView& view, double x,
                                     double y, MeshPickBudget& budget, double radius) {
    const auto result =
        pick_mesh_part_impl<false>(part, part_pose, pose, view, x, y, budget, radius);
    return result ? std::optional(result->depth) : std::nullopt;
}

} // namespace forge
