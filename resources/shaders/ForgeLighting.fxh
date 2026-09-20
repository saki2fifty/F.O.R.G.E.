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
    float3 light_direction = float3(Light.DirectionX, Light.DirectionY, Light.DirectionZ);
    if (Light.Type != PBR_LIGHT_TYPE_DIRECTIONAL)
    {
        float3 delta = Shading.Pos - float3(Light.PosX, Light.PosY, Light.PosZ);
        float distance_squared = dot(delta, delta);
        if (!(distance_squared >= 1.175494351e-38) || !isfinite(distance_squared))
            return false;
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
    // Native GetAngularInfo normalizes L+V. Opposite directions have no defined
    // half vector. Degenerate normals/views must not enter the native BRDF.
    float3 half_vector = Shading.View - light_direction;
    if (!(dot(half_vector, half_vector) >= 1.175494351e-38) ||
        !(dot(Shading.View, Shading.View) >= 1.175494351e-38) ||
        !(dot(Shading.BaseLayer.Normal, Shading.BaseLayer.Normal) >= 1.175494351e-38))
        return false;
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
    if (!all(isfinite(base)))
        return false;
#if ENABLE_SHEEN
    float3 sheen = Lighting.Sheen.Punctual + candidate.Sheen.Punctual;
    if (!all(isfinite(sheen)))
        return false;
#endif
#if ENABLE_CLEAR_COAT
    float3 coat = Lighting.Clearcoat.Punctual + candidate.Clearcoat.Punctual;
    if (!all(isfinite(coat)))
        return false;
#endif
    Lighting.Base.Punctual = base;
#if ENABLE_SHEEN
    Lighting.Sheen.Punctual = sheen;
#endif
#if ENABLE_CLEAR_COAT
    Lighting.Clearcoat.Punctual = coat;
#endif
    return true;
}
#endif
