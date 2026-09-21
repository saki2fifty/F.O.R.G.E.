#ifndef FORGE_SHADOWS_FXH
#define FORGE_SHADOWS_FXH
#define PCF_FILTER_SIZE 3
#include "PCF.fxh"
struct ForgeShadowLight {
    float4 Matrix[32];
    float4 Range[8];
    float4 Settings; // count, depth bias, normal bias in metres, light kind
    float4 Position; // main-camera-relative light position, map resolution
};
cbuffer ForgeShadows {ForgeShadowLight g_ShadowLights[8];};
Texture2DArray<float> g_ForgeShadows[8];
SamplerComparisonState g_ForgeShadowSampler;
float ForgeShadowSlice(uint index,uint slice,float3 position) {
    float4 p=float4(position,1);
    float4 clip=float4(dot(g_ShadowLights[index].Matrix[slice*4],p),
                      dot(g_ShadowLights[index].Matrix[slice*4+1],p),
                      dot(g_ShadowLights[index].Matrix[slice*4+2],p),
                      dot(g_ShadowLights[index].Matrix[slice*4+3],p));
    if(!(clip.w>0)||!all(isfinite(clip)))return 1;
    // Perspective spots/point faces require Z/W as well as XY/W. The pinned
    // native PBR convenience path only divides XY; use native PCF explicitly.
    float3 ndc=clip.xyz/clip.w;
    if(any(abs(ndc.xy)>1)||ndc.z<0||ndc.z>1)return 1;
    float2 uv=ndc.xy*float2(.5,-.5)+.5;
    float size=g_ShadowLights[index].Position.w;
    return FilterShadowMapFixedPCF(g_ForgeShadows[index],g_ForgeShadowSampler,
        float4(size,size,1/size,1/size),uv,slice,
        ndc.z-g_ShadowLights[index].Settings.y,float2(0,0));
}
float ForgeShadowVisibility(int selected,float3 position,float3 normal,float camera_depth) {
    if(selected<0||selected>=8)return 1;
    uint index=(uint)selected;
    uint count=(uint)g_ShadowLights[index].Settings.x;
    if(count==0)return 1;
    uint kind=(uint)g_ShadowLights[index].Settings.w;
    uint slice=0;
    if(kind==0) {
        [loop]while(slice<count && camera_depth>g_ShadowLights[index].Range[slice].y)++slice;
        if(slice==count || camera_depth<g_ShadowLights[index].Range[0].x)return 1;
    } else if(kind==1) {
        float3 direction=position-g_ShadowLights[index].Position.xyz;
        float3 extent=abs(direction);
        slice=extent.x>=extent.y && extent.x>=extent.z ? (direction.x>=0?0:1) :
              extent.y>=extent.z ? (direction.y>=0?2:3) : (direction.z>=0?4:5);
    }
    position+=normal*g_ShadowLights[index].Settings.z;
    float visibility=ForgeShadowSlice(index,slice,position);
    if(kind==0 && slice+1<count) {
        float begin=g_ShadowLights[index].Range[slice+1].x;
        float end=g_ShadowLights[index].Range[slice].y;
        float blend=saturate((camera_depth-begin)/(end-begin));
        if(blend>0)visibility=lerp(visibility,ForgeShadowSlice(index,slice+1,position),blend);
    }
    if(kind==0 && slice+1==count) {
        float2 range=g_ShadowLights[index].Range[slice].xy;
        float fade=saturate((camera_depth-lerp(range.x,range.y,.9))/((range.y-range.x)*.1));
        visibility=lerp(visibility,1,fade);
    }
    return visibility;
}
#endif
