#pragma once
#include <string>
namespace forge {
inline constexpr const char* particle_gpu_declarations = R"(
cbuffer ParticleValues {
    uint4 Births;uint4 Modes;
    float4 Timing,Axis,Side,Up,Acceleration,Shape,Sizes,Tint;
    float4 Emitter[3],Colors[2],SizeStops[16],ColorStops[16];
};
RWStructuredBuffer<float4> ParticleRows;
uint ForgeParticleHashWord(uint h,uint word){
    [unroll]for(uint byte=0;byte<4;byte++){h=(h^(word&255u))*16777619u;word>>=8u;}
    return h;
}
float ForgeParticleUnit(inout uint state){
    int t=48271*int(state%44488u)-3399*int(state/44488u);
    state=uint(t>0?t:t+2147483647);
    return float(state>>7u)*(1.0/16777216.0);
}
float ForgeParticleSize(float u){
    if(Modes.y==0u)return lerp(Sizes.x,Sizes.y,u);
    [loop]for(uint i=1u;i<Modes.y;i++){
        if(u<SizeStops[i].x){
            float4 a=SizeStops[i-1u],b=SizeStops[i];
            return a.z!=0?a.y:lerp(a.y,b.y,saturate((u-a.x)/(b.x-a.x)));
        }
    }
    return SizeStops[Modes.y-1u].y;
}
float3 ForgeParticleColor(float u){
    if(Modes.z==0u)return lerp(Colors[0].xyz,Colors[1].xyz,u);
    [loop]for(uint i=1u;i<Modes.z;i++){
        if(u<ColorStops[i].x){
            float4 a=ColorStops[i-1u],b=ColorStops[i];
            return (Modes.w&(1u<<(i-1u)))!=0u?a.yzw:lerp(a.yzw,b.yzw,saturate((u-a.x)/(b.x-a.x)));
        }
    }
    return ColorStops[Modes.z-1u].yzw;
}
)";
inline constexpr const char* particle_gpu_birth_entry =
    R"([numthreads(64,1,1)]void main(uint3 thread:SV_DispatchThreadID){
    uint id=thread.x;if(id>=Births.x)return;
    uint low=Births.y+id,high=Births.z+uint(low<Births.y);
    uint h=ForgeParticleHashWord(ForgeParticleHashWord(ForgeParticleHashWord(2166136261u,Births.w),low),high);
    uint state=h%2147483646u+1u;
    float c=1-ForgeParticleUnit(state)*(1-Axis.w);
    float s=sqrt(max(0.0,1-c*c)),phi=ForgeParticleUnit(state)*6.283185307179586;
    float speed=lerp(Side.w,Up.w,ForgeParticleUnit(state));
    float3 initial=speed*(Axis.xyz*c+Side.xyz*(s*cos(phi))+Up.xyz*(s*sin(phi)));
    float3 origin=0;
    if(Modes.x==1u){
        origin.x=(ForgeParticleUnit(state)-.5)*Shape.x;
        origin.y=(ForgeParticleUnit(state)-.5)*Shape.y;
        origin.z=(ForgeParticleUnit(state)-.5)*Shape.z;
    }else if(Modes.x==2u){
        float radius=Shape.w*pow(ForgeParticleUnit(state),1.0/3.0);
        float y=2*ForgeParticleUnit(state)-1;
        float radial=sqrt(max(0.0,1-y*y)),angle=ForgeParticleUnit(state)*6.283185307179586;
        origin=radius*float3(radial*cos(angle),y,radial*sin(angle));
    }else if(Modes.x==3u){
        float radius=Shape.w*sqrt(ForgeParticleUnit(state));
        float angle=ForgeParticleUnit(state)*6.283185307179586;
        origin=radius*(Side.xyz*cos(angle)+Up.xyz*sin(angle));
    }
)";
inline constexpr const char* particle_gpu_age =
    R"(    float age=max(0.0,Timing.x-float(id)*Timing.y),x=Timing.w*age;
)";
inline constexpr const char* particle_gpu_integrals = R"(    float alpha=age,beta=.5*age*age;
    if(Timing.w>0){
        // Float subtraction in (age-alpha)/drag loses useful precision well
        // before x reaches .001. These series retain float accuracy up to .1.
        if(x<.1){
            alpha=age*(1+x*(-.5+x*(1.0/6.0+x*(-1.0/24.0+x/120.0))));
            beta=age*age*(.5+x*(-1.0/6.0+x*(1.0/24.0+x*(-1.0/120.0+x/720.0))));
        }else{
            alpha=(1-exp(-x))/Timing.w;beta=(age-alpha)/Timing.w;
        }
    }
)";
inline const std::string particle_gpu_kernel_prefix_text =
    std::string(particle_gpu_declarations) + particle_gpu_birth_entry + particle_gpu_age +
    particle_gpu_integrals;
inline const char* const particle_gpu_kernel_prefix = particle_gpu_kernel_prefix_text.c_str();
inline constexpr const char* particle_gpu_plain_motion =
    R"(    float3 position=origin+initial*alpha+Acceleration.xyz*beta;
    float u=saturate(age/Timing.z),size=ForgeParticleSize(u)*Acceleration.w;
    float3 color=ForgeParticleColor(u)*Tint.xyz;
)";
inline constexpr const char* particle_gpu_kernel_suffix = R"(#ifdef FORGE_PARTICLE_GRAPH
    ForgeParticleGraphInput input;
    input.Position=position;input.Velocity=initial*exp(-x)+Acceleration.xyz*alpha;
    input.Origin=origin;input.Age=age;input.Age01=u;
    input.Size=ForgeParticleSize(u);input.Color=ForgeParticleColor(u);
    input.Random=ForgeParticleUnit(state);
    ForgeParticleGraphValues graph=ForgeParticleGraph(input);
    // A graph cannot escape its declared local bounds or publish NaN/Inf rows.
    position=clamp(ForgeFinite3(graph.Position)?graph.Position:input.Position,
                   -GraphExtentSize.xyz,GraphExtentSize.xyz);
    size=clamp(ForgeFinite(graph.Size)?graph.Size:input.Size,0,GraphExtentSize.w)*Acceleration.w;
    color=clamp(ForgeFinite3(graph.Color)?graph.Color:input.Color,0,65504)*Tint.xyz;
#endif
    float basis_max=max(max(max(abs(Emitter[0].x),abs(Emitter[0].y)),abs(Emitter[0].z)),
                        max(max(max(abs(Emitter[1].x),abs(Emitter[1].y)),abs(Emitter[1].z)),
                            max(max(abs(Emitter[2].x),abs(Emitter[2].y)),abs(Emitter[2].z))));
    uint row=id*FORGE_STATIC_INSTANCE_ROWS;
    [unroll]for(uint r=0u;r<3u;r++){
        ParticleRows[row+r]=float4(Emitter[r].xyz*size,dot(Emitter[r].xyz,position)+Emitter[r].w);
        ParticleRows[row+3u+r]=float4(basis_max>0?Emitter[r].xyz/basis_max:float3(0,0,0),0);
    }
    ParticleRows[row+6u]=float4(color,1);
}
)";
// Canonical legacy program bytes are retained while motion variants share births,
// lifetime appearance, transforms and the immutable 47-row input/7-row output.
inline const std::string particle_gpu_compute_text = std::string(particle_gpu_kernel_prefix) +
                                                     particle_gpu_plain_motion +
                                                     particle_gpu_kernel_suffix;
inline const char* const particle_gpu_compute = particle_gpu_compute_text.c_str();
// Project the existing device rows in place after analytic or authored evaluation.
// The original evaluator's constants and RNG remain unchanged.
inline constexpr const char* particle_gpu_view_declarations = R"(
cbuffer ParticleBillboardValues {
    uint4 BillboardCounts;float4 CameraRows[3];
    float4 OpacityTiming,OpacityEndpoints,OpacityStops[16];
};
)";
inline constexpr const char* particle_gpu_view_math = R"(float ForgeParticleOpacity(float u){
    if(BillboardCounts.z==0u)return lerp(OpacityEndpoints.x,OpacityEndpoints.y,u);
    [loop]for(uint i=1u;i<BillboardCounts.z;i++){
        if(u<OpacityStops[i].x){
            float4 a=OpacityStops[i-1u],b=OpacityStops[i];
            return a.z!=0?a.y:lerp(a.y,b.y,saturate((u-a.x)/(b.x-a.x)));
        }
    }
    return OpacityStops[BillboardCounts.z-1u].y;
}
float ForgeParticleColumnLength(float3 value){
    float scale=max(max(abs(value.x),abs(value.y)),abs(value.z));
    return scale>0?scale*length(value/scale):0;
}
)";
inline constexpr const char* particle_gpu_projection_entry =
    R"([numthreads(64,1,1)]void main(uint3 thread:SV_DispatchThreadID){
    uint id=thread.x;if(id>=BillboardCounts.x)return;
    uint row=id*FORGE_STATIC_INSTANCE_ROWS;
    if(BillboardCounts.y!=0u){
    float4 a=ParticleRows[row],b=ParticleRows[row+1u],c=ParticleRows[row+2u];
    float width=ForgeParticleColumnLength(float3(a.x,b.x,c.x));
    float height=ForgeParticleColumnLength(float3(a.y,b.y,c.y));
    float3 sizes=float3(width,height,max(width,height));
    float3 basis[3];
    [unroll]for(uint r=0u;r<3u;r++)basis[r]=CameraRows[r].xyz*sizes;
    float maximum=max(max(max(abs(basis[0].x),abs(basis[0].y)),abs(basis[0].z)),
                      max(max(max(abs(basis[1].x),abs(basis[1].y)),abs(basis[1].z)),
                          max(max(abs(basis[2].x),abs(basis[2].y)),abs(basis[2].z))));
    ParticleRows[row]=float4(basis[0],a.w);
    ParticleRows[row+1u]=float4(basis[1],b.w);
    ParticleRows[row+2u]=float4(basis[2],c.w);
    [unroll]for(uint r=0u;r<3u;r++)
        ParticleRows[row+3u+r]=float4(maximum>0?basis[r]/maximum:float3(0,0,0),0);
    }
    float4 tint=ParticleRows[row+6u];
    if(BillboardCounts.w!=0u){
        float age=max(0.0,OpacityTiming.x-float(id)*OpacityTiming.y);
        float4 normal=ParticleRows[row+3u];
        normal.w=saturate(ForgeParticleOpacity(saturate(age/OpacityTiming.z)));
        ParticleRows[row+3u]=normal;
        ParticleRows[row+6u]=float4(tint.xyz,3);
    }else ParticleRows[row+6u]=float4(tint.xyz,2);
}
)";
inline const std::string particle_gpu_billboard_compute_text =
    std::string(particle_gpu_view_declarations) + "RWStructuredBuffer<float4> ParticleRows;\n" +
    particle_gpu_view_math + particle_gpu_projection_entry;
inline const char* const particle_gpu_billboard_compute =
    particle_gpu_billboard_compute_text.c_str();
} // namespace forge
