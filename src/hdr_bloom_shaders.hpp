#pragma once
namespace forge::bloom_shader {
inline constexpr const char* vertex = R"(
float4 main(uint id:SV_VertexID):SV_Position {
    return float4(id==2?3:-1,id==1?3:-1,0,1);
})";
inline constexpr const char* common = R"(
Texture2D<float4> g_Source;
cbuffer ForgeBloom {float4 g_Settings; float4 g_Rect; float4 g_SourceSize;};
// Pixel-index coordinates, bilinear reconstruction, view-local edge replication.
float3 sample_glow(float2 p) {
    uint width=uint(g_SourceSize.x),height=uint(g_SourceSize.y);
    p=clamp(p,0,float2(width-1,height-1));
    int2 low=int2(floor(p)),high=min(low+1,int2(width-1,height-1));
    float2 t=frac(p);
    return lerp(lerp(g_Source.Load(int3(low,0)).rgb,
                     g_Source.Load(int3(high.x,low.y,0)).rgb,t.x),
                lerp(g_Source.Load(int3(low.x,high.y,0)).rgb,
                     g_Source.Load(int3(high,0)).rgb,t.x),t.y);
}
)";
inline constexpr const char* extract = R"(
float4 main(float4 position:SV_Position):SV_Target0 {
    uint width=uint(g_SourceSize.x),height=uint(g_SourceSize.y);
    int2 base=int2(position.xy)*2;
    float3 sum=0;
    [unroll]for(int y=0;y<2;++y)[unroll]for(int x=0;x<2;++x) {
        float3 value=g_Source.Load(int3(min(base+int2(x,y),int2(width-1,height-1)),0)).rgb;
        value=all(isfinite(value))?max(value,0):0;
        float peak=max(value.r,max(value.g,value.b));
        sum+=value*(peak>0?max(0,peak-g_Settings.x)/peak:0);
    }
    return float4(sum*.25,0);
})";
inline constexpr const char* blur = R"(
float4 main(float4 position:SV_Position):SV_Target0 {
    float2 p=floor(position.xy);
    float2 direction=g_Settings.w>0?float2(g_Settings.z,0):float2(0,g_Settings.z);
    float3 result=sample_glow(p)*6;
    result+=(sample_glow(p-direction)+sample_glow(p+direction))*4;
    result+=sample_glow(p-2*direction)+sample_glow(p+2*direction);
    return float4(result/16,0);
})";
inline constexpr const char* composite = R"(
Texture2D<float4> g_Original;
float4 main(float4 position:SV_Position):SV_Target0 {
    int2 p=int2(position.xy-g_Rect.xy);
    uint width=uint(g_SourceSize.x),height=uint(g_SourceSize.y);
    float4 original=g_Original.Load(int3(p,0));
    if(!all(isfinite(original)))return original;
    float2 glow_position=(float2(p)+.5)*float2(width,height)/g_Rect.zw-.5;
    float3 glow=sample_glow(glow_position)*g_Settings.y;
    // Keep the existing half-float HDR profile finite, preserving source alpha.
    return float4(min(original.rgb+glow,65504),original.a);
})";
} // namespace forge::bloom_shader
