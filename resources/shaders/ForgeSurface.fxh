#ifndef FORGE_SURFACE_FXH
#define FORGE_SURFACE_FXH
// FORGE's column-vector affine convention. All directional arithmetic is scaled
// before products so useful tiny/large visual scales do not require an inverse.
float3 ForgeUnit(float3 v)
{
    float largest = max(abs(v.x), max(abs(v.y), abs(v.z)));
    bool valid = largest > 0 && all(isfinite(v));
    // Select safe operands before division, not a potentially invalid quotient
    // afterward. Both paths remain finite if a compiler flattens control flow.
    float3 scaled = (valid ? v : float3(0, 0, 0)) / (valid ? largest : 1.0);
    float length_squared = dot(scaled, scaled);
    return scaled * rsqrt(length_squared > 0 ? length_squared : 1.0);
}
struct ForgeSurfaceFrame
{
    float3 Normal;
    float3 Tangent;
    float3 Bitangent;
    bool NormalValid;
    bool TangentValid;
};
ForgeSurfaceFrame ForgeMakeSurfaceFrame(float3x3 basis, float3 normal, float4 tangent)
{
    ForgeSurfaceFrame result = (ForgeSurfaceFrame)0;
    float3 row_max = max(abs(basis[0]), max(abs(basis[1]), abs(basis[2])));
    float largest = max(row_max.x, max(row_max.y, row_max.z));
    bool valid = largest > 0 && all(isfinite(basis[0])) &&
                 all(isfinite(basis[1])) && all(isfinite(basis[2]));
    basis = (valid ? basis : (float3x3)0) / (valid ? largest : 1.0);
    float3 c0 = float3(basis[0][0], basis[1][0], basis[2][0]);
    float3 c1 = float3(basis[0][1], basis[1][1], basis[2][1]);
    float3 c2 = float3(basis[0][2], basis[1][2], basis[2][2]);
    float3 k0 = cross(c1, c2), k1 = cross(c2, c0), k2 = cross(c0, c1);
    float determinant = dot(c0, k0);
    float error_scale = dot(abs(c0), abs(c1.yzx * c2.zxy) + abs(c1.zxy * c2.yzx));
    // Treat determinant cancellation as rank deficient rather than randomly
    // flipping the frame. No inverse or reciprocal determinant is evaluated.
    float parity = determinant < -16.0 * 1.192092896e-7 * error_scale ? -1.0 : 1.0;
    result.Normal = ForgeUnit((k0 * normal.x + k1 * normal.y + k2 * normal.z) * parity);
    result.NormalValid = any(result.Normal != 0);
    float3 t = mul(basis, tangent.xyz);
    result.Tangent = ForgeUnit(t - result.Normal * dot(result.Normal, t));
    // Transform the source bitangent too. Its orientation includes both source
    // UV handedness and reflections, and remains defined on surviving rank-2 faces.
    float3 b = ForgeUnit(mul(basis, cross(normal, tangent.xyz) * tangent.w));
    float orientation = dot(cross(result.Normal, result.Tangent), b);
    result.TangentValid = abs(orientation) > 1e-6;
    if (result.TangentValid)
        result.Bitangent = cross(result.Normal, result.Tangent) * (orientation < 0 ? -1 : 1);
    else
        result.Tangent = float3(0, 0, 0);
    return result;
}
float3 ForgePerturbNormal(ForgeSurfaceFrame frame, float3 tangent_normal)
{
    if (!frame.TangentValid)
        return frame.Normal;
    tangent_normal = ForgeUnit(tangent_normal);
    float3 result = ForgeUnit(frame.Tangent * tangent_normal.x +
                             frame.Bitangent * tangent_normal.y +
                             frame.Normal * tangent_normal.z);
    return any(result != 0) ? result : frame.Normal;
}
// Pixel interpolation can break orthogonality. Prefer the authored frame and
// rebuild from the chosen texture's derivatives only when it has collapsed.
// Derivatives are evaluated by the caller before divergent shading/discard.
ForgeSurfaceFrame ForgePixelFrame(float3 normal, float3 tangent, float3 bitangent,
                                  float3 dpdx, float3 dpdy, float2 dx, float2 dy)
{
    ForgeSurfaceFrame result = (ForgeSurfaceFrame)0;
    result.Normal = ForgeUnit(normal);
    result.NormalValid = any(result.Normal != 0);
    if (!result.NormalValid)
        return result;
    float3 n = result.Normal;
    float3 t = ForgeUnit(tangent - n * dot(n, tangent));
    float3 b = ForgeUnit(bitangent);
    float handedness = dot(cross(n, t), b);
    if (abs(handedness) <= 1e-6)
    {
        float det = dx.x * dy.y - dx.y * dy.x;
        if (isfinite(det) && abs(det) > 0)
        {
            t = ForgeUnit((dpdx * dy.y - dpdy * dx.y) * (det < 0 ? -1 : 1));
            t = ForgeUnit(t - n * dot(n, t));
            b = ForgeUnit((dpdy * dx.x - dpdx * dy.x) * (det < 0 ? -1 : 1));
            handedness = dot(cross(n, t), b);
        }
    }
    result.TangentValid = abs(handedness) > 1e-6;
    if (result.TangentValid)
    {
        result.Tangent = t;
        result.Bitangent = cross(n, t) * (handedness < 0 ? -1 : 1);
    }
    return result;
}
#endif
