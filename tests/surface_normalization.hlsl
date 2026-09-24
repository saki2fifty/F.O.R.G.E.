#include "../resources/shaders/ForgeSurface.fxh"
float4 zero_normal() : SV_Target { return float4(ForgeUnit(float3(0,0,0)),1); }
float4 zero_basis() : SV_Target {
    ForgeSurfaceFrame frame = ForgeMakeSurfaceFrame((float3x3)0,float3(0,1,0),float4(1,0,0,1));
    return float4(frame.Normal,frame.NormalValid?1:0);
}
float4 dynamic_frame(float3 v:TEXCOORD0) : SV_Target {
    ForgeSurfaceFrame frame = ForgeMakeSurfaceFrame(float3x3(v, v.yzx, v.zxy),v,float4(v,1));
    return float4(frame.Normal+ForgeUnit(v),frame.TangentValid?1:0);
}

float4 special_values(float3 v:TEXCOORD0) : SV_Target {
    float3 rejected = ForgeUnit(float3(asfloat(0x7f800000u), 1, 0)) +
                      ForgeUnit(float3(0, asfloat(0xff800000u), 1)) +
                      ForgeUnit(float3(1, 0, asfloat(0x7fc00001u)));
    bool classified = ForgeFinite(0) && ForgeFinite(asfloat(0x80000000u)) &&
        ForgeFinite(asfloat(0x7f7fffffu)) && ForgeFinite(asfloat(1u)) &&
        !ForgeFinite(asfloat(0x7f800000u)) && !ForgeFinite(asfloat(0xff800000u)) &&
        !ForgeFinite(asfloat(0x7fc00001u));
    return float4(rejected + ForgeUnit(v), classified ? 1 : 0);
}
