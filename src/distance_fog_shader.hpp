#pragma once
#include <string_view>
namespace forge {
// Shared by builtins and cooked custom/graph surface wrappers. World is already
// camera relative. Scale before length so even admitted very distant geometry
// cannot overflow the squared distance. No per-pixel texture or depth readback.
inline constexpr std::string_view distance_fog_shader = R"(
#ifndef FORGE_DISTANCE_FOG
#define FORGE_DISTANCE_FOG
cbuffer ForgeDistanceFog {float4 g_ForgeFogColor;float4 g_ForgeFogRange;};
float ForgeDistanceFogAmount(float3 world) {
    if(g_ForgeFogColor.w==0)return 0;
    if(!all(isfinite(world)))return 1;
    float largest=max(abs(world.x),max(abs(world.y),abs(world.z)));
    float amount=1;
    if(largest<g_ForgeFogRange.y) {
        float distance=largest>0?length(world/largest)*largest:0;
        amount=saturate((distance-g_ForgeFogRange.x)/(g_ForgeFogRange.y-g_ForgeFogRange.x));
    }
    return amount;
}
float ForgeDistanceFogTransmittance(float3 world) {
    return 1-ForgeDistanceFogAmount(world);
}
float4 ForgeApplyDistanceFog(float4 value,float3 world) {
    if(g_ForgeFogColor.w==0)return value;
    if(!all(isfinite(world)))return float4(1,0,1,1);
    float amount=ForgeDistanceFogAmount(world);
    if(amount==0)return value;
    value.rgb=value.rgb*(1-amount)+g_ForgeFogColor.rgb*amount;
    return value;
}
#endif
)";
} // namespace forge
