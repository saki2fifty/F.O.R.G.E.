#pragma once
#include <array>
#include <cstdint>
#include <forge/identity.hpp>
#include <map>
#include <string>
namespace forge {
using Double3 = std::array<double, 3>;
struct LocalTranslation {
    double x{}, y{}, z{};
    bool operator==(const LocalTranslation&) const = default;
};
struct LocalRotation {
    float x{}, y{}, z{}, w{1};
    bool operator==(const LocalRotation&) const = default;
};
struct LocalScale {
    float x{1}, y{1}, z{1};
    bool operator==(const LocalScale&) const = default;
};
inline constexpr float max_local_scale = 10000;
// Numerical inversion policy, independent of authored scale validity. At this
// condition limit, 64 double epsilons of rounding amplify to less than 2e-6.
inline constexpr double min_inverse_rcond = 1e-8;
// Assembled value ONLY. Never registered as an ECS component. Reads do not own channels.
struct LocalTransform {
    LocalTranslation translation;
    LocalRotation rotation;
    LocalScale scale;
    bool operator==(const LocalTransform&) const = default;
};
// Row-major 3x4 storage; column vectors; point = linear * point + translation.
// Composition parent * local. Engine-owned double math; explicit GPU conversion.
struct AffineTransform {
    std::array<double, 12> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    Double3 point(Double3 value) const;
    Double3 vector(Double3 value) const;
    bool operator==(const AffineTransform&) const = default;
};
enum class SpatialMode { FollowStructure, World, Explicit };
struct SpatialBinding {
    SpatialMode mode{SpatialMode::FollowStructure};
    EntityRef target{};
    bool operator==(const SpatialBinding&) const = default;
};
// Transform module is the only writer. Not authored, inherited or persisted.
struct WorldTransform {
    AffineTransform affine;
    bool resolved{true};
    std::uint64_t revision{};
};
enum class TransformChannel : unsigned { Translation = 1, Rotation = 2, Scale = 4, All = 7 };
enum class ReparentMode { PreserveWorld, KeepLocal };
LocalRotation normalized(LocalRotation value);
LocalScale checked_local_scale(Double3 value);
LocalRotation rotation_from_euler(Double3 degrees);
Double3 rotation_to_euler(LocalRotation value);
LocalRotation rotation_about_axis(Double3 axis, double degrees);
AffineTransform affine_transform(const LocalTransform& value);
AffineTransform operator*(const AffineTransform& a, const AffineTransform& b);
AffineTransform inverse(const AffineTransform& value);
double inverse_reciprocal_condition(const AffineTransform& value);
enum class TransformParity { Singular, Positive, Negative };
TransformParity transform_parity(const AffineTransform& value);
// Direction-equivalent inverse transpose for nonsingular matrices; area/cofactor
// transform at singularity. A zero output normal is explicitly degenerate.
AffineTransform normal_transform(const AffineTransform& value);
// Rejects shear / unrepresentable scale, not reflection or zero. The optional
// previous local value selects continuous scale signs and quaternion hemisphere.
LocalTransform decompose(const AffineTransform& value, const LocalTransform* previous = nullptr);
bool equivalent(LocalTranslation a, LocalTranslation b);
bool equivalent(LocalRotation a, LocalRotation b);
bool equivalent(LocalScale a, LocalScale b);
// Derived input adapters (live ECS / detached transaction) share this evaluator.
// Keys are transient handles in live evaluation, stable draft indices in preparation.
struct EffectiveSpatialParent {
    std::uint64_t entity{};
    bool resolved{true};
};
EffectiveSpatialParent effective_spatial_parent(SpatialMode mode,
                                                std::uint64_t structural_transform,
                                                std::uint64_t explicit_transform);
struct TransformNode {
    LocalTransform local;
    std::uint64_t parent{};
    bool parent_resolved{true};
    bool operator==(const TransformNode&) const = default;
};
struct EvaluatedTransform {
    AffineTransform affine;
    bool resolved{true};
    bool operator==(const EvaluatedTransform&) const = default;
};
class TransformEvaluator {
  public:
    const std::map<std::uint64_t, EvaluatedTransform>&
    evaluate(const std::map<std::uint64_t, TransformNode>& nodes);

  private:
    std::map<std::uint64_t, TransformNode> previous_;
    std::map<std::uint64_t, EvaluatedTransform> values_;
};
std::map<std::uint64_t, EvaluatedTransform>
evaluate_transforms(const std::map<std::uint64_t, TransformNode>& nodes);
} // namespace forge
