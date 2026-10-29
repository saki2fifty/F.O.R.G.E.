#ifndef FORGE_SHADOWS_FXH
#define FORGE_SHADOWS_FXH
#define PCF_FILTER_SIZE 3
#include "PCF.fxh"
struct ForgeShadowLight {
    float4 Matrix[32];
    float4 Range[8]; // XY: cascade ranges. ZW: derived soft-shadow projection data.
    float4 Settings; // count, depth bias, normal bias in metres, light kind
    float4 Position; // main-camera-relative light position, map resolution
};
cbuffer ForgeShadows {ForgeShadowLight g_ShadowLights[8];};
Texture2DArray<float> g_ForgeShadows[8];
SamplerComparisonState g_ForgeShadowSampler;
// Original disk quadrature with an explicit center, fixed in light-map coordinates.
// At most32 blocker loads +32 bilinear comparisons; no temporal state or images.
static const float2 ForgeSoftDisk[32]={float2(0,0),
float2(-0.1596450451,0.1462479387),
float2(0.0244362329,-0.2784382706),
float2(0.2012222393,0.2624587785),
float2(-0.3692675570,-0.0653182314),
float2(0.3498024660,-0.2225156956),
float2(-0.1170020791,0.4352419023),
float2(-0.2231356539,-0.4296341234),
float2(0.4841151151,0.1767980636),
float2(-0.5036411085,0.2078957282),
float2(0.2427882944,-0.5188244829),
float2(0.1794143739,0.5720012958),
float2(-0.5407570061,-0.3133797383),
float2(0.6343695234,-0.1394643602),
float2(-0.3871458452,0.5506751262),
float2(-0.0894396549,-0.6901996437),
float2(0.5490717571,0.4627582582),
float2(-0.7388784713,0.0305549445),
float2(0.5389551252,-0.5363323345),
float2(-0.0360581859,0.7797915152),
float2(-0.5128175319,-0.6145267927),
float2(0.8123595934,0.1093018343),
float2(-0.6883106405,0.4789086157),
float2(0.1880860573,-0.8360613823),
float2(0.4350332656,0.7591910549),
float2(-0.8504484103,-0.2713162386),
float2(0.8261024011,-0.3816802626),
float2(-0.3578882017,0.8551555619),
float2(-0.3194073343,-0.8880337577),
float2(0.8499086354,0.4466881591),
float2(-0.9440346462,0.2488445031),
float2(0.5365958083,-0.8345297709)};

uint ForgePointShadowFace(float3 ray) {
    float3 e=abs(ray);
    return e.x>=e.y&&e.x>=e.z?(ray.x>=0?0:1):
           e.y>=e.z?(ray.y>=0?2:3):(ray.z>=0?4:5);
}
float3 ForgePointShadowRay(uint face,float2 uv) {
    float2 p=(uv*2-1)*float2(1,-1);
    float3 ray=float3(1,p.y,-p.x);
    if(face==1)ray=float3(-1,p.y,p.x);
    if(face==2)ray=float3(-p.x,1,p.y);
    if(face==3)ray=float3(p.x,-1,p.y);
    if(face==4)ray=float3(p.x,p.y,1);
    if(face==5)ray=float3(-p.x,p.y,-1);
    return ray;
}
// Derive the receiver's depth plane from its actual geometric normal and
// fitted light projection. No screen derivatives/history or mapped normal.
float2 ForgeReceiverGradient(uint index,uint face,float2 uv,float3 normal,float axial) {
    float2 gradient=0;
    uint kind=(uint)g_ShadowLights[index].Settings.w;
    float3 x=g_ShadowLights[index].Matrix[face*4].xyz;
    float3 y=g_ShadowLights[index].Matrix[face*4+1].xyz;
    float3 z=g_ShadowLights[index].Matrix[face*4+(kind==0?2:3)].xyz;
    float3 scale=float3(length(x),length(y),length(z));
    if(all(scale>0)&&all(isfinite(scale))&&dot(normal,normal)>1e-12) {
        float3 n=float3(dot(normal,x)/scale.x,dot(normal,y)/scale.y,dot(normal,z)/scale.z);
        if(kind==0&&abs(n.z)>1e-6)
            gradient=2*scale.z*float2(-n.x/scale.x,n.y/scale.y)/n.z;
        if(kind==2) {
            float3 ray=float3((uv*2-1)*float2(1,-1)/scale.xy,1);
            float d=axial*dot(n,ray);
            if(abs(d)>1e-6)
                gradient=g_ShadowLights[index].Range[0].w*float2(2*n.x/scale.x,-2*n.y/scale.y)/d;
        }
    }
    if(!all(isfinite(gradient)))gradient=0;
    return gradient;
}
// Carry wide point taps across adjacent faces. Compare against the receiver
// plane at the actual tap, not the center pixel's depth. Raw loads use texel
// centers; bilinear comparisons include one texel of conservative plane bias.
void ForgeSoftTap(uint kind,uint face,float2 at,float2 coefficients,float receiver,
                  float bias,float2 base_uv,float2 gradient,float3 normal,float plane_d,
                  float size,bool raw,out float2 uv,out uint slice,out float comparison,
                  out float radial_scale) {
    uv=at;slice=face;radial_scale=1;
    if(kind==1) {
        float3 ray=ForgePointShadowRay(face,at);
        slice=ForgePointShadowFace(ray);
        float axial=max(abs(ray.x),max(abs(ray.y),abs(ray.z)));
        float2 p=float2(-ray.z,ray.y);
        if(slice==1)p=float2(ray.z,ray.y);
        if(slice==2)p=float2(-ray.x,ray.z);
        if(slice==3)p=float2(ray.x,ray.z);
        if(slice==4)p=ray.xy;
        if(slice==5)p=float2(-ray.x,ray.y);
        uv=p/axial*float2(.5,-.5)+.5;
        if(!raw)uv=clamp(uv,.5/size,1-.5/size);
    }
    if(raw&&all(uv>=0)&&all(uv<=1))
        uv=(clamp(floor(uv*size),0,size-1)+.5)/size;
    float2 slope=gradient;
    comparison=receiver+dot(slope,uv-base_uv);
    if(kind==1) {
        float3 ray=ForgePointShadowRay(slice,uv);
        radial_scale=length(ray);
        comparison=coefficients.x+coefficients.y*radial_scale/receiver;
        slope=0;
        if(abs(plane_d)>1e-6&&isfinite(plane_d)&&dot(normal,normal)>1e-12) {
            float3 du=ForgePointShadowRay(slice,float2(1,.5))-ForgePointShadowRay(slice,float2(0,.5));
            float3 dv=ForgePointShadowRay(slice,float2(.5,1))-ForgePointShadowRay(slice,float2(.5,0));
            float factor=coefficients.y/plane_d;
            comparison=coefficients.x+factor*dot(normal,ray);
            slope=factor*float2(dot(normal,du),dot(normal,dv));
        }
    }
    if(!raw)bias+=(abs(slope.x)+abs(slope.y))/size;
    comparison=saturate(comparison-bias);
}
float ForgeSoftShadow(uint index,uint face,float2 uv,float receiver,float size,
                       float3 position,float3 normal,float receiver_axis,out bool accepted) {
    accepted=false;
    float visibility=1;
    uint kind=(uint)g_ShadowLights[index].Settings.w;
    bool perspective=kind!=0;
    float2 coefficients=g_ShadowLights[index].Range[0].zw;
    float2 source=perspective?g_ShadowLights[index].Range[7].zw:
                              g_ShadowLights[index].Range[face].zw;
    float cap=min(32,floor(max(0,size-3)/4));
    if(cap>1&&all(isfinite(source))&&all(source>=0)&&any(source>0)) {
        float receiver_z=receiver,axial_z=receiver,near_z=0;
        if(perspective) {
            axial_z=receiver_axis;
            near_z=-coefficients.y/coefficients.x;
            receiver_z=kind==1?axial_z*length(ForgePointShadowRay(face,uv)):axial_z;
        }
        bool valid=!perspective||(isfinite(receiver_z)&&isfinite(near_z)&&
                                 receiver_z>0&&axial_z>0&&near_z>0);
        if(valid) {
            float2 radius=source*receiver;
            if(perspective)
                radius=source*max(0,receiver_z-near_z)/(axial_z*near_z);
            radius=min(radius,cap/size);
            // No admitted blocker could produce a resolved penumbra here.
            // Preserve fixed PCF rather than hardening a near-zero source.
            if(max(radius.x,radius.y)>1/size) {
                float sum=0,count=0;
                float bias=g_ShadowLights[index].Settings.y;
                float2 gradient=ForgeReceiverGradient(index,face,uv,normal,axial_z);
                float plane_d=dot(normal,position-g_ShadowLights[index].Position.xyz);
                [loop]for(uint i=0;i<32;++i) {
                    float2 at;uint slice;float comparison,radial_scale;
                    ForgeSoftTap(kind,face,uv+ForgeSoftDisk[i]*radius,coefficients,
                                 kind==1?receiver_z:receiver,bias,uv,gradient,normal,plane_d,size,true,
                                 at,slice,comparison,radial_scale);
                    if(all(at>=0)&&all(at<=1)) {
                        int2 pixel=clamp(int2(at*size),int2(0,0),int2(size-1,size-1));
                        float d=g_ForgeShadows[index].Load(int4(pixel,slice,0));
                        if(d>=0&&d<comparison&&d<1&&isfinite(d)) {
                            float z=d;
                            if(perspective)z=coefficients.y/(d-coefficients.x)*radial_scale;
                            if(isfinite(z)&&(!perspective||z>0)) {sum+=z;count+=1;}
                        }
                    }
                }
                if(count==0)accepted=true;
                else {
                    float blocker=sum/count;
                    float2 filter=source*max(0,receiver_z-blocker);
                    if(perspective)filter/=axial_z*blocker;
                    filter=min(filter,cap/size);
                    // Sub-texel source size retains the ordinary fixed PCF footprint.
                    if(all(isfinite(filter))&&max(filter.x,filter.y)>1/size) {
                        visibility=0;accepted=true;
                        [loop]for(uint j=0;j<32;++j) {
                            float2 at;uint slice;float comparison,radial_scale;
                            ForgeSoftTap(kind,face,uv+ForgeSoftDisk[j]*filter,coefficients,
                                         kind==1?receiver_z:receiver,bias,uv,gradient,normal,plane_d,size,false,
                                         at,slice,comparison,radial_scale);
                            visibility+=g_ForgeShadows[index].SampleCmpLevelZero(
                                g_ForgeShadowSampler,float3(at,slice),comparison)/32;
                        }
                    }
                }
            }
        }
    }
    return visibility;
}
float ForgeShadowSlice(uint index,uint slice,float3 position,float3 normal) {
    float4 p=float4(position,1);
    float4 clip=float4(dot(g_ShadowLights[index].Matrix[slice*4],p),
                      dot(g_ShadowLights[index].Matrix[slice*4+1],p),
                      dot(g_ShadowLights[index].Matrix[slice*4+2],p),
                      dot(g_ShadowLights[index].Matrix[slice*4+3],p));
    float visibility=1;
    if(clip.w>0 && all(isfinite(clip))) {
        // Perspective spots/point faces require Z/W as well as XY/W.
        float3 ndc=clip.xyz/clip.w;
        float size=g_ShadowLights[index].Position.w;
        if(all(abs(ndc.xy)<=1) && ndc.z>=0 && ndc.z<=1 && size>0 && isfinite(size)) {
            float2 uv=ndc.xy*float2(.5,-.5)+.5;
            bool accepted=false;
            visibility=ForgeSoftShadow(index,slice,uv,ndc.z,size,position,normal,clip.w,accepted);
            if(!accepted)
                visibility=FilterShadowMapFixedPCF(g_ForgeShadows[index],g_ForgeShadowSampler,
                    float4(size,size,1/size,1/size),uv,slice,
                    ndc.z-g_ShadowLights[index].Settings.y,float2(0,0));
        }
    }
    return visibility;
}
float ForgeShadowSlice(uint index,uint slice,float3 position) {
    return ForgeShadowSlice(index,slice,position,float3(0,0,0));
}
float ForgeShadowVisibility(int selected,float3 position,float3 normal,float camera_depth) {
    float visibility=1;
    if(selected>=0 && selected<8) {
        uint index=(uint)selected;
        uint count=(uint)g_ShadowLights[index].Settings.x;
        if(count>0 && count<=8) {
            uint kind=(uint)g_ShadowLights[index].Settings.w;
            uint slice=0;
            bool valid=true;
            if(kind==0) {
                // HLSL logical operators need not short-circuit. Test bounds
                // before indexing rather than reading Range[count] at the end.
                [loop]while(slice<count) {
                    if(camera_depth<=g_ShadowLights[index].Range[slice].y)break;
                    ++slice;
                }
                valid=slice<count && camera_depth>=g_ShadowLights[index].Range[0].x;
            } else if(kind==1) {
                float3 sample_position=position;
                if(any(g_ShadowLights[index].Range[7].zw>0))
                    sample_position+=normal*g_ShadowLights[index].Settings.z;
                float3 direction=sample_position-g_ShadowLights[index].Position.xyz;
                float3 extent=abs(direction);
                slice=extent.x>=extent.y && extent.x>=extent.z ? (direction.x>=0?0:1) :
                      extent.y>=extent.z ? (direction.y>=0?2:3) : (direction.z>=0?4:5);
                valid=slice<count;
            }
            if(valid) {
                position+=normal*g_ShadowLights[index].Settings.z;
                visibility=ForgeShadowSlice(index,slice,position,normal);
                if(kind==0 && slice+1<count) {
                    float begin=g_ShadowLights[index].Range[slice+1].x;
                    float end=g_ShadowLights[index].Range[slice].y;
                    float blend=saturate((camera_depth-begin)/(end-begin));
                    if(blend>0)visibility=lerp(visibility,ForgeShadowSlice(index,slice+1,position,normal),blend);
                }
                if(kind==0 && slice+1==count) {
                    float2 range=g_ShadowLights[index].Range[slice].xy;
                    float fade=saturate((camera_depth-lerp(range.x,range.y,.9))/((range.y-range.x)*.1));
                    visibility=lerp(visibility,1,fade);
                }
            }
        }
    }
    return visibility;
}
#endif
