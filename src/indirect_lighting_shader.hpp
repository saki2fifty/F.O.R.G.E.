#pragma once
#include <string>
#include <string_view>

namespace forge {
// FORGE adapter around the selected native public lighting API. It reuses the
// completed layer state; no BRDF implementation or extra cubemap sampling.
inline constexpr std::string_view indirect_lighting_shader = R"(
float3 ForgeIndirectRadiance(SurfaceShadingInfo shading,SurfaceLightingInfo lighting) {
    shading.Emissive=0;
    lighting.Base.Punctual=0;
#if ENABLE_SHEEN
    lighting.Sheen.Punctual=0;
#endif
#if ENABLE_CLEAR_COAT
    lighting.Clearcoat.Punctual=0;
#endif
    return ResolveLighting(shading,lighting);
}
)";
// The material supplies these values once; spatial passes never reconstruct BRDFs.
inline constexpr std::string_view reflection_surface_data = R"(
#ifndef FORGE_REFLECTION_SURFACE_DATA
#define FORGE_REFLECTION_SURFACE_DATA
struct ForgeReflectionSurfaceData {
    float4 Surface;
    float3 Response;
    float3 Specular;
};
#endif
)";
inline constexpr std::string_view base_reflection_shader = R"(
ForgeReflectionSurfaceData ForgeBaseReflection(SurfaceShadingInfo shading,
    SurfaceLightingInfo lighting,float3 viewNormal,float fogTransmittance) {
    ForgeReflectionSurfaceData result=(ForgeReflectionSurfaceData)0;
#if ENABLE_ANISOTROPY
    if(shading.Anisotropy.Strength!=0)return result;
#endif
    result.Surface=float4(normalize(viewNormal),shading.BaseLayer.Srf.PerceptualRoughness);
    SurfaceLightingInfo base=GetDefaultSurfaceLightingInfo();
    base.Base.SpecularIBL=lighting.Base.SpecularIBL;
    result.Specular=ForgeIndirectRadiance(shading,base)*fogTransmittance;
    IBLSamplingInfo info=GetIBLSamplingInfo(shading.BaseLayer.Srf,g_ForgeGGX,
        g_ForgeLightSampler,shading.BaseLayer.Normal,shading.View);
    base.Base.SpecularIBL=GetSpecularIBL_GGX(shading.BaseLayer.Srf,info,float3(1,1,1));
    shading.IBLScale=1;
    result.Response=ForgeIndirectRadiance(shading,base)*fogTransmittance;
    if(!all(isfinite(result.Surface))||!all(isfinite(result.Response))||
       !all(isfinite(result.Specular))||any(result.Response<0)||any(result.Specular<0))
        return (ForgeReflectionSurfaceData)0;
    return result;
}
)";
// Built-in and cooked surface entries share the exact optional MRT order.
inline std::string indirect_target_shader(bool temporal, bool reflection = false) {
    std::string result = "struct ForgeIndirectTargets {float4 Color:SV_Target0;\n";
    if (temporal)
        result += "float4 Motion:SV_Target1;float4 Indirect:SV_Target2;\n";
    else
        result += "float4 Indirect:SV_Target1;\n";
    if (reflection) {
        const unsigned first = temporal ? 3 : 2;
        result += "float4 Surface:SV_Target" + std::to_string(first) + ";\n";
        result += "float4 Response:SV_Target" + std::to_string(first + 1) + ";\n";
        result += "float4 Specular:SV_Target" + std::to_string(first + 2) + ";\n";
    }
    return result + "};\n";
}
} // namespace forge
