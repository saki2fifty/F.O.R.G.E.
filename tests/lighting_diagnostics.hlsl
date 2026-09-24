// Compile exact pinned upstream functions and FORGE wrappers independently.
#include "HLSLDefinitions.fxh"
#define ENABLE_CLEAR_COAT 1
#define ENABLE_IRIDESCENCE 1
#define USE_IBL 1
#include "ForgeLighting.fxh"
#include "Iridescence.fxh"
#include "ForgeShadows.fxh"
cbuffer Probe {
    SurfaceShadingInfo Shading;
    PBRLightAttribs Light;
    float4 Values;
};
float4 upstream_iridescence() : SV_Target {
    return float4(EvalIridescence(1, Values.x, Values.y, Values.z, Values.www), 1);
}
float4 upstream_sheen() : SV_Target {
    return LambdaSheenNumericHelper(Values.x, Values.y).xxxx;
}
float4 forge_lighting() : SV_Target {
    SurfaceLightingInfo lighting = GetDefaultSurfaceLightingInfo();
    bool valid = ForgeApplyPunctualLight(Shading, Light, lighting);
    return float4(lighting.Base.Punctual, valid ? 1 : 0);
}
float4 forge_shadow() : SV_Target {
    return ForgeShadowVisibility((int)Values.x, Shading.Pos, Shading.BaseLayer.Normal, Values.y).xxxx;
}
