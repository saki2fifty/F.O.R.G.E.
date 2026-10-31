#ifndef FORGE_LOCAL_IRRADIANCE_FXH
#define FORGE_LOCAL_IRRADIANCE_FXH
// One count row, then center/intensity, three world planes and fade/yaw per probe.
cbuffer ForgeLocalIrradiance { float4 g_ForgeLocalIrradiance[41]; };
TextureCube g_ForgeIrradiance0;
TextureCube g_ForgeIrradiance1;
TextureCube g_ForgeIrradiance2;
TextureCube g_ForgeIrradiance3;
TextureCube g_ForgeIrradiance4;
TextureCube g_ForgeIrradiance5;
TextureCube g_ForgeIrradiance6;
TextureCube g_ForgeIrradiance7;
float3 ForgeProbeDiffuse(uint index, SurfaceReflectanceInfo surface, IBLSamplingInfo sampling,
                        float2 rotation) {
    switch (index) {
    case 0: return GetLambertianIBL(surface, sampling, rotation, g_ForgeIrradiance0, g_ForgeLightSampler);
    case 1: return GetLambertianIBL(surface, sampling, rotation, g_ForgeIrradiance1, g_ForgeLightSampler);
    case 2: return GetLambertianIBL(surface, sampling, rotation, g_ForgeIrradiance2, g_ForgeLightSampler);
    case 3: return GetLambertianIBL(surface, sampling, rotation, g_ForgeIrradiance3, g_ForgeLightSampler);
    case 4: return GetLambertianIBL(surface, sampling, rotation, g_ForgeIrradiance4, g_ForgeLightSampler);
    case 5: return GetLambertianIBL(surface, sampling, rotation, g_ForgeIrradiance5, g_ForgeLightSampler);
    case 6: return GetLambertianIBL(surface, sampling, rotation, g_ForgeIrradiance6, g_ForgeLightSampler);
    case 7: return GetLambertianIBL(surface, sampling, rotation, g_ForgeIrradiance7, g_ForgeLightSampler);
    default: return 0;
    }
}
bool ForgeApplyLocalIrradiance(float3 position, float globalIntensity,
                             inout SurfaceShadingInfo surface, inout SurfaceLightingInfo lighting) {
    float count = g_ForgeLocalIrradiance[0].x;
    bool valid = isfinite(count) && count >= 0 && count <= 8 && floor(count) == count;
    if (valid && count > 0) {
        IBLSamplingInfo sampling = GetIBLSamplingInfo(surface.BaseLayer.Srf, g_ForgeGGX,
            g_ForgeLightSampler, surface.BaseLayer.Normal, surface.View);
        float remaining = 1;
        float3 local = 0;
        // The count and selected texture index are uniform across the draw. Sample
        // every admitted probe before spatial coverage branches; native sampling
        // retains defined derivatives for mapped normals at volume boundaries.
        [loop]for (uint i = 0; i < (uint)count; ++i) {
            uint row = 1 + 5 * i;
            float4 center = g_ForgeLocalIrradiance[row];
            float4 options = g_ForgeLocalIrradiance[row + 4];
            float3 incoming = ForgeProbeDiffuse(i, surface.BaseLayer.Srf, sampling, options.yz);
            valid = valid && all(isfinite(incoming)) && all(incoming >= 0);
            float3 at = position - center.xyz;
            float margin = 3.402823466e+38;
            [unroll]for (uint axis = 0; axis < 3; ++axis) {
                float4 plane = g_ForgeLocalIrradiance[row + 1 + axis];
                margin = min(margin, plane.w - abs(dot(plane.xyz, at)));
            }
            float fade = margin < 0 ? 0 : (options.x == 0 ? 1 : saturate(margin / options.x));
            float weight = remaining * fade;
            local += incoming * (center.w * weight);
            remaining *= 1 - fade;
        }
#if ENABLE_TRANSMISSION
        local *= 1 - surface.Transmission;
#endif
        float3 diffuse = local + lighting.Base.DiffuseIBL * (remaining * globalIntensity);
        valid = valid && all(isfinite(diffuse));
        if (valid) {
            lighting.Base.DiffuseIBL = diffuse;
            // Local light remains visible when the Scene environment gain is zero.
            // Its other native indirect lobes retain their original global gain.
            lighting.Base.SpecularIBL *= globalIntensity;
#if ENABLE_SHEEN
            lighting.Sheen.SpecularIBL *= globalIntensity;
#endif
#if ENABLE_CLEAR_COAT
            lighting.Clearcoat.SpecularIBL *= globalIntensity;
#endif
            surface.IBLScale = 1;
        }
    }
    return valid;
}
#endif
