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
    const float fade =
        std::min(EditorCamera::far_plane * .8f, std::max(200 * spacing, std::abs(eye[1]) * 200));
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
float lines(float2 world, float spacing, float2 footprint) {
    float2 distanceToLine = abs(frac(world / spacing + 0.5) - 0.5) * spacing;
    float2 coverage = 1 - smoothstep(footprint * 0.4, footprint * 1.4, distanceToLine);
    coverage *= 1 - smoothstep(0.2, 0.5, footprint / spacing);
    return max(coverage.x, coverage.y);
}
GridOutput main(float4 pixel : SV_POSITION) {
    float2 screen = (2 * pixel.xy - viewportFade.xy) / (rightFocal.w * viewportFade.y);
    float3 ray = forwardNear.xyz + rightFocal.xyz * screen.x - upFar.xyz * screen.y;
    float rayY = (ray.y < 0 ? -1 : 1) * max(abs(ray.y), 0.000001);
    float depth = -eyeSpacing.y / rayY;
    float3 world = eyeSpacing.xyz + ray * depth;
    float2 footprint = max(fwidth(world.xz), float2(0.000001, 0.000001));
    // Adjacent decimal grids share world zero. Only their opacity changes with
    // distance; lines never translate with the camera or orbit target.
    float lod = max(0, log10(max(footprint.x, footprint.y) * 12 / eyeSpacing.w));
    float spacing = eyeSpacing.w * pow(10, floor(lod));
    float minor = lines(world.xz, spacing, footprint);
    float major = lines(world.xz, spacing * 10, footprint);
    float coarse = lines(world.xz, spacing * 100, footprint);
    // Adjacent LOD intervals have identical endpoint weights: a division change
    // must not produce a brightness jump in existing world lines.
    float alpha = lerp(max(minor * 0.32, major * 0.52),
                       max(major * 0.32, coarse * 0.52), frac(lod));
    float3 color = float3(0.38, 0.44, 0.50);
    float2 axes = 1 - smoothstep(footprint * 0.6, footprint * 1.8, abs(world.xz));
    float axis = max(axes.x, axes.y);
    if (axis > 0) {
        float3 axisColor = axes.y >= axes.x ? float3(0.95, 0.25, 0.25) : float3(0.25, 0.50, 1.0);
        color = lerp(color, axisColor, axis);
        alpha = max(alpha, axis * 0.85);
    }
    float fade = 1 - smoothstep(viewportFade.z * 0.45, viewportFade.z, length(world - eyeSpacing.xyz));
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
