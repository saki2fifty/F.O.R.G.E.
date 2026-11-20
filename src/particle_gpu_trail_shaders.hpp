#pragma once
#include "particle_gpu_shaders.hpp"
namespace forge {
// Built-in analytic ribbons share births, RNG and lifetime appearance. The
// supplemental view upload is consumed directly; no affine projection follows.
inline const std::string& particle_gpu_trail_compute_source() {
    static const std::string source =
        std::string("#include \"ForgeMath.fxh\"\n") + particle_gpu_declarations +
        particle_gpu_view_declarations + particle_gpu_view_math + particle_gpu_birth_entry + R"(
    uint segments=BillboardCounts.y;
    [loop] for(uint slot=0;slot<segments;++slot) {
        uint base=(id*segments+slot)*FORGE_STATIC_INSTANCE_ROWS;
        [unroll]for(uint r=0;r<FORGE_STATIC_INSTANCE_ROWS;++r)ParticleRows[base+r]=float4(0,0,0,r==6?4:0);
    }
    float head=max(0.0,Timing.x-float(id)*Timing.y);
    float ages[17];ages[0]=0;uint count=0;
    [loop]for(uint sample_index=0;sample_index<=segments;++sample_index) {
        float at=clamp(head-OpacityTiming.w*float(segments-sample_index)/float(segments),0.0,Timing.z);
        if(count==0)ages[count++]=at;else if(at>ages[max(count,1u)-1])ages[count++]=at;
    }
    if(count<2)return;
    float3 centers[17],sides[17];sides[0]=0;float widths[17];float4 appearance[17];
    float3 forward=-float3(CameraRows[0].z,CameraRows[1].z,CameraRows[2].z);
    float3 right=float3(CameraRows[0].x,CameraRows[1].x,CameraRows[2].x);
    float scale=ForgeParticleColumnLength(float3(Emitter[0].x,Emitter[1].x,Emitter[2].x));
    [loop]for(uint i=0;i<count;++i) {
        float age=ages[i],x=Timing.w*age;
)" + particle_gpu_integrals +
        R"(        // Derive tangents from displacement. Including a large birth origin
        // first would quantize short intervals before the subtraction.
        float3 position=initial*alpha+Acceleration.xyz*beta;
        centers[i]=float3(dot(Emitter[0].xyz,position),dot(Emitter[1].xyz,position),dot(Emitter[2].xyz,position));
        float u=saturate(age/Timing.z);
        widths[i]=ForgeParticleSize(u)*Acceleration.w*scale;
        appearance[i]=float4(ForgeParticleColor(u)*Tint.xyz,BillboardCounts.w!=0?saturate(ForgeParticleOpacity(u)):1);
    }
    [loop] for (uint i=0; i<count; ++i) {
        float3 incoming=ForgeUnit(centers[i]-centers[i>0?i-1:i]);
        float3 outgoing=ForgeUnit(centers[min(i+1,count-1)]-centers[i]);
        float3 direction=ForgeUnit(incoming+outgoing);
        if (all(direction==0)) direction=any(outgoing!=0)?outgoing:incoming;
        float3 side=ForgeUnit(cross(direction,forward));
        float3 previous=i>0?ForgeUnit(sides[max(i,1u)-1]):right;
        if (all(side==0)) side=previous;
        if (dot(side,previous)<0) side=-side;
        sides[i]=side*widths[i];
    }
    float3 translation=float3(dot(Emitter[0].xyz,origin)+Emitter[0].w,dot(Emitter[1].xyz,origin)+Emitter[1].w,dot(Emitter[2].xyz,origin)+Emitter[2].w);
    [loop] for (uint i=1; i<count; ++i) {
        float3 direction=ForgeUnit(centers[i]-centers[i-1]);
        float3 first=centers[i-1]+translation,last=centers[i]+translation;
        // Different endpoint sides can form a face even when their centers
        // coincide after float rounding. Retain the cleared degenerate slot.
        if (all(direction==0)||all(first==last)) continue;
        float3 normal=-ForgeUnit(cross(ForgeUnit(sides[i-1]+sides[i]),direction));
        if (all(normal==0)) normal=-forward;
        float span=ages[count-1]-ages[0];
        uint row=(id*segments+i-1)*FORGE_STATIC_INSTANCE_ROWS;
        ParticleRows[row]=float4(first,appearance[i-1].w);
        ParticleRows[row+1]=float4(last,appearance[i].w);
        ParticleRows[row+2]=float4(appearance[i-1].xyz,0);
        ParticleRows[row+3]=float4(sides[i-1],(ages[i-1]-ages[0])/span);
        ParticleRows[row+4]=float4(sides[i],(ages[i]-ages[0])/span);
        ParticleRows[row+5]=float4(normal,0);
        ParticleRows[row+6]=float4(appearance[i].xyz,4);
    }
}
)";
    return source;
}
} // namespace forge
