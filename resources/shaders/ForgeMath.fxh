#ifndef FORGE_MATH_FXH
#define FORGE_MATH_FXH
// Classify binary32 bits before floating-point arithmetic. FXC warns on
// isfinite(constant) even for intentionally tested zero/identity transforms.
// Bit classification also preserves NaN/Inf rejection under finite-math folding.
bool ForgeFinite(float v) { return (asuint(v) & 0x7f800000u) != 0x7f800000u; }
bool ForgeFinite3(float3 v) { return all((asuint(v) & 0x7f800000u) != 0x7f800000u); }
// FORGE's column-vector affine convention. All directional arithmetic is scaled
// before products so useful tiny/large visual scales do not require an inverse.
float3 ForgeUnit(float3 v)
{
    float largest = max(abs(v.x), max(abs(v.y), abs(v.z)));
    bool valid = largest > 0 && ForgeFinite3(v);
    // Select safe operands before division, not a potentially invalid quotient
    // afterward. Both paths remain finite if a compiler flattens control flow.
    float3 scaled = (valid ? v : float3(0, 0, 0)) / (valid ? largest : 1.0);
    float length_squared = dot(scaled, scaled);
    return scaled * rsqrt(length_squared > 0 ? length_squared : 1.0);
}
#endif
