#ifndef FORGE_TRANSPARENCY_FXH
#define FORGE_TRANSPARENCY_FXH
// Identical additive blending on both targets. No independent-blend feature,
// fragment list or per-pixel allocation is required.
struct ForgeWeightedTargets {
    float4 Radiance : SV_Target0;
    float2 Optical : SV_Target1;
};
float ForgeTransparencyAbsorbance(float alpha) {
    if (alpha == 1)
        return 0;
    if (alpha < .125) {
        // -log(1-a) = 2*atanh(a/(2-a)); this remains accurate for tiny a.
        float t = alpha / (2 - alpha), q = t * t;
        return 2 * t * (1 + q * (1.0/3 + q * (1.0/5 + q/7)));
    }
    return -log(1 - alpha);
}
float ForgeTransparencyCoverage(float2 optical) {
    if (optical.y > 0)
        return 1; // An exact-alpha-one fragment; never log(0) or an epsilon stand-in.
    float x = optical.x;
    float coverage = x < .125
        ? x * (1 + x * (-.5 + x * (1.0/6 + x * (-1.0/24 + x * (1.0/120 - x/720)))))
        : 1 - exp(-x);
    return saturate(coverage);
}
ForgeWeightedTargets ForgeWeightedColor(float4 color, float3 position) {
    // Match the finite HDR display range. Invalid fragment arithmetic produces
    // the renderer's diagnostic color rather than poisoning the accumulated view.
    if (!all(isfinite(color)) || any(abs(color.rgb) > 65504) || !all(isfinite(position))) {
        color = float4(1,0,1,1);
        position = 0;
    }
    float alpha = saturate(color.a);
    float3 p = clamp(position * .1, -16, 16);
    float weight = max(1.0/256, 1 / (1 + dot(p,p)));
    ForgeWeightedTargets result;
    result.Radiance = float4(color.rgb * alpha * weight, alpha * weight);
    result.Optical = float2(ForgeTransparencyAbsorbance(alpha), alpha == 1 ? 1 : 0);
    return result;
}
#endif
