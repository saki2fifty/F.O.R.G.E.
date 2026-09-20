#include <algorithm>
#include <cmath>
#include <forge/transform.hpp>
#include <functional>
#include <limits>
#include <stdexcept>
#include <vector>
namespace forge {
namespace {
constexpr double radians = 0.017453292519943295769;
double dot(Double3 a, Double3 b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Double3 cross(Double3 a, Double3 b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
void finite(const AffineTransform& a) {
    for (auto n : a.m)
        if (!std::isfinite(n))
            throw std::runtime_error("Transform is not finite");
}
} // namespace
Double3 AffineTransform::vector(Double3 p) const {
    return {m[0] * p[0] + m[1] * p[1] + m[2] * p[2], m[4] * p[0] + m[5] * p[1] + m[6] * p[2],
            m[8] * p[0] + m[9] * p[1] + m[10] * p[2]};
}
Double3 AffineTransform::point(Double3 p) const {
    auto r = vector(p);
    for (unsigned i = 0; i < 3; ++i)
        r[i] += m[4 * i + 3];
    return r;
}
LocalScale checked_local_scale(Double3 value) {
    float result[3];
    for (unsigned i = 0; i < 3; ++i) {
        if (!std::isfinite(value[i]) || std::abs(value[i]) > max_local_scale)
            throw std::runtime_error(
                "Visual LocalScale must be finite and within -10000 to +10000");
        result[i] = float(value[i]);
        if (value[i] != 0 && result[i] == 0)
            throw std::runtime_error(
                "LocalScale underflows float storage; no zero substitution was made");
        if (result[i] == 0)
            result[i] = 0;
    }
    return {result[0], result[1], result[2]};
}
LocalRotation normalized(LocalRotation q) {
    const double length =
        std::sqrt(double(q.x) * q.x + double(q.y) * q.y + double(q.z) * q.z + double(q.w) * q.w);
    if (!std::isfinite(length) || length < 1e-12)
        throw std::runtime_error("Invalid rotation quaternion");
    const double factor = 1.0 / length;
    return {float(q.x * factor), float(q.y * factor), float(q.z * factor), float(q.w * factor)};
}
LocalRotation rotation_from_euler(Double3 d) {
    for (auto& n : d) {
        if (!std::isfinite(n))
            throw std::runtime_error("Invalid Euler rotation");
        n = std::remainder(n, 360.0) * radians * .5;
    }
    const auto cx = std::cos(d[0]), sx = std::sin(d[0]), cy = std::cos(d[1]), sy = std::sin(d[1]),
               cz = std::cos(d[2]), sz = std::sin(d[2]);
    return normalized({float(sx * cy * cz - cx * sy * sz), float(cx * sy * cz + sx * cy * sz),
                       float(cx * cy * sz - sx * sy * cz), float(cx * cy * cz + sx * sy * sz)});
}
LocalRotation rotation_about_axis(Double3 axis, double degrees) {
    double length = std::sqrt(dot(axis, axis));
    if (!std::isfinite(length) || length < 1e-12 || !std::isfinite(degrees))
        throw std::runtime_error("Invalid rotation axis");
    double a = std::remainder(degrees, 360.0) * radians * .5, s = std::sin(a) / length;
    return normalized(
        {float(axis[0] * s), float(axis[1] * s), float(axis[2] * s), float(std::cos(a))});
}
AffineTransform affine_transform(const LocalTransform& t) {
    const auto q = normalized(t.rotation);
    const double x = q.x, y = q.y, z = q.z, w = q.w;
    // Renormalize in double so matrix orthogonality is not limited by float norm error.
    const double s = 2 / (x * x + y * y + z * z + w * w);
    AffineTransform r{{1 - s * (y * y + z * z), s * (x * y - z * w), s * (x * z + y * w),
                       t.translation.x, s * (x * y + z * w), 1 - s * (x * x + z * z),
                       s * (y * z - x * w), t.translation.y, s * (x * z - y * w),
                       s * (y * z + x * w), 1 - s * (x * x + y * y), t.translation.z}};
    const double scales[] = {t.scale.x, t.scale.y, t.scale.z};
    for (unsigned col = 0; col < 3; ++col) {
        if (!std::isfinite(scales[col]) || std::abs(scales[col]) > max_local_scale)
            throw std::runtime_error(
                "Visual LocalScale must be finite and within -10000 to +10000");
        for (unsigned row = 0; row < 3; ++row)
            r.m[row * 4 + col] *= scales[col];
    }
    finite(r);
    return r;
}
AffineTransform operator*(const AffineTransform& a, const AffineTransform& b) {
    AffineTransform r;
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 4; ++j) {
            r.m[4 * i + j] = j == 3 ? a.m[4 * i + 3] : 0;
            for (unsigned k = 0; k < 3; ++k)
                r.m[4 * i + j] += a.m[4 * i + k] * b.m[4 * k + j];
        }
    finite(r);
    return r;
}
namespace {
struct LinearAnalysis {
    Double3 columns[3]{};
    Double3 cofactors[3]{};
    double magnitude{}, determinant{}, rcond{};
};
LinearAnalysis analyze_linear(const AffineTransform& a) {
    finite(a);
    LinearAnalysis result;
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j)
            result.magnitude = std::max(result.magnitude, std::abs(a.m[4 * i + j]));
    if (result.magnitude == 0)
        return result;
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j)
            result.columns[i][j] = a.m[4 * j + i] / result.magnitude;
    for (unsigned i = 0; i < 3; ++i)
        result.cofactors[i] = cross(result.columns[(i + 1) % 3], result.columns[(i + 2) % 3]);
    result.determinant = dot(result.columns[0], result.cofactors[0]);
    double determinant_sum = 0;
    for (unsigned i = 0; i < 3; ++i) {
        const auto j = (i + 1) % 3, k = (i + 2) % 3;
        determinant_sum += std::abs(result.columns[0][i]) *
                           (std::abs(result.columns[1][j] * result.columns[2][k]) +
                            std::abs(result.columns[1][k] * result.columns[2][j]));
    }
    // A forward-composed rank-two matrix can acquire a tiny determinant through
    // cancellation. Do not randomly alternate its normal orientation each frame.
    if (std::abs(result.determinant) <=
        16 * std::numeric_limits<double>::epsilon() * determinant_sum)
        result.determinant = 0;
    double norm = 0, adjugate_norm = 0;
    for (unsigned i = 0; i < 3; ++i) {
        norm = std::max(norm, std::abs(result.columns[0][i]) + std::abs(result.columns[1][i]) +
                                  std::abs(result.columns[2][i]));
        adjugate_norm = std::max(adjugate_norm, std::abs(result.cofactors[i][0]) +
                                                    std::abs(result.cofactors[i][1]) +
                                                    std::abs(result.cofactors[i][2]));
    }
    if (adjugate_norm != 0)
        result.rcond = std::abs(result.determinant) / (norm * adjugate_norm);
    return result;
}
LocalRotation basis_rotation(const Double3* c) {
    double m[3][3];
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j)
            m[i][j] = c[j][i];
    double q[4]{};
    const double trace = m[0][0] + m[1][1] + m[2][2];
    if (trace > 0) {
        const double s = 2 * std::sqrt(trace + 1);
        q[3] = s / 4;
        q[0] = (m[2][1] - m[1][2]) / s;
        q[1] = (m[0][2] - m[2][0]) / s;
        q[2] = (m[1][0] - m[0][1]) / s;
    } else {
        unsigned i = m[1][1] > m[0][0] ? 1 : 0;
        if (m[2][2] > m[i][i])
            i = 2;
        unsigned j = (i + 1) % 3, k = (i + 2) % 3;
        const double s = 2 * std::sqrt(1 + m[i][i] - m[j][j] - m[k][k]);
        q[i] = s / 4;
        q[3] = (m[k][j] - m[j][k]) / s;
        q[j] = (m[j][i] + m[i][j]) / s;
        q[k] = (m[k][i] + m[i][k]) / s;
    }
    return normalized({float(q[0]), float(q[1]), float(q[2]), float(q[3])});
}
} // namespace

double inverse_reciprocal_condition(const AffineTransform& value) {
    return analyze_linear(value).rcond;
}
TransformParity transform_parity(const AffineTransform& value) {
    const auto determinant = analyze_linear(value).determinant;
    return determinant == 0  ? TransformParity::Singular
           : determinant < 0 ? TransformParity::Negative
                             : TransformParity::Positive;
}
AffineTransform inverse(const AffineTransform& a) {
    const auto linear = analyze_linear(a);
    if (linear.rcond < min_inverse_rcond)
        throw std::runtime_error(
            "Transform inverse unavailable: singular or ill-conditioned linear transform");
    AffineTransform r;
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j)
            r.m[4 * i + j] = (linear.cofactors[i][j] / linear.determinant) / linear.magnitude;
    auto t = r.vector({-a.m[3], -a.m[7], -a.m[11]});
    for (unsigned i = 0; i < 3; ++i)
        r.m[4 * i + 3] = t[i];
    finite(r);
    return r;
}
AffineTransform normal_transform(const AffineTransform& a) {
    const auto linear = analyze_linear(a);
    AffineTransform r;
    r.m.fill(0);
    double magnitude = 0;
    for (const auto& c : linear.cofactors)
        for (auto v : c)
            magnitude = std::max(magnitude, std::abs(v));
    if (magnitude == 0)
        return r;
    // Cofactor(A) = det(A) * inverse-transpose(A). Removing the negative
    // determinant sign preserves outward normal direction for mirrored draws.
    // Rank-two transforms have an oriented area normal but no inverse.
    const auto sign = linear.determinant < 0 ? -1.0 : 1.0;
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j)
            r.m[4 * j + i] = sign * linear.cofactors[i][j] / magnitude;
    return r;
}
LocalTransform decompose(const AffineTransform& a, const LocalTransform* previous) {
    finite(a);
    const auto preferred = previous ? *previous : LocalTransform{};
    const auto preferred_basis = affine_transform({{}, preferred.rotation, {}});
    const double preferred_scale[] = {preferred.scale.x, preferred.scale.y, preferred.scale.z};
    Double3 c[3];
    double lengths[3];
    unsigned rank = 0, nonzero = 0, missing = 0;
    for (unsigned i = 0; i < 3; ++i) {
        c[i] = {a.m[i], a.m[4 + i], a.m[8 + i]};
        lengths[i] = std::hypot(c[i][0], c[i][1], c[i][2]);
        if (!std::isfinite(lengths[i]) || lengths[i] > max_local_scale * (1 + 1e-6))
            throw std::runtime_error("Required local scale is outside supported bounds");
        if (lengths[i] != 0) {
            ++rank;
            nonzero = i;
            for (auto& n : c[i])
                n /= lengths[i];
        } else
            missing = i;
    }
    if (std::abs(dot(c[0], c[1])) > 1e-6 || std::abs(dot(c[0], c[2])) > 1e-6 ||
        std::abs(dot(c[1], c[2])) > 1e-6)
        throw std::runtime_error(
            "Operation requires local shear; authored TRS cannot represent it");
    if (rank == 3) {
        if (dot(c[0], cross(c[1], c[2])) < 0) {
            lengths[0] = -lengths[0];
            for (auto& v : c[0])
                v = -v;
        }
    } else if (rank == 2) {
        c[missing] = cross(c[(missing + 1) % 3], c[(missing + 2) % 3]);
    } else if (rank == 1) {
        // Preserve the previous twist by projecting its next axis into the
        // available perpendicular plane. At a parallel pole choose deterministically.
        const unsigned next = (nonzero + 1) % 3, last = (nonzero + 2) % 3;
        Double3 reference{preferred_basis.m[next], preferred_basis.m[4 + next],
                          preferred_basis.m[8 + next]};
        auto perpendicular = [&](Double3 ref) {
            const auto along = dot(ref, c[nonzero]);
            for (unsigned j = 0; j < 3; ++j)
                ref[j] -= along * c[nonzero][j];
            return ref;
        };
        c[next] = perpendicular(reference);
        double length = std::hypot(c[next][0], c[next][1], c[next][2]);
        if (length < 1e-8) {
            unsigned axis = 0;
            for (unsigned j = 1; j < 3; ++j)
                if (std::abs(c[nonzero][j]) < std::abs(c[nonzero][axis]))
                    axis = j;
            reference = {};
            reference[axis] = 1;
            c[next] = perpendicular(reference);
            length = std::hypot(c[next][0], c[next][1], c[next][2]);
        }
        for (auto& v : c[next])
            v /= length;
        c[last] = cross(c[nonzero], c[next]);
    } else {
        for (unsigned i = 0; i < 3; ++i)
            c[i] = {preferred_basis.m[i], preferred_basis.m[4 + i], preferred_basis.m[8 + i]};
    }
    LocalTransform result;
    unsigned best_mismatches = 4;
    double best_distance = std::numeric_limits<double>::infinity();
    // Four proper bases for a given orthogonal linear transform. Sign continuity
    // wins, then closest quaternion; fixed iteration order settles exact ties.
    for (auto signs :
         {Double3{1, 1, 1}, Double3{1, -1, -1}, Double3{-1, 1, -1}, Double3{-1, -1, 1}}) {
        Double3 basis[3];
        float scales[3];
        unsigned mismatches = 0;
        for (unsigned i = 0; i < 3; ++i) {
            for (unsigned j = 0; j < 3; ++j)
                basis[i][j] = c[i][j] * signs[i];
            const double signed_length = lengths[i] * signs[i];
            // Only roundoff at the existing maximum boundary is tolerated.
            scales[i] = signed_length == 0
                            ? 0
                            : float(std::clamp(signed_length, -double(max_local_scale),
                                               double(max_local_scale)));
            if (scales[i] != 0 && (std::signbit(scales[i]) != std::signbit(preferred_scale[i])))
                ++mismatches;
        }
        auto q = basis_rotation(basis);
        auto p = normalized(preferred.rotation);
        double cosine =
            double(q.x) * p.x + double(q.y) * p.y + double(q.z) * p.z + double(q.w) * p.w;
        if (cosine < 0)
            q = {-q.x, -q.y, -q.z, -q.w};
        const double distance = 1 - std::abs(cosine);
        if (mismatches < best_mismatches ||
            (mismatches == best_mismatches && distance < best_distance)) {
            best_mismatches = mismatches;
            best_distance = distance;
            result = {{a.m[3], a.m[7], a.m[11]}, q, {scales[0], scales[1], scales[2]}};
        }
    }
    const auto check = affine_transform(result);
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j)
            if (std::abs(check.m[4 * i + j] - a.m[4 * i + j]) > 2e-6 * std::abs(lengths[j]))
                throw std::runtime_error("Local TRS reconstruction exceeds tolerance");
    return result;
}
Double3 rotation_to_euler(LocalRotation q) {
    auto a = affine_transform({{}, q, {}});
    const double y = std::asin(std::clamp(-a.m[8], -1.0, 1.0));
    const bool pole = std::abs(std::cos(y)) < 1e-7;
    return {(pole ? 0 : std::atan2(a.m[9], a.m[10])) / radians, y / radians,
            (pole ? std::atan2(-a.m[1], a.m[5]) : std::atan2(a.m[4], a.m[0])) / radians};
}
bool equivalent(LocalTranslation a, LocalTranslation b) {
    return std::abs(a.x - b.x) <= 1e-9 + 1e-14 * std::max(std::abs(a.x), std::abs(b.x)) &&
           std::abs(a.y - b.y) <= 1e-9 + 1e-14 * std::max(std::abs(a.y), std::abs(b.y)) &&
           std::abs(a.z - b.z) <= 1e-9 + 1e-14 * std::max(std::abs(a.z), std::abs(b.z));
}
bool equivalent(LocalRotation a, LocalRotation b) {
    a = normalized(a);
    b = normalized(b);
    double d = double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z + double(a.w) * b.w;
    // Compare normalized orientations, allowing q and -q.
    double aa = double(a.x) * a.x + double(a.y) * a.y + double(a.z) * a.z + double(a.w) * a.w;
    double bb = double(b.x) * b.x + double(b.y) * b.y + double(b.z) * b.z + double(b.w) * b.w;
    return 1 - std::abs(d) / std::sqrt(aa * bb) < 1e-13;
}
bool equivalent(LocalScale a, LocalScale b) {
    return std::abs(a.x - b.x) <= 2e-6 * std::max(std::abs(a.x), std::abs(b.x)) &&
           std::abs(a.y - b.y) <= 2e-6 * std::max(std::abs(a.y), std::abs(b.y)) &&
           std::abs(a.z - b.z) <= 2e-6 * std::max(std::abs(a.z), std::abs(b.z));
}
const std::map<std::uint64_t, EvaluatedTransform>&
TransformEvaluator::evaluate(const std::map<std::uint64_t, TransformNode>& nodes) {
    std::map<std::uint64_t, EvaluatedTransform> result;
    // Iterative parent-first traversal: deep authored hierarchies do not consume call stack.
    for (const auto& [id, node] : nodes) {
        (void)node;
        std::vector<std::uint64_t> path;
        std::map<std::uint64_t, bool> visiting;
        auto current = id;
        while (current && !result.contains(current)) {
            auto it = nodes.find(current);
            if (it == nodes.end())
                break;
            if (!visiting.emplace(current, true).second)
                throw std::runtime_error("Cyclic effective spatial hierarchy");
            path.push_back(current);
            current = it->second.parent;
        }
        while (!path.empty()) {
            auto key = path.back();
            path.pop_back();
            const auto& n = nodes.at(key);
            bool parent_unchanged = !n.parent;
            if (n.parent) {
                auto before = values_.find(n.parent), after = result.find(n.parent);
                parent_unchanged = (before == values_.end() && after == result.end()) ||
                                   (before != values_.end() && after != result.end() &&
                                    before->second == after->second);
            }
            if (previous_.contains(key) && previous_.at(key) == n && values_.contains(key) &&
                parent_unchanged) {
                result.emplace(key, values_.at(key));
                continue;
            }
            EvaluatedTransform value{affine_transform(n.local), n.parent_resolved};
            if (n.parent) {
                auto p = result.find(n.parent);
                if (p == result.end() || !p->second.resolved)
                    value.resolved = false;
                else
                    value.affine = p->second.affine * value.affine;
            }
            result.emplace(key, value);
        }
    }
    previous_ = nodes;
    values_ = std::move(result);
    return values_;
}
std::map<std::uint64_t, EvaluatedTransform>
evaluate_transforms(const std::map<std::uint64_t, TransformNode>& nodes) {
    TransformEvaluator evaluator;
    return evaluator.evaluate(nodes);
}
EffectiveSpatialParent effective_spatial_parent(SpatialMode mode, std::uint64_t structural,
                                                std::uint64_t explicit_target) {
    if (mode == SpatialMode::World)
        return {};
    if (mode == SpatialMode::FollowStructure)
        return {structural, true};
    return {explicit_target, explicit_target != 0};
}
} // namespace forge
