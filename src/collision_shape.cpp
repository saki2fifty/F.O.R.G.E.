// clang-format off
#include <Jolt/Jolt.h>
// clang-format on
#include "collision_shape.hpp"
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <algorithm>
#include <cmath>
#include <functional>

namespace forge::physics_detail {
namespace {
JPH::Vec3 vec(const std::array<float, 3>& v) { return {v[0], v[1], v[2]}; }
JPH::Ref<JPH::Shape> checked(JPH::Shape::ShapeResult result) {
    if (result.HasError())
        throw std::runtime_error("Jolt collision preparation: " + std::string(result.GetError()));
    return result.Get();
}
} // namespace
std::shared_ptr<const PreparedCollision> prepare_collision(const CollisionData& data,
                                                           CollisionLimits limits) {
    validate_collision(data, limits);
    auto result = std::make_shared<PreparedCollision>();
    result->registration_lease = registration();
    result->static_only = collision_contains_triangle_mesh(data);
    for (const auto& n : data.nodes)
        result->members.push_back(n.id);
    std::sort(result->members.begin(), result->members.end());
    std::function<JPH::RefConst<JPH::Shape>(std::uint32_t)> build = [&](std::uint32_t index) {
        const auto& n = data.nodes[index];
        JPH::Ref<JPH::Shape> shape;
        switch (n.kind) {
        case CollisionKind::Box:
            shape = checked(JPH::BoxShapeSettings(vec(n.dimensions) * .5f).Create());
            break;
        case CollisionKind::Sphere:
            shape = checked(JPH::SphereShapeSettings(n.dimensions[0]).Create());
            break;
        case CollisionKind::Capsule:
            shape =
                checked(JPH::CapsuleShapeSettings(n.dimensions[1] * .5f, n.dimensions[0]).Create());
            break;
        case CollisionKind::Cylinder:
            shape = checked(
                JPH::CylinderShapeSettings(n.dimensions[1] * .5f, n.dimensions[0]).Create());
            break;
        case CollisionKind::ConvexHull: {
            JPH::Array<JPH::Vec3> points;
            points.reserve(n.vertices.size());
            for (const auto& v : n.vertices)
                points.push_back(vec(v));
            shape = checked(JPH::ConvexHullShapeSettings(points).Create());
            break;
        }
        case CollisionKind::TriangleMesh: {
            JPH::VertexList vertices;
            JPH::IndexedTriangleList triangles;
            vertices.reserve(n.vertices.size());
            triangles.reserve(n.indices.size() / 3);
            for (const auto& v : n.vertices)
                vertices.emplace_back(v[0], v[1], v[2]);
            for (std::size_t i = 0; i < n.indices.size(); i += 3)
                triangles.emplace_back(n.indices[i], n.indices[i + 1], n.indices[i + 2]);
            shape =
                checked(JPH::MeshShapeSettings(std::move(vertices), std::move(triangles)).Create());
            break;
        }
        case CollisionKind::Compound: {
            JPH::StaticCompoundShapeSettings settings;
            auto children = n.children;
            std::sort(children.begin(), children.end(),
                      [&](auto a, auto b) { return data.nodes[a].id < data.nodes[b].id; });
            for (auto child : children) {
                auto child_shape = build(child);
                const auto member = std::lower_bound(result->members.begin(), result->members.end(),
                                                     data.nodes[child].id) -
                                    result->members.begin();
                settings.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), child_shape,
                                  static_cast<JPH::uint32>(member));
            }
            shape = checked(settings.Create());
            break;
        }
        default:
            throw std::runtime_error("Unsupported collision family");
        }
        if (n.kind != CollisionKind::Compound) {
            const auto member =
                std::lower_bound(result->members.begin(), result->members.end(), n.id) -
                result->members.begin();
            shape->SetUserData(static_cast<JPH::uint64>(member) + 1);
        }
        const auto scale = vec(n.scale);
        if (!shape->IsValidScale(scale))
            throw std::runtime_error("Jolt rejects scale for collision member " + n.id.str());
        // No MakeScaleValid/ScaleShape approximation: preserve admitted signed
        // scale exactly, including small differences from unit scale.
        if (n.scale != std::array<float, 3>{1, 1, 1})
            shape = checked(JPH::ScaledShapeSettings(shape, scale).Create());
        if (n.translation != std::array<float, 3>{} ||
            n.rotation != std::array<float, 4>{0, 0, 0, 1}) {
            const auto& q = n.rotation;
            shape = checked(
                JPH::RotatedTranslatedShapeSettings(
                    vec(n.translation), JPH::Quat(q[0], q[1], q[2], q[3]).Normalized(), shape)
                    .Create());
        }
        const auto bounds = shape->GetLocalBounds();
        const auto com = shape->GetCenterOfMass();
        for (unsigned axis = 0; axis < 3; ++axis) {
            const double lo = double(bounds.mMin[axis]) + com[axis];
            const double hi = double(bounds.mMax[axis]) + com[axis];
            if (!std::isfinite(lo) || !std::isfinite(hi) || lo > hi || std::abs(lo) > 1000000 ||
                std::abs(hi) > 1000000)
                throw std::runtime_error("Composed collision bounds exceed supported meter range");
        }
        return JPH::RefConst<JPH::Shape>(shape);
    };
    result->shape = build(data.root);
    return result;
}
} // namespace forge::physics_detail
