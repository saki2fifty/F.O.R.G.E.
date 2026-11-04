#pragma once
#include "fog_volume_geometry.hpp"
#include <string_view>
namespace forge {
static_assert(fog_volume_limit == 16 && fog_volume_uniform_rows == 81 &&
              fog_volume_ray_limit == 10000000);
static_assert(fog_volume_ray_limit >=
              2 * (fog_volume_max_view_offset + 3 * fog_volume_max_world_extent));
// Shared by builtins and cooked custom/graph surface wrappers. World is already
// camera relative. Scale before length so even admitted very distant geometry
// cannot overflow the squared distance. No per-pixel texture or depth readback.
inline constexpr std::string_view distance_fog_shader = R"(
#ifndef FORGE_DISTANCE_FOG
#define FORGE_DISTANCE_FOG
cbuffer ForgeDistanceFog {float4 g_ForgeFogColor;float4 g_ForgeFogRange;};
cbuffer ForgeFogVolumes {float4 g_ForgeFogVolumes[81];};
float ForgeFogVolumeLength(float3 origin,float3 direction,float rayLength,float shape) {
    if(shape==0) {
        float entry=0,exit=rayLength;
        for(uint axis=0;axis<3;++axis) {
            if(direction[axis]==0) {
                if(abs(origin[axis])>1)return 0;
            } else {
                float a=(-1-origin[axis])/direction[axis];
                float b=(1-origin[axis])/direction[axis];
                entry=max(entry,min(a,b));exit=min(exit,max(a,b));
                if(entry>=exit)return 0;
            }
        }
        return max(0,exit-entry);
    }
    float speed=length(direction);
    if(speed==0)return 0;
    float3 axis=direction/speed;
    float projection=dot(origin,axis);
    float3 closest=origin-axis*projection;
    float radiusSquared=dot(closest,closest);
    if(radiusSquared>=1)return 0;
    float halfChord=sqrt(max(0,1-radiusSquared));
    float entry=max(0,(-projection-halfChord)/speed);
    float exit=min(rayLength,(-projection+halfChord)/speed);
    return max(0,exit-entry);
}
float4 ForgeFogVolumeComposite(float3 world) {
    if(g_ForgeFogVolumes[0].x==0)return float4(0,0,0,1);
    if(!all(isfinite(world)))return float4(1,0,1,0);
    float largest=max(abs(world.x),max(abs(world.y),abs(world.z)));
    if(largest==0)return float4(0,0,0,1);
    float3 scaled=world/largest;
    float norm=length(scaled);
    float3 ray=scaled/norm;
    float distance=min(largest,10000000.0/norm)*norm;
    float3 rayOrigin=0;
    float3 forward=g_ForgeFogVolumes[0].yzw;
    if(dot(forward,forward)>.5) {
        forward=normalize(forward);
        float depth=dot(world,forward);
        float3 origin;
        if(isfinite(depth)) {
            origin=world-forward*depth;
            if(!all(isfinite(origin))||max(abs(origin.x),max(abs(origin.y),abs(origin.z)))>10000000.0)
                return float4(0,0,0,1);
            rayOrigin=origin;
            if(depth==0)return float4(0,0,0,1);
            ray=depth>0?forward:-forward;
            distance=min(abs(depth),10000000.0);
        } else {
            depth=dot(scaled,forward);
            origin=scaled-forward*depth;
            if(max(abs(origin.x),max(abs(origin.y),abs(origin.z)))>10000000.0/largest)
                return float4(0,0,0,1);
            rayOrigin=origin*largest;
            if(depth==0)return float4(0,0,0,1);
            ray=depth>0?forward:-forward;
            distance=min(largest,10000000.0/abs(depth))*abs(depth);
        }
    }
    float opticalDepth=0;float3 weightedColor=0;
    for(uint i=0;i<min(16u,(uint)g_ForgeFogVolumes[0].x);++i) {
        uint at=1+i*5;
        float4 x=g_ForgeFogVolumes[at],y=g_ForgeFogVolumes[at+1],z=g_ForgeFogVolumes[at+2];
        float4 medium=g_ForgeFogVolumes[at+3];
        float3 origin=float3(x.w,y.w,z.w)+float3(dot(x.xyz,rayOrigin),dot(y.xyz,rayOrigin),dot(z.xyz,rayOrigin));
        float3 direction=float3(dot(x.xyz,ray),dot(y.xyz,ray),dot(z.xyz,ray));
        float lengthInVolume=ForgeFogVolumeLength(origin,direction,distance,g_ForgeFogVolumes[at+4].x);
        float tau=lengthInVolume*medium.w;
        opticalDepth+=tau;weightedColor+=medium.xyz*tau;
    }
    if(opticalDepth==0)return float4(0,0,0,1);
    return float4(weightedColor/opticalDepth,exp(-opticalDepth));
}
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
    return (1-ForgeDistanceFogAmount(world))*ForgeFogVolumeComposite(world).a;
}
float4 ForgeApplyDistanceFog(float4 value,float3 world) {
    if(g_ForgeFogColor.w==0&&g_ForgeFogVolumes[0].x==0)return value;
    if(!all(isfinite(world)))return float4(1,0,1,1);
    float amount=ForgeDistanceFogAmount(world);
    if(amount!=0)value.rgb=value.rgb*(1-amount)+g_ForgeFogColor.rgb*amount;
    float4 local=ForgeFogVolumeComposite(world);
    if(local.a!=1)value.rgb=value.rgb*local.a+local.rgb*(1-local.a);
    return value;
}
#endif
)";
} // namespace forge
