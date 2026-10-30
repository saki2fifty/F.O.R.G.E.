#ifndef FORGE_DECALS_FXH
#define FORGE_DECALS_FXH
#if FORGE_DECAL_CAPACITY < 0 || FORGE_DECAL_CAPACITY > 8
#error Unsupported decal receiver capacity
#endif
#if FORGE_DECAL_CAPACITY > 0
// One control row, then thirteen rows per projector. Maps share the existing
// bounded receiver slots; each channel retains its own affine UV transform.
cbuffer ForgeDecals { float4 g_ForgeDecals[1 + 13 * FORGE_DECAL_CAPACITY]; };
#if FORGE_DECAL_CAPACITY > 0
Texture2D<float4> g_ForgeDecalMap0;
#if FORGE_DECAL_NAMED_SAMPLERS
SamplerState g_ForgeDecalSamplers_0;
#endif
#endif
#if FORGE_DECAL_CAPACITY > 1
Texture2D<float4> g_ForgeDecalMap1;
#if FORGE_DECAL_NAMED_SAMPLERS
SamplerState g_ForgeDecalSamplers_1;
#endif
#endif
#if FORGE_DECAL_CAPACITY > 2
Texture2D<float4> g_ForgeDecalMap2;
#if FORGE_DECAL_NAMED_SAMPLERS
SamplerState g_ForgeDecalSamplers_2;
#endif
#endif
#if FORGE_DECAL_CAPACITY > 3
Texture2D<float4> g_ForgeDecalMap3;
#if FORGE_DECAL_NAMED_SAMPLERS
SamplerState g_ForgeDecalSamplers_3;
#endif
#endif
#if FORGE_DECAL_CAPACITY > 4
Texture2D<float4> g_ForgeDecalMap4;
#if FORGE_DECAL_NAMED_SAMPLERS
SamplerState g_ForgeDecalSamplers_4;
#endif
#endif
#if FORGE_DECAL_CAPACITY > 5
Texture2D<float4> g_ForgeDecalMap5;
#if FORGE_DECAL_NAMED_SAMPLERS
SamplerState g_ForgeDecalSamplers_5;
#endif
#endif
#if FORGE_DECAL_CAPACITY > 6
Texture2D<float4> g_ForgeDecalMap6;
#if FORGE_DECAL_NAMED_SAMPLERS
SamplerState g_ForgeDecalSamplers_6;
#endif
#endif
#if FORGE_DECAL_CAPACITY > 7
Texture2D<float4> g_ForgeDecalMap7;
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
        return g_ForgeDecalMap0.SampleGrad(FORGE_DECAL_SAMPLER(0), uv, dx, dy);
#endif
#if FORGE_DECAL_CAPACITY > 1
    case 1:
        return g_ForgeDecalMap1.SampleGrad(FORGE_DECAL_SAMPLER(1), uv, dx, dy);
#endif
#if FORGE_DECAL_CAPACITY > 2
    case 2:
        return g_ForgeDecalMap2.SampleGrad(FORGE_DECAL_SAMPLER(2), uv, dx, dy);
#endif
#if FORGE_DECAL_CAPACITY > 3
    case 3:
        return g_ForgeDecalMap3.SampleGrad(FORGE_DECAL_SAMPLER(3), uv, dx, dy);
#endif
#if FORGE_DECAL_CAPACITY > 4
    case 4:
        return g_ForgeDecalMap4.SampleGrad(FORGE_DECAL_SAMPLER(4), uv, dx, dy);
#endif
#if FORGE_DECAL_CAPACITY > 5
    case 5:
        return g_ForgeDecalMap5.SampleGrad(FORGE_DECAL_SAMPLER(5), uv, dx, dy);
#endif
#if FORGE_DECAL_CAPACITY > 6
    case 6:
        return g_ForgeDecalMap6.SampleGrad(FORGE_DECAL_SAMPLER(6), uv, dx, dy);
#endif
#if FORGE_DECAL_CAPACITY > 7
    case 7:
        return g_ForgeDecalMap7.SampleGrad(FORGE_DECAL_SAMPLER(7), uv, dx, dy);
#endif
    default:
        return 0;
    }
}
bool ForgeProjectedMap(float slot, float4 u, float4 v, float2 uv,
                       float2 dx, float2 dy, out float4 texel,
                       out float2 mappedX, out float2 mappedY) {
    texel = 1;
    float2 mapped = float2(dot(u.xy, uv) + u.z, dot(v.xy, uv) + v.z);
    mappedX = float2(dot(u.xy, dx), dot(v.xy, dx));
    mappedY = float2(dot(u.xy, dy), dot(v.xy, dy));
    if (!all(isfinite(mapped)) || !all(isfinite(mappedX)) || !all(isfinite(mappedY)) ||
        !isfinite(slot) || slot < 0 || slot > FORGE_DECAL_CAPACITY || floor(slot) != slot)
        return false;
    if (slot > 0)
        texel = ForgeProjectedSample((uint)slot - 1, mapped, mappedX, mappedY);
    return all(isfinite(texel));
}
bool ForgeApplyProjectedSurface(float3 position, float3 geometric, float3 dx, float3 dy,
                                inout float3 color, inout float3 normal, inout float roughness) {
    float count = g_ForgeDecals[0].x;
    if (!isfinite(count) || count < 0 || count > FORGE_DECAL_CAPACITY || floor(count) != count)
        return false;
    float3 candidate = color, candidateNormal = normal;
    float candidateRoughness = roughness;
    [loop] for (uint i = 0; i < (uint)count; ++i) {
        uint at = 1 + i * 13;
        float4 x = g_ForgeDecals[at], y = g_ForgeDecals[at + 1], z = g_ForgeDecals[at + 2];
        float3 p = float3(dot(x.xyz, position) + x.w, dot(y.xyz, position) + y.w,
                          dot(z.xyz, position) + z.w);
        if (!all(isfinite(p)))
            return false;
        if (any(abs(p) > .5))
            continue;
        float4 tint = g_ForgeDecals[at + 3], face = g_ForgeDecals[at + 4],
               alpha = g_ForgeDecals[at + 5], influence = g_ForgeDecals[at + 8];
        if (!(alpha.x > face.w))
            return false;
        float fade = smoothstep(face.w, alpha.x, dot(-face.xyz, geometric));
        if (fade == 0 || tint.w == 0)
            continue;
        float2 uv = float2(.5 + p.x, .5 - p.y);
        float2 gradX = float2(dot(x.xyz, dx), -dot(y.xyz, dx));
        float2 gradY = float2(dot(x.xyz, dy), -dot(y.xyz, dy));
        float4 u = g_ForgeDecals[at + 6], v = g_ForgeDecals[at + 7], texel;
        float2 mappedX, mappedY;
        if (!ForgeProjectedMap(u.w, u, v, uv, gradX, gradY, texel, mappedX, mappedY))
            return false;
        float coverage = saturate(texel.a * alpha.y);
        if (alpha.z == 0)
            coverage = 1;
        else if (alpha.z == 1)
            coverage = coverage >= alpha.w ? 1 : 0;
        else if (alpha.z != 2)
            return false;
        coverage *= tint.w * fade;
        candidate = lerp(candidate, texel.rgb * tint.rgb, coverage * influence.x);
        if (coverage * influence.y > 0) {
            float4 nu = g_ForgeDecals[at + 9], nv = g_ForgeDecals[at + 10];
            float4 mapped;
            if (!ForgeProjectedMap(v.w, nu, nv, uv, gradX, gradY, mapped, mappedX, mappedY))
                return false;
            float3 tangentNormal = v.w > 0 ? mapped.xyz * 2 - 1 : float3(0, 0, 1);
            tangentNormal.xy *= influence.w;
            ForgeSurfaceFrame frame = ForgePixelFrame(candidateNormal, 0, 0, dx, dy,
                                                       mappedX, mappedY);
            float3 blended = ForgeUnit(lerp(candidateNormal,
                ForgePerturbNormal(frame, tangentNormal), coverage * influence.y));
            if (any(blended != 0))
                candidateNormal = blended;
        }
        if (coverage * influence.z > 0) {
            float4 ru = g_ForgeDecals[at + 11], rv = g_ForgeDecals[at + 12], mapped;
            float4 info = g_ForgeDecals[at + 9];
            float channel = g_ForgeDecals[at + 10].w;
            if ((channel != 1 && channel != 3) ||
                !ForgeProjectedMap(info.w, ru, rv, uv, gradX, gradY, mapped, mappedX, mappedY))
                return false;
            float projected = saturate(mapped[(uint)channel] * ru.w + rv.w);
            candidateRoughness = lerp(candidateRoughness, projected, coverage * influence.z);
        }
    }
    if (!all(isfinite(candidate)) || !all(isfinite(candidateNormal)) || !isfinite(candidateRoughness))
        return false;
    color = candidate;
    normal = candidateNormal;
    roughness = candidateRoughness;
    return true;
}
#else
bool ForgeApplyProjectedSurface(float3 position, float3 geometric, float3 dx, float3 dy,
                                inout float3 color, inout float3 normal, inout float roughness) {
    return true;
}
#endif
#endif
