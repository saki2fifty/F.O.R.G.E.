#pragma once
namespace forge {
// Independently derived bounded contour reconstruction. No lookup image or
// supplied shader is used: each weight integrates a linear boundary over a pixel.
inline constexpr const char* morphological_aa_pixel = R"(
Texture2D<float4> AASource;
Texture2D<float4> MorphEdges;
Texture2D<float4> MorphWeights;
cbuffer AAValues {float4 Extent;float4 Options;};
int2 imageSize() {return int2(Extent.xy);}
bool inside(int2 p) {return all(p>=int2(0,0)) && all(p<imageSize());}
float4 sourcePixel(int2 p) {return AASource.Load(int3(clamp(p,int2(0,0),imageSize()-1),0));}
float difference(float3 a,float3 b) {float3 d=abs(a-b);return max(d.x,max(d.y,d.z));}
float4 edges(float4 position:SV_Position):SV_Target0 {
    int2 p=int2(position.xy);
    float4 center=sourcePixel(p),left=sourcePixel(p-int2(1,0)),top=sourcePixel(p-int2(0,1));
    float4 right=sourcePixel(p+int2(1,0)),bottom=sourcePixel(p+int2(0,1));
    // Reconstruct color only where the complete immediate alpha neighborhood
    // represents the same composited surface. Never reconstruct central alpha.
    float alpha=max(max(abs(left.a-center.a),abs(top.a-center.a)),
                    max(abs(right.a-center.a),abs(bottom.a-center.a)));
    if(alpha>1./255.)return 0;
    float west=difference(center.rgb,left.rgb),north=difference(center.rgb,top.rgb);
    float local=max(max(west,north),max(difference(center.rgb,right.rgb),difference(center.rgb,bottom.rgb)));
    float west_context=max(local,difference(left.rgb,sourcePixel(p-int2(2,0)).rgb));
    float north_context=max(local,difference(top.rgb,sourcePixel(p-int2(0,2)).rgb));
    return float4(p.x>0 && west>=Extent.z && west>=.5*west_context,
                  p.y>0 && north>=Extent.z && north>=.5*north_context,0,0);
}
bool edge(int2 p,uint axis) {
    if(!inside(p))return false;
    float2 flags=MorphEdges.Load(int3(p,0)).rg;
    return (axis==0 ? flags.y : flags.x)>.5;
}
// Axis zero follows a north boundary along X. Axis one transposes this
// construction to follow a west boundary along Y.
float crossing(int2 at,uint axis,out bool corner) {
    int2 normal=axis==0 ? int2(0,1) : int2(1,0);
    bool negative=edge(at-normal,1-axis),positive=edge(at,1-axis);
    corner=false;
    if(negative==positive)return 0;
    corner=negative ? edge(at-2*normal,1-axis) : edge(at+normal,1-axis);
    return negative ? -.5 : .5;
}
float positiveArea(float a,float b) {
    if(a>=0 && b>=0)return .5*(a+b);
    if(a<=0 && b<=0)return 0;
    float high=max(a,b);
    return high*high/(2*abs(b-a));
}
float boundary(float t,float first,float last,bool turn) {
    if(!turn)return lerp(first,last,t);
    return t<.5 ? lerp(first,0,t*2) : lerp(0,last,(t-.5)*2);
}
float2 areas(float low,float high,float first,float last) {
    bool turn=first*last>0;
    float a=boundary(low,first,last,turn),b=boundary(high,first,last,turn);
    float positive=positiveArea(a,b),negative=positiveArea(-a,-b);
    if(turn && low<.5 && high>.5) {
        float fraction=(.5-low)/(high-low);
        positive=fraction*positiveArea(a,0)+(1-fraction)*positiveArea(0,b);
        negative=fraction*positiveArea(-a,0)+(1-fraction)*positiveArea(0,-b);
    }
    return float2(positive,negative);
}
bool diagonal(int2 p,uint axis,int rise) {
    int2 tangent=axis==0 ? int2(1,0) : int2(0,1);
    int2 normal=axis==0 ? int2(0,1) : int2(1,0);
    int count=0;
    [unroll]for(int sign=-1;sign<=1;sign+=2) {
        int2 at=p;
        [loop]for(int i=1;i<=32;++i) {
            if(i>int(Extent.w))break;
            int2 connector=at+(sign>0 ? tangent : int2(0,0));
            if(sign*rise<0)connector-=normal;
            int2 next=at+sign*(tangent+rise*normal);
            if(!edge(connector,1-axis) || !edge(next,axis))break;
            ++count;at=next;
        }
    }
    return count>=2;
}
float2 interfaceWeights(int2 p,uint axis) {
    if(!edge(p,axis))return 0;
    int2 tangent=axis==0 ? int2(1,0) : int2(0,1);
    int2 normal=axis==0 ? int2(0,1) : int2(1,0);
    // Two parallel boundaries one pixel apart describe isolated fine detail,
    // not an unobserved broad surface. Keep that feature's original coverage.
    if(edge(p-normal,axis) || edge(p+normal,axis))return 0;
    int left=0,right=0;
    [loop]for(int i=1;i<=32;++i) {
        if(i>int(Extent.w) || !edge(p-i*tangent,axis))break;
        left=i;
    }
    [loop]for(int i=1;i<=32;++i) {
        if(i>int(Extent.w) || !edge(p+i*tangent,axis))break;
        right=i;
    }
    bool first_corner=false,last_corner=false;
    // A search truncated in the middle of a contour has no observed endpoint.
    float first=edge(p-(left+1)*tangent,axis) ? 0 : crossing(p-left*tangent,axis,first_corner);
    float last=edge(p+(right+1)*tangent,axis) ? 0 : crossing(p+(right+1)*tangent,axis,last_corner);
    bool is_diagonal=left==0 && right==0 && first*last<0 && diagonal(p,axis,last>first ? 1 : -1);
    if(!is_diagonal) {
        if(first_corner)first*=Options.x;
        if(last_corner)last*=Options.x;
    }
    float length=left+right+1;
    float2 result=areas(left/length,(left+1)/length,first,last);
    // Negative weights tag confirmed connected diagonal contours. The signed
    // half-float target retains that tag without quantized bit packing.
    return is_diagonal ? -result : result;
}
float4 weights(float4 position:SV_Position):SV_Target0 {
    int2 p=int2(position.xy);
    return float4(interfaceWeights(p,1),interfaceWeights(p,0));
}
float4 weightPixel(int2 p) {return inside(p) ? MorphWeights.Load(int3(p,0)) : float4(0,0,0,0);}
float4 blend(float4 position:SV_Position):SV_Target0 {
    int2 p=int2(position.xy);
    float4 center=sourcePixel(p),west=sourcePixel(p-int2(1,0)),north=sourcePixel(p-int2(0,1));
    float4 east=sourcePixel(p+int2(1,0)),south=sourcePixel(p+int2(0,1));
    if(max(max(abs(west.a-center.a),abs(north.a-center.a)),max(abs(east.a-center.a),abs(south.a-center.a)))>1./255.)return center;
    float4 own=weightPixel(p);
    float4 signed_weights=float4(own.x,weightPixel(p+int2(1,0)).y,own.z,weightPixel(p+int2(0,1)).w);
    float4 value=abs(signed_weights);
    if(!any(signed_weights<0)) {
        // Orthogonal junctions choose one normal. Confirmed diagonal staircases
        // combine their two axis contributions to reconstruct triangular areas.
        if(value.x+value.y>=value.z+value.w)value.zw=0;
        else value.xy=0;
    }
    float total=dot(value,float4(1,1,1,1));
    value/=max(total,1.);
    total=min(total,1.);
    return float4(center.rgb*(1-total)+west.rgb*value.x+east.rgb*value.y+north.rgb*value.z+south.rgb*value.w,center.a);
}
)";
} // namespace forge
