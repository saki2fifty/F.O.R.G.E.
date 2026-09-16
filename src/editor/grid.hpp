#pragma once
#include "camera.hpp"
namespace forge {
struct GridSettings {
    bool visible = true;
    float spacing = 1;
    bool operator==(const GridSettings&) const = default;
};
// One shared camera supplies meshes, the ground grid and editor projections.
// No orbit-target offsets or snapped grid origins are sent to the shader.
struct GridConstants {
    std::array<float, 4> eye_spacing, right_focal, up_far, forward_near, viewport_fade;
};
inline GridConstants grid_constants(const EditorCamera& camera, unsigned width, unsigned height,
                                    const GridSettings& settings) {
    const auto eye = camera.eye(), right = camera.right(), up = camera.up(),
               forward = camera.forward();
    const float spacing = std::clamp(settings.spacing, .1f, 1000.0f);
    // Horizon fading is angular; only the final half of the view range uses
    // distance clipping. Camera height must not create a moving fade boundary.
    const float fade = EditorCamera::far_plane * .5f;
    return {{eye[0], eye[1], eye[2], spacing},
            {right[0], right[1], right[2], EditorCamera::focal},
            {up[0], up[1], up[2], EditorCamera::far_plane},
            {forward[0], forward[1], forward[2], EditorCamera::near_plane},
            {float(width), float(height), fade, 0}};
}
inline constexpr char grid_vertex_shader[] = R"(
float4 main(uint id : SV_VertexID) : SV_POSITION {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * 2 - 1, 0, 1);
})";
inline constexpr char grid_pixel_shader[] = R"(
cbuffer GridView {
    float4 eyeSpacing;
    float4 rightFocal;
    float4 upFar;
    float4 forwardNear;
    float4 viewportFade;
};
struct GridOutput { float4 color : SV_TARGET; float depth : SV_DEPTH; };
// Pixel coverage for a thin line, with a small smooth reconstruction filter.
// Keep this in framebuffer pixels, independent of world spacing and UI zoom.
float coverage(float pixelDistance) {
    return 1 - smoothstep(-0.0924, 1.0924, pixelDistance);
}
float lines(float2 world, float spacing, float2 footprint) {
    float2 nearest = abs(world - spacing * floor(world / spacing + 0.5));
    return coverage(min(nearest.x / footprint.x, nearest.y / footprint.y));
}
GridOutput main(float4 pixel : SV_POSITION) {
    float2 screen = (2 * pixel.xy - viewportFade.xy) / (rightFocal.w * viewportFade.y);
    float3 ray = forwardNear.xyz + rightFocal.xyz * screen.x - upFar.xyz * screen.y;
    float rayY = (ray.y < 0 ? -1 : 1) * max(abs(ray.y), 0.000001);
    float depth = -eyeSpacing.y / rayY;
    float3 world = eyeSpacing.xyz + ray * depth;
    float3 dx = ddx(world), dy = ddy(world);
    float2 footprint = max(abs(dx.xz) + abs(dy.xz), float2(0.000001, 0.000001));
    // Select density from the horizontal screen-space scale, not the largest
    // ground derivative (which grows sharply near the horizon).
    float resolution = max(4 * abs(dot(dx, rightFocal.xyz)), 0.000001);
    float level = max(0, ceil(log10(resolution / eyeSpacing.w)));
    float spacing = eyeSpacing.w * pow(10, level);
    float previous = level > 0 ? spacing * 0.1 : 0;
    float detail = 1 - saturate((resolution - previous) / (spacing - previous));
    detail = detail * detail * detail;
    float fine = lines(world.xz, spacing, footprint);
    float major = lines(world.xz, spacing * 10, footprint);
    float coarse = lines(world.xz, spacing * 100, footprint);
    // Nested grids share world zero. Their opacity and emphasis have matching
    // endpoints when the active level changes; intersections do not brighten.
    float alpha = max(fine * detail, max(major, coarse));
    float emphasis = max(major * detail, coarse);
    float3 color = lerp(float3(0.24, 0.255, 0.275), float3(0.32, 0.335, 0.355), emphasis);
    float2 axisDistance = abs(world.xz) / footprint;
    float2 axes = float2(coverage(axisDistance.x - 0.1), coverage(axisDistance.y - 0.1));
    float axis = max(axes.x, axes.y);
    if (axis > 0) {
        color = axes.y >= axes.x ? float3(0.80, 0.20, 0.20) : float3(0.20, 0.40, 0.85);
        alpha = max(alpha, axis);
    }
    float distanceToEye = length(world - eyeSpacing.xyz);
    float grazing = 1 - saturate(abs(eyeSpacing.y) / max(distanceToEye, 0.000001));
    float fade = 1 - grazing * grazing * grazing * grazing;
    fade *= 1 - smoothstep(viewportFade.z, viewportFade.z * 2, distanceToEye);
    alpha *= fade;
    clip(depth - forwardNear.w);
    clip(upFar.w - depth);
    clip(abs(ray.y) - 0.000001);
    clip(alpha - 0.001);
    GridOutput output;
    output.color = float4(color, alpha);
    output.depth = (1 - forwardNear.w / depth) * upFar.w / (upFar.w - forwardNear.w);
    return output;
})";
} // namespace forge
