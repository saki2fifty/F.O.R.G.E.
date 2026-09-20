#include <algorithm>
#include <cmath>
#include <forge/render_view.hpp>
#include <limits>
#include <numbers>
#include <stdexcept>
namespace forge {
namespace {
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
bool finite(double v) { return std::isfinite(v); }
double dot(Double3 a, Double3 b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Double3 cross(Double3 a, Double3 b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Double3 unit(Double3 v) {
    const auto size = std::hypot(v[0], v[1], v[2]);
    require(finite(size) && size > 0, "Camera/light direction is collapsed or nonfinite");
    for (auto& x : v)
        x /= size;
    return v;
}
float gpu(double v) {
    require(finite(v) && std::abs(v) <= std::numeric_limits<float>::max(),
            "Camera/light value exceeds finite GPU float representation");
    const auto out = static_cast<float>(v);
    // The selected FXC5.1 profile flushes float32 subnormals during shader math.
    // In particular a tiny positive range must not turn into "unbounded", or a
    // perspective near coefficient into zero. This is not a LocalScale limit.
    require(v == 0 || std::abs(out) >= std::numeric_limits<float>::min(),
            "Camera/light value underflows GPU arithmetic representation");
    return out;
}
void validate_affine(const AffineTransform& a) {
    for (auto x : a.m)
        require(finite(x), "Nonfinite camera/light world transform");
}
} // namespace
void validate_camera(const Camera& c) {
    require(c.projection <= std::uint32_t(CameraProjection::Orthographic) &&
                c.basis <= std::uint32_t(ViewBasis::GltfNegativeZ),
            "Unknown camera projection/basis");
    require(finite(c.vertical_fov) && c.vertical_fov > 0 && c.vertical_fov < std::numbers::pi,
            "Camera vertical field of view must be between zero and pi radians");
    require(finite(c.orthographic_height) && c.orthographic_height > 0 &&
                finite(c.orthographic_width) && c.orthographic_width >= 0,
            "Camera orthographic height must be positive; width zero selects automatic aspect");
    require(finite(c.near_plane) && c.near_plane >= 0 && finite(c.far_plane) && c.far_plane > 0 &&
                (c.infinite_far || c.far_plane > c.near_plane),
            "Camera needs valid near/far planes; finite far must be greater than near");
    if (c.projection == std::uint32_t(CameraProjection::Perspective))
        require(c.near_plane > 0, "Perspective camera near plane must be positive");
    else
        require(!c.infinite_far, "Infinite far is only supported for perspective cameras");
    require(finite(c.aspect) && c.aspect >= 0,
            "Camera aspect must be positive or zero for automatic");
    require(finite(c.viewport_x) && finite(c.viewport_y) && finite(c.viewport_width) &&
                finite(c.viewport_height) && c.viewport_x >= 0 && c.viewport_y >= 0 &&
                c.viewport_width > 0 && c.viewport_height > 0 &&
                c.viewport_x + c.viewport_width <= 1 && c.viewport_y + c.viewport_height <= 1,
            "Camera viewport must be a nonempty rectangle inside the target");
    for (auto channel : {c.background_r, c.background_g, c.background_b, c.background_a})
        require(finite(channel) && channel >= 0 && channel <= 1,
                "Camera background must contain linear channels from zero to one");
}
void validate_light(const Light& l) {
    require(l.kind <= std::uint32_t(LightKind::Spot) &&
                l.basis <= std::uint32_t(ViewBasis::GltfNegativeZ),
            "Unknown light kind/basis");
    for (auto channel : {l.color_r, l.color_g, l.color_b})
        require(finite(channel) && channel >= 0 && channel <= 1,
                "Light color must be linear RGB in [0,1]");
    require(finite(l.intensity) && l.intensity >= 0 && finite(l.range) && l.range >= 0,
            "Light intensity/range must be nonnegative and finite; range zero is unbounded");
    require(finite(l.inner_cone) && finite(l.outer_cone) && l.inner_cone >= 0 &&
                l.inner_cone < l.outer_cone && l.outer_cone <= std::numbers::pi / 2,
            "Light cone angles require 0 <= inner < outer <= pi/2 radians");
    require(finite(l.shadow_bias) && l.shadow_bias >= 0 && finite(l.shadow_normal_bias) &&
                l.shadow_normal_bias >= 0,
            "Shadow biases must be nonnegative and finite");
}
CameraView camera_view(const Camera& c, const AffineTransform& a, std::uint32_t width,
                       std::uint32_t height) {
    validate_camera(c);
    validate_affine(a);
    require(width && height, "Camera target has zero extent");
    CameraView result{};
    result.position = a.point({0, 0, 0});
    result.right = unit(a.vector({1, 0, 0}));
    result.up = unit(a.vector({0, 1, 0}));
    auto forward = unit(a.vector({0, 0, 1}));
    // Exact glTF view-matrix contract: proper normalized orientation. Explicit
    // orthogonality additionally prevents a near-unit determinant hiding shear.
    constexpr double tolerance = 1e-5;
    require(std::abs(dot(cross(result.right, result.up), forward) - 1) <= tolerance &&
                std::abs(dot(result.right, result.up)) <= tolerance &&
                std::abs(dot(result.right, forward)) <= tolerance &&
                std::abs(dot(result.up, forward)) <= tolerance,
            "Camera orientation is reflected or sheared; a proper orthogonal frame is required");
    if (c.basis == std::uint32_t(ViewBasis::GltfNegativeZ))
        for (auto& x : forward)
            x = -x;
    result.forward = forward;
    const Double3 rows[]{result.right, result.up, result.forward};
    for (unsigned i = 0; i < 3; ++i) {
        for (unsigned j = 0; j < 3; ++j)
            result.view.m[i * 4 + j] = rows[i][j];
        result.view.m[i * 4 + 3] = -dot(rows[i], result.position);
    }
    validate_affine(result.view);
    auto& viewport = result.viewport;
    viewport.x = std::uint32_t(std::floor(c.viewport_x * width));
    viewport.y = std::uint32_t(std::floor(c.viewport_y * height));
    viewport.width =
        std::uint32_t(std::floor((c.viewport_x + c.viewport_width) * width)) - viewport.x;
    viewport.height =
        std::uint32_t(std::floor((c.viewport_y + c.viewport_height) * height)) - viewport.y;
    require(viewport.width && viewport.height, "Camera viewport is smaller than one pixel");
    const bool ortho = c.projection == std::uint32_t(CameraProjection::Orthographic);
    const double fixed_aspect =
        ortho && c.orthographic_width > 0 ? c.orthographic_width / c.orthographic_height : c.aspect;
    require(finite(fixed_aspect) && (!(ortho && c.orthographic_width > 0) || fixed_aspect > 0),
            "Camera aspect is not representable");
    if (fixed_aspect > 0) {
        const double actual = double(viewport.width) / viewport.height;
        if (actual > fixed_aspect) {
            const auto fit = std::uint32_t(std::floor(viewport.height * fixed_aspect));
            require(fit && fit <= viewport.width, "Camera fitted width is smaller than one pixel");
            viewport.x += (viewport.width - fit) / 2;
            viewport.width = fit;
        } else {
            const auto fit = std::uint32_t(std::floor(viewport.width / fixed_aspect));
            require(fit && fit <= viewport.height,
                    "Camera fitted height is smaller than one pixel");
            viewport.y += (viewport.height - fit) / 2;
            viewport.height = fit;
        }
    }
    const double aspect =
        fixed_aspect > 0 ? fixed_aspect : double(viewport.width) / viewport.height;
    std::array<double, 16> p{};
    if (ortho) {
        p[5] = 2 / c.orthographic_height;
        p[0] = p[5] / aspect;
        p[10] = 1 / (c.far_plane - c.near_plane);
        p[11] = -c.near_plane * p[10];
        p[15] = 1;
    } else {
        p[5] = 1 / std::tan(c.vertical_fov / 2);
        p[0] = p[5] / aspect;
        p[10] = c.infinite_far ? 1 : 1 / (1 - c.near_plane / c.far_plane);
        p[11] = -c.near_plane * p[10];
        p[14] = 1;
    }
    if (c.flip_x)
        p[0] = -p[0];
    if (c.flip_y)
        p[5] = -p[5];
    for (unsigned i = 0; i < 16; ++i)
        result.projection[i] = gpu(p[i]);
    result.orientation_reversed =
        (c.basis == std::uint32_t(ViewBasis::GltfNegativeZ)) != (c.flip_x != c.flip_y);
    return result;
}
LightView light_view(const Light& l, const AffineTransform& a) {
    validate_light(l);
    validate_affine(a);
    LightView result{};
    result.position = a.point({0, 0, 0});
    if (l.kind != std::uint32_t(LightKind::Point))
        result.direction =
            unit(a.vector({0, 0, l.basis == std::uint32_t(ViewBasis::GltfNegativeZ) ? -1. : 1.}));
    result.color = {l.color_r, l.color_g, l.color_b};
    result.intensity = gpu(l.intensity);
    result.range = gpu(l.range);
    result.cosine_inner = gpu(std::cos(l.inner_cone));
    result.cosine_outer = gpu(std::cos(l.outer_cone));
    if (l.kind == std::uint32_t(LightKind::Spot))
        require(result.cosine_inner > result.cosine_outer,
                "Spot cone angles are indistinguishable at GPU float precision");
    result.kind = l.kind;
    result.layers = l.layers;
    result.cast_shadows = l.cast_shadows;
    result.shadow_bias = l.shadow_bias;
    result.shadow_normal_bias = l.shadow_normal_bias;
    return result;
}
} // namespace forge
