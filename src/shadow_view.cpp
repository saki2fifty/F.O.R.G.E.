#include "shadow_view.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
namespace forge {
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
double dot(const Double3& a, const Double3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Double3 cross(const Double3& a, const Double3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Double3 unit(Double3 v) {
    const auto size = std::hypot(v[0], v[1], v[2]);
    require(std::isfinite(size) && size > 0, "Shadow view has an undefined direction");
    for (auto& c : v)
        c /= size;
    return v;
}
float gpu(double value) {
    require(std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max(),
            "Shadow transform exceeds finite GPU representation");
    return static_cast<float>(value);
}
void validate(const CameraView& view) {
    for (auto v : view.projection)
        require(std::isfinite(v), "Invalid shadow projection");
    require(view.projection[0] != 0 && view.projection[5] != 0 && view.projection[10] != 0,
            "Shadow extent loses representable projection precision");
}
CameraView view_from_basis(const Double3& origin, const Double3& right, const Double3& up,
                           const Double3& forward, const Diligent::float4x4& projection,
                           unsigned resolution) {
    CameraView result;
    result.position = origin;
    result.right = right;
    result.up = up;
    result.forward = forward;
    // Pinned Diligent BasisFromDirection(..., false) negates its Y basis.
    // Preserve that view parity so the shared draw path selects matching culling.
    result.orientation_reversed =
        (dot(cross(right, up), forward) < 0) != ((projection._11 < 0) != (projection._22 < 0));
    result.viewport = {0, 0, resolution, resolution};
    for (unsigned r = 0; r < 3; ++r) {
        const auto& axis = r == 0 ? right : r == 1 ? up : forward;
        for (unsigned c = 0; c < 3; ++c)
            result.view.m[r * 4 + c] = axis[c];
        result.view.m[r * 4 + 3] = -dot(axis, origin);
    }
    for (unsigned r = 0; r < 4; ++r)
        for (unsigned c = 0; c < 4; ++c)
            result.projection[r * 4 + c] = projection[c][r];
    validate(result);
    return result;
}
} // namespace
std::vector<ShadowView> directional_shadow_views(Diligent::ShadowMapManager& manager,
                                                 const CameraView& camera, const LightView& light,
                                                 const SceneShadows& settings,
                                                 std::span<const RenderBounds> casters) {
    using namespace Diligent;
    static_assert(shadow_cascade_limit == MAX_CASCADES);
    require(light.kind == std::uint32_t(LightKind::Directional),
            "Directional shadow requires a directional light");
    require(manager.GetSRV() && settings.cascades >= 1 && settings.cascades <= MAX_CASCADES,
            "Directional shadow manager is unavailable");
    require(manager.GetSRV()->GetTexture()->GetDesc().ArraySize == settings.cascades,
            "Directional shadow resource differs from selected cascade count");
    float4x4 projection;
    for (unsigned r = 0; r < 4; ++r)
        for (unsigned c = 0; c < 4; ++c)
            projection[r][c] = camera.projection[c * 4 + r];
    const bool perspective = projection._44 == 0;
    const double near_plane = -double(projection._43) / projection._33;
    const double far_plane =
        perspective ? (projection._33 > 1 ? double(projection._43) / (1 - double(projection._33))
                                          : std::numeric_limits<double>::infinity())
                    : (1 - double(projection._43)) / projection._33;
    const double end = std::min(settings.distance, far_plane);
    if (!(end > near_plane))
        return {};
    require(std::isfinite(near_plane) && near_plane >= 0 && std::isfinite(end) &&
                (!perspective ||
                 (near_plane > 0 && end / near_plane <= std::numeric_limits<float>::max())),
            "Directional cascade range exceeds the native fitting profile");
    projection.SetNearFarClipPlanes(gpu(near_plane), gpu(end), false);
    const float4x4 camera_view{gpu(camera.right[0]),
                               gpu(camera.up[0]),
                               gpu(camera.forward[0]),
                               0,
                               gpu(camera.right[1]),
                               gpu(camera.up[1]),
                               gpu(camera.forward[1]),
                               0,
                               gpu(camera.right[2]),
                               gpu(camera.up[2]),
                               gpu(camera.forward[2]),
                               0,
                               0,
                               0,
                               0,
                               1};
    const float4x4 camera_world = camera_view.Transpose();
    const float3 direction{gpu(light.direction[0]), gpu(light.direction[1]),
                           gpu(light.direction[2])};
    ShadowMapManager::DistributeCascadeInfo info;
    info.pCameraView = &camera_view;
    info.pCameraWorld = &camera_world;
    info.pCameraProj = &projection;
    info.pLightDir = &direction;
    info.UseRightHandedLightViewTransform = false;
    info.PackMatrixRowMajor = true;
    info.StabilizeExtents = perspective;
    // Preserve finite requested bounds even when float projection rounding makes
    // GetNearFarClipPlanes report infinity for a very distant far plane.
    std::array<float, shadow_cascade_limit> split_starts{};
    info.AdjustCascadeRange = [&](int cascade, float& start, float& finish) {
        if (cascade == -1) {
            // Native logarithmic splitting always evaluates far/near, even at
            // partition factor zero. Orthographic splits below replace it with
            // uniform camera-depth intervals and support authored near=0.
            start = gpu(perspective ? near_plane : std::max(near_plane, end * .5));
            finish = gpu(end);
        } else if (!perspective) {
            start = std::max(std::numeric_limits<float>::min(),
                             gpu(near_plane + (end - near_plane) * cascade / settings.cascades));
            finish = gpu(near_plane + (end - near_plane) * (cascade + 1) / settings.cascades);
        }
        if (cascade >= 0) {
            split_starts[cascade] = start;
            if (cascade > 0)
                start -= (start - split_starts[cascade - 1]) * .1f;
        }
    };
    std::array<std::array<double, 2>, shadow_cascade_limit> unsnapped{};
    info.AdjustCascadeCenter = [&](int cascade, const float4x4& light_basis, float texel_x,
                                   float texel_y, float& center_x, float& center_y) {
        require(texel_x > 0 && texel_y > 0 && std::isfinite(texel_x) && std::isfinite(texel_y),
                "Directional shadow texel size is unrepresentable");
        auto snap = [&](unsigned axis, double center, float step) {
            double offset = 0;
            for (unsigned row = 0; row < 3; ++row)
                offset += camera.position[row] * light_basis[row][axis];
            require(std::isfinite(offset), "Shadow origin is outside finite light coordinates");
            const auto phase = std::remainder(offset, double(step));
            return gpu(std::round((center + phase) / step) * step - phase);
        };
        // Snap in absolute light space using a double remainder, not a large
        // absolute float position; native fitting itself remains camera-relative.
        center_x = snap(0, unsnapped[cascade][0], texel_x);
        center_y = snap(1, unsnapped[cascade][1], texel_y);
    };
    ShadowMapAttribs attributes{};
    attributes.iFixedFilterSize = 3;
    // The native callback receives an already rounded relative center. Rounding
    // it again in absolute space can jump a texel early. Obtain the native fit
    // without snapping first, then apply one absolute-space rounding operation.
    info.SnapCascades = false;
    manager.DistributeCascades(info, attributes);
    for (unsigned i = 0; i < settings.cascades; ++i) {
        const auto& p = manager.GetCascadeTransform(i).Proj;
        unsnapped[i] = {-double(p._41) / p._11, -double(p._42) / p._22};
    }
    info.SnapCascades = true;
    manager.DistributeCascades(info, attributes);
    const auto& basis = attributes.mWorldToLightView;
    const Double3 right{basis._11, basis._21, basis._31};
    const Double3 up{basis._12, basis._22, basis._32};
    const Double3 forward{basis._13, basis._23, basis._33};
    std::vector<ShadowView> result;
    for (unsigned index = 0; index < settings.cascades; ++index) {
        auto p = manager.GetCascadeTransform(index).Proj;
        double lo = -double(p._43) / p._33, hi = (1 - double(p._43)) / p._33;
        // Native fitting covers receivers. Include off-camera casters whose
        // light-space XY bounds overlap this cascade, without widening its XY.
        for (const auto& bounds : casters) {
            Double3 minimum{INFINITY, INFINITY, INFINITY}, maximum{-INFINITY, -INFINITY, -INFINITY};
            for (unsigned corner = 0; corner < 8; ++corner) {
                Double3 relative;
                for (unsigned axis = 0; axis < 3; ++axis)
                    relative[axis] =
                        ((corner & (1u << axis)) ? bounds.maximum[axis] : bounds.minimum[axis]) -
                        camera.position[axis];
                const Double3 value{dot(relative, right), dot(relative, up),
                                    dot(relative, forward)};
                for (unsigned axis = 0; axis < 3; ++axis) {
                    require(std::isfinite(value[axis]), "Shadow caster extent is nonfinite");
                    minimum[axis] = std::min(minimum[axis], value[axis]);
                    maximum[axis] = std::max(maximum[axis], value[axis]);
                }
            }
            if (maximum[0] * p._11 + p._41 < -1 || minimum[0] * p._11 + p._41 > 1 ||
                maximum[1] * p._22 + p._42 < -1 || minimum[1] * p._22 + p._42 > 1)
                continue;
            lo = std::min(lo, minimum[2]);
            hi = std::max(hi, maximum[2]);
        }
        require(std::isfinite(lo) && std::isfinite(hi) && hi > lo,
                "Directional shadow has no representable depth extent");
        p._33 = gpu(1 / (hi - lo));
        p._43 = gpu(-lo / (hi - lo));
        result.push_back(
            {view_from_basis(camera.position, right, up, forward, p, settings.resolution),
             index == 0 ? near_plane : attributes.Cascades[index].f4StartEndZ.x,
             attributes.Cascades[index].f4StartEndZ.y});
    }
    return result;
}
std::vector<ShadowView> punctual_shadow_views(const LightView& light,
                                              const SceneShadows& settings) {
    require(light.kind == std::uint32_t(LightKind::Point) ||
                light.kind == std::uint32_t(LightKind::Spot),
            "Punctual shadow requires a point or spot light");
    const double far_plane = light.range > 0 ? light.range : settings.distance;
    const double near_plane = std::min(.05, far_plane / 1024);
    Camera config;
    config.near_plane = near_plane;
    config.far_plane = far_plane;
    config.vertical_fov = light.kind == std::uint32_t(LightKind::Point)
                              ? std::numbers::pi / 2
                              : 2 * std::acos(std::clamp(double(light.cosine_outer), -1., 1.));
    const std::array<Double3, 6> faces{
        {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    std::vector<ShadowView> result;
    const auto count = light.kind == std::uint32_t(LightKind::Point) ? 6u : 1u;
    for (unsigned face = 0; face < count; ++face) {
        const auto forward = unit(count == 6 ? faces[face] : light.direction);
        const Double3 reference = std::abs(forward[1]) > .99 ? Double3{0, 0, 1} : Double3{0, 1, 0};
        const auto right = unit(cross(reference, forward));
        const auto up = cross(forward, right);
        AffineTransform world;
        for (unsigned row = 0; row < 3; ++row) {
            world.m[row * 4] = right[row];
            world.m[row * 4 + 1] = up[row];
            world.m[row * 4 + 2] = forward[row];
            world.m[row * 4 + 3] = light.position[row];
        }
        result.push_back({camera_view(config, world, settings.resolution, settings.resolution),
                          near_plane, far_plane});
    }
    return result;
}
} // namespace forge
