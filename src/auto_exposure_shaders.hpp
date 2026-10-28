#pragma once
namespace forge::auto_exposure_shader {
// Constants are supplied by the existing C++ plan, before these original HLSL
// kernels. No source image or GPU history is copied to the CPU during metering.
inline constexpr const char* common = R"(
cbuffer ForgeAutoExposure {
    uint4 g_Size;      // Source width/height, metering width/height.
    float4 g_Limits;   // Min/max EV, low/high sample percentiles.
    float4 g_Adapt;    // Target gray, rising/falling EV rates, elapsed seconds.
    uint4 g_History;   // Reset, group count X/Y, reserved.
};
)";
inline constexpr const char* histogram = R"(
Texture2D<float4> g_Source;
RWStructuredBuffer<uint> g_TileHistogram;
groupshared uint g_LocalBins[FORGE_AUTO_BINS];
[numthreads(FORGE_AUTO_TILE_X,FORGE_AUTO_TILE_Y,1)]
void main(uint3 id:SV_DispatchThreadID,uint3 group:SV_GroupID,uint lane:SV_GroupIndex) {
    g_LocalBins[lane]=0;
    GroupMemoryBarrierWithGroupSync();
    if(all(id.xy<g_Size.zw)) {
        uint2 pixel=((2*id.xy+1)*g_Size.xy)/(2*g_Size.zw);
        float4 value=g_Source.Load(int3(pixel,0));
        float luminance=dot(max(value.rgb,0),float3(.2126,.7152,.0722));
        // Unlit/empty black pixels carry no exposure information. Positive
        // finite values still participate down to the fixed logarithmic floor.
        if(all(isfinite(value)) && isfinite(luminance) && luminance>0) {
            float logarithm=log2(max(luminance,exp2(FORGE_AUTO_LOG_MIN)));
            uint bin=(uint)clamp(floor((logarithm-FORGE_AUTO_LOG_MIN)*FORGE_AUTO_BINS/
                                     (FORGE_AUTO_LOG_MAX-FORGE_AUTO_LOG_MIN)),0,FORGE_AUTO_BINS-1);
            InterlockedAdd(g_LocalBins[bin],1);
        }
    }
    GroupMemoryBarrierWithGroupSync();
    uint tile=group.y*g_History.y+group.x;
    g_TileHistogram[tile*FORGE_AUTO_BINS+lane]=g_LocalBins[lane];
}
)";
inline constexpr const char* adaptation = R"(
StructuredBuffer<uint> g_TileHistogram;
RWStructuredBuffer<float4> g_Exposure;
groupshared uint g_Counts[FORGE_AUTO_BINS];
[numthreads(FORGE_AUTO_BINS,1,1)]
void main(uint lane:SV_GroupIndex) {
    uint count=0;
    for(uint tile=0;tile<g_History.y*g_History.z;++tile)
        count+=g_TileHistogram[tile*FORGE_AUTO_BINS+lane];
    g_Counts[lane]=count;
    GroupMemoryBarrierWithGroupSync();
    if(lane!=0)return;
    uint total=0;
    for(uint bin=0;bin<FORGE_AUTO_BINS;++bin)total+=g_Counts[bin];
    float low=total*g_Limits.z,high=total*g_Limits.w;
    float weight=0,weighted=0;
    uint cumulative=0;
    for(uint bin=0;bin<FORGE_AUTO_BINS;++bin) {
        uint next=cumulative+g_Counts[bin];
        float included=max(0,min((float)next,high)-max((float)cumulative,low));
        float center=FORGE_AUTO_LOG_MIN+(bin+.5)*(FORGE_AUTO_LOG_MAX-FORGE_AUTO_LOG_MIN)/FORGE_AUTO_BINS;
        weight+=included;weighted+=included*center;cumulative=next;
    }
    float4 previous=g_Exposure[0];
    float target=weight>0?log2(g_Adapt.x)-weighted/weight:0;
    target=clamp(target,g_Limits.x,g_Limits.y);
    float exposure=target;
    if(g_History.x==0 && previous.y!=0 && total!=0) {
        float old=clamp(previous.x,g_Limits.x,g_Limits.y);
        float rate=target>old?g_Adapt.y:g_Adapt.z;
        exposure=target+(old-target)*exp(-rate*g_Adapt.w);
    }
    // Valid history, last target and sample count also support bounded native
    // correctness readback. Presentation itself consumes only the first value.
    g_Exposure[0]=float4(exposure,total!=0?1:0,target,(float)total);
}
)";
} // namespace forge::auto_exposure_shader
