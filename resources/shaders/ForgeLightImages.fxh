#ifndef FORGE_LIGHT_IMAGES_FXH
#define FORGE_LIGHT_IMAGES_FXH
// The existing projected-image sampler/map slots are shared with surface decals.
// One camera-relative anchor and three coordinate rows per ordinary light.
cbuffer ForgeLightImages { float4 g_ForgeLightImages[256]; };
#if FORGE_DECAL_CAPACITY > 0
TextureCube<float4> g_ForgeLightCube0;
#endif
#if FORGE_DECAL_CAPACITY > 1
TextureCube<float4> g_ForgeLightCube1;
#endif
#if FORGE_DECAL_CAPACITY > 2
TextureCube<float4> g_ForgeLightCube2;
#endif
#if FORGE_DECAL_CAPACITY > 3
TextureCube<float4> g_ForgeLightCube3;
#endif
#if FORGE_DECAL_CAPACITY > 4
TextureCube<float4> g_ForgeLightCube4;
#endif
#if FORGE_DECAL_CAPACITY > 5
TextureCube<float4> g_ForgeLightCube5;
#endif
#if FORGE_DECAL_CAPACITY > 6
TextureCube<float4> g_ForgeLightCube6;
#endif
#if FORGE_DECAL_CAPACITY > 7
TextureCube<float4> g_ForgeLightCube7;
#endif
float3 ForgeLightCubeSample(uint index, float3 ray, float3 dx, float3 dy) {
    switch (index) {
#if FORGE_DECAL_CAPACITY > 0
    case 0: return g_ForgeLightCube0.SampleGrad(FORGE_DECAL_SAMPLER(0), ray, dx, dy).rgb;
#endif
#if FORGE_DECAL_CAPACITY > 1
    case 1: return g_ForgeLightCube1.SampleGrad(FORGE_DECAL_SAMPLER(1), ray, dx, dy).rgb;
#endif
#if FORGE_DECAL_CAPACITY > 2
    case 2: return g_ForgeLightCube2.SampleGrad(FORGE_DECAL_SAMPLER(2), ray, dx, dy).rgb;
#endif
#if FORGE_DECAL_CAPACITY > 3
    case 3: return g_ForgeLightCube3.SampleGrad(FORGE_DECAL_SAMPLER(3), ray, dx, dy).rgb;
#endif
#if FORGE_DECAL_CAPACITY > 4
    case 4: return g_ForgeLightCube4.SampleGrad(FORGE_DECAL_SAMPLER(4), ray, dx, dy).rgb;
#endif
#if FORGE_DECAL_CAPACITY > 5
    case 5: return g_ForgeLightCube5.SampleGrad(FORGE_DECAL_SAMPLER(5), ray, dx, dy).rgb;
#endif
#if FORGE_DECAL_CAPACITY > 6
    case 6: return g_ForgeLightCube6.SampleGrad(FORGE_DECAL_SAMPLER(6), ray, dx, dy).rgb;
#endif
#if FORGE_DECAL_CAPACITY > 7
    case 7: return g_ForgeLightCube7.SampleGrad(FORGE_DECAL_SAMPLER(7), ray, dx, dy).rgb;
#endif
    default: return 1;
    }
}
// Derivatives are supplied before the per-light branches. Cube gradients stay in
// unnormalized direction space; spot gradients differentiate its perspective divide.
bool ForgeLightImage(uint index, float3 position, float3 dx, float3 dy, out float3 image) {
    image = 1;
    uint row = min(index, 63u) * 4;
    float slot = g_ForgeLightImages[row].w;
    bool valid = isfinite(slot) && slot >= 0 && slot <= FORGE_DECAL_CAPACITY && floor(slot) == slot;
    if (valid && slot > 0) {
        float4 a = g_ForgeLightImages[row + 1];
        float4 b = g_ForgeLightImages[row + 2];
        float4 c = g_ForgeLightImages[row + 3];
        float3 delta = position - g_ForgeLightImages[row].xyz;
        float3 q = float3(dot(a.xyz, delta), dot(b.xyz, delta), dot(c.xyz, delta));
        float3 qx = float3(dot(a.xyz, dx), dot(b.xyz, dx), dot(c.xyz, dx));
        float3 qy = float3(dot(a.xyz, dy), dot(b.xyz, dy), dot(c.xyz, dy));
        valid = all(isfinite(q)) && all(isfinite(qx)) && all(isfinite(qy)) &&
                (a.w == 0 || a.w == 1 || a.w == 2) && (b.w == 0 || b.w == 1);
        float3 sample = 1;
        uint selected = (uint)slot - 1;
        if (a.w == 1) {
            // A point exactly at the source has no direction; ordinary punctual
            // lighting rejects its undefined ray too. Keep flattened sampling safe.
            float length2 = dot(q, q);
            bool rayValid = isfinite(length2) && length2 >= 1.175494351e-38;
            bool coordinates = valid && rayValid;
            sample = ForgeLightCubeSample(selected, coordinates ? q : float3(0, 0, 1),
                                           coordinates ? qx : 0, coordinates ? qy : 0);
            valid = valid && rayValid;
        } else {
            bool spot = a.w == 2;
            bool front = !spot || q.z >= 1.175494351e-38;
            float inverse = spot && front ? 1 / q.z : 1;
            float2 xy = float2(q.x, -q.y);
            float2 uv = .5 + xy * inverse;
            float2 ux = (float2(qx.x, -qx.y) - (spot ? xy * (qx.z * inverse) : 0)) * inverse;
            float2 uy = (float2(qy.x, -qy.y) - (spot ? xy * (qy.z * inverse) : 0)) * inverse;
            bool coordinates = all(isfinite(uv)) && all(isfinite(ux)) && all(isfinite(uy));
            bool inside = front && (!(spot || b.w == 1) || (all(uv >= 0) && all(uv <= 1)));
            // No NaN/Inf reaches an image lookup, including flattened rejected lanes.
            sample = ForgeProjectedSample(selected, coordinates ? uv : .5,
                                           coordinates ? ux : 0, coordinates ? uy : 0).rgb;
            sample = inside ? sample : 0;
            valid = valid && coordinates;
        }
        valid = valid && all(isfinite(sample)) && all(sample >= 0);
        if (valid)
            image = sample;
    }
    return valid;
}
#endif
