#pragma once
#include "particle_gpu_shaders.hpp"
namespace forge {
inline constexpr const char* particle_gpu_plane_math = R"(
cbuffer PlaneCollisionValues {float4 CollisionPlane,CollisionResponse;};
float3 Factors(float drag,float t){
    if(drag==0)return float3(1,t,.5*t*t);
    float x=drag*t,e=exp(-x),a,b;
    if(x<.125){
        a=t*(1+x*(-.5+x*(1.0/6+x*(-1.0/24+x*(1.0/120+x*(-1.0/720+x/5040))))));
        b=t*t*(.5+x*(-1.0/6+x*(1.0/24+x*(-1.0/120+x*(1.0/720-x/5040)))));
    }else {a=(1-e)/drag;b=(t-a)/drag;}
    return float3(e,a,b);
}
float2 NormalAt(float d,float v,float a,float drag,float t){
    float3 f=Factors(drag,t);
    if(drag==0)return float2(d+v*t+.5*a*t*t,v+a*t);
    return float2(d+v*f.y+a*f.z,v*f.x+a*f.y);
}
float Contact(float d,float v,float a,float drag,float age){
    if(d==0&&(v<0||(v==0&&a<0)))return 0;
    if(age==0)return -1;
    if(drag==0&&a>0&&v<0){
        // Classify the full parabola before endpoint distance cancellation.
        precise float discriminant=v*v-(2*a)*d;
        if(discriminant<=0)return -1;
    }
    if(drag>0&&a==0&&v<0&&d+v/drag>=0)return -1;
    float end=age;float2 last=NormalAt(d,v,a,drag,end);
    if(v<0&&last.y>0){
        float lo=0,hi=end;
        [loop]for(uint i=0;i<80;i++){
            float mid=lo+(hi-lo)*.5;if(mid==lo||mid==hi)break;
            if(NormalAt(d,v,a,drag,mid).y<0)lo=mid;else hi=mid;
        }
        end=hi;last=NormalAt(d,v,a,drag,end);if(last.x>=0)return -1;
    }else if(last.x>0||(last.x==0&&last.y>=0))return -1;
    if(last.x==0)return end;
    float lo=0,hi=end;
    [loop]for(uint i=0;i<80;i++){
        float mid=lo+(hi-lo)*.5;if(mid==lo||mid==hi)break;
        if(NormalAt(d,v,a,drag,mid).x>0)lo=mid;else hi=mid;
    }
    return hi;
}
float4 CollidedPosition(float3 p,float3 v,float3 force,float drag,float age){
    float3 n=normalize(CollisionPlane.xyz);
    float d=dot(p,n)-CollisionPlane.w,vn=dot(v,n),an=dot(force,n),elapsed=0;
    uint bounces=0,limit=uint(CollisionResponse.z);
    if(d<0)return float4(p,0);
    [loop]for(uint event=0;event<18;event++){
        float remaining=age-elapsed,contact=Contact(d,vn,an,drag,remaining);
        float t=contact<0?remaining:contact;float3 f=Factors(drag,t);
        if(drag==0){p+=v*t+.5*force*t*t;v+=force*t;}
        else {p+=v*f.y+force*f.z;v=v*f.x+force*f.y;}
        if(contact<0)return float4(p,1);
        float incoming=NormalAt(d,vn,an,drag,contact).y;
        elapsed=min(age,elapsed+contact);p+=n*(CollisionPlane.w-dot(p,n));
        if(bounces==limit||(incoming==0&&an<=0))return float4(p,0);
        vn=-CollisionResponse.x*incoming;++bounces;
        v=(v-n*incoming)*(1-CollisionResponse.y)+n*vn;
        if(vn==0&&an<0)return float4(p,0);
        d=0;
    }
    return float4(p,0);
}
)";
inline constexpr const char* particle_gpu_plane_motion = R"(
    float4 collision=CollidedPosition(origin,initial,Acceleration.xyz,Timing.w,age);
    float3 position=collision.xyz;
    float u=saturate(age/Timing.z),size=ForgeParticleSize(u)*Acceleration.w*collision.w;
    float3 color=ForgeParticleColor(u)*Tint.xyz;
)";
inline const std::string& particle_gpu_plane_compute_source() {
    static const std::string source = std::string(particle_gpu_plane_math) +
                                      particle_gpu_kernel_prefix + particle_gpu_plane_motion +
                                      particle_gpu_kernel_suffix;
    return source;
}
} // namespace forge
