#pragma once
namespace forge {
// FORGE's bounded spatial edge filter. Input is display-encoded UNORM color;
// central alpha is retained and discontinuous alpha neighborhoods are excluded.
inline constexpr const char* fast_approximate_aa_pixel = R"(
Texture2D<float4> AASource;
SamplerState AALinear;
cbuffer AAValues {float4 Extent;};
float luminance(float3 rgb) {return dot(rgb,float3(.2126,.7152,.0722));}
float3 sampleColor(float2 pixel) {
    return AASource.SampleLevel(AALinear,(pixel+.5)/Extent.xy,0).rgb;
}
float4 main(float4 position:SV_Position):SV_Target0 {
    int2 p=int2(position.xy),size=int2(Extent.xy);
    float4 original=AASource.Load(int3(p,0));
    float l[9];float3 mean=0;
    float low=1,high=0;
    [unroll]for(int y=0;y<3;++y)[unroll]for(int x=0;x<3;++x) {
        float4 value=AASource.Load(int3(clamp(p+int2(x-1,y-1),int2(0,0),size-1),0));
        if(abs(value.a-original.a)>1./255.)return original;
        l[y*3+x]=luminance(value.rgb);mean+=value.rgb;
        low=min(low,l[y*3+x]);high=max(high,l[y*3+x]);
    }
    float contrast=high-low;
    if(contrast<max(.04,high*.125))return original;
    // Compare curvature across the two candidate edge normals, using all rows
    // rather than deciding from one potentially isolated neighboring sample.
    float acrossX=abs(l[0]-2*l[1]+l[2])+2*abs(l[3]-2*l[4]+l[5])+
                  abs(l[6]-2*l[7]+l[8]);
    float acrossY=abs(l[0]-2*l[3]+l[6])+2*abs(l[1]-2*l[4]+l[7])+
                  abs(l[2]-2*l[5]+l[8]);
    bool horizontal=acrossY>=acrossX;
    float sideA=horizontal?l[1]:l[3],sideB=horizontal?l[7]:l[5];
    float direction=abs(sideA-l[4])>=abs(sideB-l[4])?-1:1;
    float2 normal=horizontal?float2(0,direction):float2(direction,0);
    float2 tangent=horizontal?float2(1,0):float2(0,1);
    float middle=(l[4]+(direction<0?sideA:sideB))*.5;
    float2 edgeOrigin=float2(p)+normal*.5;
    float endContrast=max(.02,abs(l[4]-middle)*.5);
    float negativeDistance=0,positiveDistance=0,negativeSignal=0,positiveSignal=0;
    bool negativeFound=false,positiveFound=false;
    [loop]for(int distance=1;distance<=16;++distance) {
        if(distance>int(Extent.w)||(negativeFound&&positiveFound))break;
        if(!negativeFound) {
            negativeDistance=distance;
            negativeSignal=luminance(sampleColor(edgeOrigin-tangent*distance))-middle;
            negativeFound=abs(negativeSignal)>=endContrast;
        }
        if(!positiveFound) {
            positiveDistance=distance;
            positiveSignal=luminance(sampleColor(edgeOrigin+tangent*distance))-middle;
            positiveFound=abs(positiveSignal)>=endContrast;
        }
    }
    float shift=0;
    if(negativeFound&&positiveFound) {
        float nearDistance=min(negativeDistance,positiveDistance);
        float nearSignal=negativeDistance<=positiveDistance?negativeSignal:positiveSignal;
        if(nearSignal*(l[4]-middle)<0)
            shift=clamp(.5-nearDistance/(negativeDistance+positiveDistance),0,.5);
    }
    float3 edge=sampleColor(float2(p)+normal*shift);
    float adjacentMean=(l[1]+l[3]+l[5]+l[7])*.25;
    float isolation=saturate((abs(l[4]-adjacentMean)/contrast-.25)/.75);
    float smoothing=.5*Extent.z*isolation*isolation;
    return float4(lerp(edge,mean/9,smoothing),original.a);
})";
} // namespace forge
