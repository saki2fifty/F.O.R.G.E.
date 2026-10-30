#ifndef FORGE_DECALS_FXH
#define FORGE_DECALS_FXH
#if FORGE_DECAL_CAPACITY < 0 || FORGE_DECAL_CAPACITY > 8
#error Unsupported decal receiver capacity
#endif
#if FORGE_DECAL_CAPACITY > 0
// One control row, then eight rows per projector: normalized inverse(3),
// color/opacity, forward/end cosine, start cosine/alpha factor/mode/cutoff,
// and affine UV rows with the texture-present flag in the first fourth lane.
cbuffer ForgeDecals { float4 g_ForgeDecals[1 + 8 * FORGE_DECAL_CAPACITY]; };
#if FORGE_DECAL_CAPACITY > 0
Texture2D<float4> g_ForgeDecalColor0;
#if FORGE_DECAL_NAMED_SAMPLERS
SamplerState g_ForgeDecalSamplers_0;
#endif
#endif
#if FORGE_DECAL_CAPACITY > 1
Texture2D<float4> g_ForgeDecalColor1;
#if FORGE_DECAL_NAMED_SAMPLERS
SamplerState g_ForgeDecalSamplers_1;
#endif
#endif
#if FORGE_DECAL_CAPACITY > 2
Texture2D<float4> g_ForgeDecalColor2;
#if FORGE_DECAL_NAMED_SAMPLERS
SamplerState g_ForgeDecalSamplers_2;
#endif
#endif
#if FORGE_DECAL_CAPACITY > 3
Texture2D<float4> g_ForgeDecalColor3;
#if FORGE_DECAL_NAMED_SAMPLERS
SamplerState g_ForgeDecalSamplers_3;
#endif
#endif
#if FORGE_DECAL_CAPACITY > 4
Texture2D<float4> g_ForgeDecalColor4;
#if FORGE_DECAL_NAMED_SAMPLERS
SamplerState g_ForgeDecalSamplers_4;
#endif
#endif
#if FORGE_DECAL_CAPACITY > 5
Texture2D<float4> g_ForgeDecalColor5;
#if FORGE_DECAL_NAMED_SAMPLERS
SamplerState g_ForgeDecalSamplers_5;
#endif
#endif
#if FORGE_DECAL_CAPACITY > 6
Texture2D<float4> g_ForgeDecalColor6;
#if FORGE_DECAL_NAMED_SAMPLERS
SamplerState g_ForgeDecalSamplers_6;
#endif
#endif
#if FORGE_DECAL_CAPACITY > 7
Texture2D<float4> g_ForgeDecalColor7;
#if FORGE_DECAL_NAMED_SAMPLERS
SamplerState g_ForgeDecalSamplers_7;
#endif
#endif
#if !FORGE_DECAL_NAMED_SAMPLERS
SamplerState g_ForgeDecalSamplers[FORGE_DECAL_CAPACITY];
#define FORGE_DECAL_SAMPLER(I) g_ForgeDecalSamplers[I]
#else
#define FORGE_DECAL_SAMPLER(I) g_ForgeDecalSamplers_##I
#endif
float4 ForgeProjectedSample(uint index, float2 uv, float2 dx, float2 dy) {
    switch (index) {
#if FORGE_DECAL_CAPACITY > 0
    case 0:
        return g_ForgeDecalColor0.SampleGrad(FORGE_DECAL_SAMPLER(0), uv, dx, dy);
#endif
#if FORGE_DECAL_CAPACITY > 1
    case 1:
        return g_ForgeDecalColor1.SampleGrad(FORGE_DECAL_SAMPLER(1), uv, dx, dy);
#endif
#if FORGE_DECAL_CAPACITY > 2
    case 2:
        return g_ForgeDecalColor2.SampleGrad(FORGE_DECAL_SAMPLER(2), uv, dx, dy);
#endif
#if FORGE_DECAL_CAPACITY > 3
    case 3:
        return g_ForgeDecalColor3.SampleGrad(FORGE_DECAL_SAMPLER(3), uv, dx, dy);
#endif
#if FORGE_DECAL_CAPACITY > 4
    case 4:
        return g_ForgeDecalColor4.SampleGrad(FORGE_DECAL_SAMPLER(4), uv, dx, dy);
#endif
#if FORGE_DECAL_CAPACITY > 5
    case 5:
        return g_ForgeDecalColor5.SampleGrad(FORGE_DECAL_SAMPLER(5), uv, dx, dy);
#endif
#if FORGE_DECAL_CAPACITY > 6
    case 6:
        return g_ForgeDecalColor6.SampleGrad(FORGE_DECAL_SAMPLER(6), uv, dx, dy);
#endif
#if FORGE_DECAL_CAPACITY > 7
    case 7:
        return g_ForgeDecalColor7.SampleGrad(FORGE_DECAL_SAMPLER(7), uv, dx, dy);
#endif
    default:
        return 0;
    }
}
bool ForgeApplyProjectedColor(float3 position, float3 normal, float3 dx, float3 dy,
                              inout float3 color) {
    float count = g_ForgeDecals[0].x;
    if (!isfinite(count) || count < 0 || count > FORGE_DECAL_CAPACITY || floor(count) != count)
        return false;
    float3 candidate = color;
    [loop] for (uint i = 0; i < (uint)count; ++i) {
        uint at = 1 + i * 8;
        float4 x = g_ForgeDecals[at], y = g_ForgeDecals[at + 1], z = g_ForgeDecals[at + 2];
        float3 p = float3(dot(x.xyz, position) + x.w, dot(y.xyz, position) + y.w,
                          dot(z.xyz, position) + z.w);
        if (!all(isfinite(p)))
            return false;
        if (any(abs(p) > .5))
            continue;
        float4 tint = g_ForgeDecals[at + 3], face = g_ForgeDecals[at + 4],
               alpha = g_ForgeDecals[at + 5];
        if (!(alpha.x > face.w))
            return false;
        float fade = smoothstep(face.w, alpha.x, dot(-face.xyz, normal));
        if (fade == 0 || tint.w == 0)
            continue;
        float4 u = g_ForgeDecals[at + 6], v = g_ForgeDecals[at + 7];
        float4 texel = 1;
        if (u.w != 0) {
            float2 uv = float2(.5 + p.x, .5 - p.y);
            float2 gradX = float2(dot(x.xyz, dx), -dot(y.xyz, dx));
            float2 gradY = float2(dot(x.xyz, dy), -dot(y.xyz, dy));
            float2 mapped = float2(dot(u.xy, uv) + u.z, dot(v.xy, uv) + v.z);
            float2 mappedX = float2(dot(u.xy, gradX), dot(v.xy, gradX));
            float2 mappedY = float2(dot(u.xy, gradY), dot(v.xy, gradY));
            if (!all(isfinite(mapped)) || !all(isfinite(mappedX)) || !all(isfinite(mappedY)))
                return false;
            texel = ForgeProjectedSample(i, mapped, mappedX, mappedY);
        }
        if (!all(isfinite(texel)))
            return false;
        float coverage = saturate(texel.a * alpha.y);
        if (alpha.z == 0)
            coverage = 1;
        else if (alpha.z == 1)
            coverage = coverage >= alpha.w ? 1 : 0;
        else if (alpha.z != 2)
            return false;
        coverage *= tint.w * fade;
        candidate = lerp(candidate, texel.rgb * tint.rgb, coverage);
    }
    if (!all(isfinite(candidate)))
        return false;
    color = candidate;
    return true;
}
#else
bool ForgeApplyProjectedColor(float3 position, float3 normal, float3 dx, float3 dy,
                              inout float3 color) {
    return true;
}
#endif
#endif
