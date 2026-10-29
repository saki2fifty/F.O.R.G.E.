#pragma once
namespace forge {
inline constexpr unsigned temporal_object_rows = 13, temporal_instance_rows = 11;
// Shared by native geometry and cooked surface wrappers. Matrices use the
// existing row-major, column-vector camera-relative FORGE convention.
inline constexpr char temporal_motion_uniform[] = R"hlsl(
cbuffer ForgeTemporalObject {float4 g_TemporalObject[13];};
float4 ForgePreviousProject(float3 position) {
    float4 view=float4(dot(g_TemporalObject[3].xyz,position),
                      dot(g_TemporalObject[4].xyz,position),
                      dot(g_TemporalObject[5].xyz,position),1);
    return float4(dot(g_TemporalObject[6],view),dot(g_TemporalObject[7],view),
                  dot(g_TemporalObject[8],view),dot(g_TemporalObject[9],view));
}
)hlsl";
inline constexpr char temporal_motion_pixel[] = R"hlsl(
struct ForgeTemporalTargets {float4 Color:SV_Target0;float4 Motion:SV_Target1;};
float4 ForgeTemporalMotion(float4 previous,float4 raster) {
    if(!all(isfinite(previous))||previous.w<=0||previous.z<0||previous.z>previous.w)
        return float4(0,0,1,0);
    float4 viewport=g_TemporalObject[12];
    float2 current=(raster.xy-viewport.xy)/viewport.zw;
    current=float2(current.x*2-1,1-current.y*2)-g_TemporalObject[10].xy;
    float2 prior=previous.xy/previous.w-g_TemporalObject[10].zw;
    return float4(current-prior,previous.z/previous.w,1);
}
)hlsl";
} // namespace forge
