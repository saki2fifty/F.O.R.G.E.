#pragma once
#include <string_view>
namespace forge {
// Current forward depth reconstruction shared by spatial surface passes.
// The caller declares CurrentDepth, Extent and the four projection rows.
inline constexpr std::string_view surface_depth_shader = R"(
bool positionAt(int2 p,out float3 location) {
    location=0;
    if(any(p<0)||any(p>=int2(Extent.xy)))return false;
    float d=CurrentDepth.Load(int3(p,0));
    if(!(d>=0&&d<1))return false;
    float denominator=d*P3.z-P2.z;
    if(abs(denominator)<=1e-20)return false;
    float z=(P2.w-d*P3.w)/denominator;
    float w=P3.z*z+P3.w;
    float2 ndc=(float2(p)+.5)/Extent.xy*float2(2,-2)+float2(-1,1);
    location=float3((ndc.x*w-P0.z*z-P0.w)/P0.x,(ndc.y*w-P1.z*z-P1.w)/P1.y,z);
    return z>0&&w>0&&all(isfinite(location));
}
)";
} // namespace forge
