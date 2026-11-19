#pragma once
namespace forge {
inline constexpr const char* particle_gpu_compute = R"(
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
[numthreads(64,1,1)]void main(uint3 thread:SV_DispatchThreadID){
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
    float age=max(0.0,Timing.x-float(id)*Timing.y),x=Timing.w*age;
    float alpha=age,beta=.5*age*age;
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
    float3 position=origin+initial*alpha+Acceleration.xyz*beta;
    float u=saturate(age/Timing.z),size=ForgeParticleSize(u)*Acceleration.w;
    float3 color=ForgeParticleColor(u)*Tint.xyz;
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
} // namespace forge
