#ifndef FORGE_LIGHTING_FXH
#define FORGE_LIGHTING_FXH
#include "BasicStructures.fxh"
#include "PBR_Shading.fxh"
// Compose with the pinned native BRDF/IBL/shadow implementation. Its spot path
// uses the cone axis for BRDF direction and linear angular falloff. Preserve the
// positional ray and glTF's squared cosine falloff through a point-light call.
// False means numerically undefined/unrepresentable contribution: the caller can
// account for it without publishing NaN/Inf into the HDR target.
bool ForgeApplyPunctualLight(in SurfaceShadingInfo Shading, in PBRLightAttribs Light,
#if ENABLE_SHEEN
    in Texture2D PreintegratedSheen, in SamplerState PreintegratedSheen_sampler,
#endif
#if ENABLE_SHADOWS
    in Texture2DArray<float> ShadowMap, in SamplerComparisonState ShadowMap_sampler,
    in PBRShadowMapInfo ShadowMapInfo,
#endif
    inout SurfaceLightingInfo Lighting)
{
    // Keep one initialized result through all rejection paths. FXC's inlined
    // definite-assignment analysis misdiagnoses locals after early returns.
    bool valid = true;
    float3 light_direction = float3(Light.DirectionX, Light.DirectionY, Light.DirectionZ);
    if (Light.Type != PBR_LIGHT_TYPE_DIRECTIONAL)
    {
        float3 delta = Shading.Pos - float3(Light.PosX, Light.PosY, Light.PosZ);
        float distance_squared = dot(delta, delta);
        valid = distance_squared >= 1.175494351e-38 && isfinite(distance_squared);
        if (valid)
        {
            float3 ray = delta * rsqrt(distance_squared);
            if (Light.Type == PBR_LIGHT_TYPE_SPOT)
            {
                float angular = saturate(dot(ray, light_direction) * Light.SpotAngleScale +
                                         Light.SpotAngleOffset);
                angular *= angular;
                Light.IntensityR *= angular;
                Light.IntensityG *= angular;
                Light.IntensityB *= angular;
                Light.Type = PBR_LIGHT_TYPE_POINT;
            }
            light_direction = ray;
        }
    }
    valid = valid && dot(Shading.View, Shading.View) >= 1.175494351e-38 &&
                    dot(Shading.BaseLayer.Normal, Shading.BaseLayer.Normal) >= 1.175494351e-38;
    // Valid back lights contribute zero without normalizing a zero half-vector.
    bool behind = dot(Shading.BaseLayer.Normal, -light_direction) <= 0;
#if ENABLE_CLEAR_COAT
    behind = behind && dot(Shading.Clearcoat.Normal, -light_direction) <= 0;
#endif
    if (valid && !behind)
    {
        float3 half_vector = Shading.View - light_direction;
        valid = dot(half_vector, half_vector) >= 1.175494351e-38;
        if (valid)
        {
            SurfaceLightingInfo candidate = GetDefaultSurfaceLightingInfo();
            ApplyPunctualLight(Shading, Light,
#if ENABLE_SHEEN
                PreintegratedSheen, PreintegratedSheen_sampler,
#endif
#if ENABLE_SHADOWS
                ShadowMap, ShadowMap_sampler, ShadowMapInfo,
#endif
                candidate);
            float3 base = Lighting.Base.Punctual + candidate.Base.Punctual;
            valid = all(isfinite(base));
#if ENABLE_SHEEN
            float3 sheen = Lighting.Sheen.Punctual + candidate.Sheen.Punctual;
            valid = valid && all(isfinite(sheen));
#endif
#if ENABLE_CLEAR_COAT
            float3 coat = Lighting.Clearcoat.Punctual + candidate.Clearcoat.Punctual;
            valid = valid && all(isfinite(coat));
#endif
            if (valid)
            {
                Lighting.Base.Punctual = base;
#if ENABLE_SHEEN
                Lighting.Sheen.Punctual = sheen;
#endif
#if ENABLE_CLEAR_COAT
                Lighting.Clearcoat.Punctual = coat;
#endif
            }
        }
    }
    return valid;
}
#endif
