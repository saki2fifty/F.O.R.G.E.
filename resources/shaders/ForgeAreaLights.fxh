#ifndef FORGE_AREA_LIGHTS_FXH
#define FORGE_AREA_LIGHTS_FXH
#include "ForgeLighting.fxh"
// FORGE-owned bounded emitter integration over the native material BRDF.
static const float ForgeAreaPi = 3.14159265358979323846;
struct ForgeAreaShape {
    float3 Center;
    float Radius;
    float2 HalfSize;
    uint Kind;
};
struct ForgeAreaBasis {
    float3 X, Y, Z;
};
ForgeAreaBasis ForgeAreaFrame(float3 n) {
    ForgeAreaBasis f;
    f.Z = n;
    f.X = normalize(cross(abs(n.z) < .8 ? float3(0, 0, 1) : float3(0, 1, 0), n));
    f.Y = cross(n, f.X);
    return f;
}
float3 ForgeAreaMap(ForgeAreaBasis f, float3 p) { return p.x * f.X + p.y * f.Y + p.z * f.Z; }
float ForgeAreaOneMinusCos(ForgeAreaShape a) {
    float s2 = a.Radius * a.Radius / dot(a.Center, a.Center);
    return s2 / (1 + sqrt(max(0, 1 - s2)));
}
// Condition the clipped spherical cosine integral before dividing by the
// source's projected solid angle. Small caps use a positive gnomonic integral;
// this avoids subtracting nearly equal angles at the receiver horizon.
float ForgeAreaSphereCosineNormalized(float c, float st) {
    c = clamp(c, -1, 1);
    if (st <= 0)
        return max(c, 0);
    if (st >= 1)
        return saturate(.5 * (1 + c));
    if (c >= st)
        return c;
    if (c <= -st)
        return 0;
    float ct = sqrt(max(0, 1 - st * st));
    if (st <= .5) {
        float sn = sqrt(max(0, 1 - c * c)), T = st / ct, delta = saturate(1 - abs(c) / (sn * T)),
              K = 1 + T * T, sum = 0;
        const float root = sqrt(30.), inner = sqrt((15 - 2 * root) / 35),
                    outer = sqrt((15 + 2 * root) / 35);
        const float nodes[4] = {(1 - outer) / 2, (1 - inner) / 2, (1 + inner) / 2, (1 + outer) / 2};
        const float weights[4] = {(18 - root) / 72, (18 + root) / 72, (18 + root) / 72,
                                  (18 - root) / 72};
        [unroll] for (uint i = 0; i < 8; ++i) {
            float q = .5 * (i / 4 + nodes[i % 4]), q2 = q * q, t = 1 - delta * q2,
                  A = 1 + T * T * t * t, z2 = T * T * delta * q2 * (2 - delta * q2) / A;
            float ratio = 1. / 17;
            ratio = -1. / 15 + z2 * ratio;
            ratio = 1. / 13 + z2 * ratio;
            ratio = -1. / 11 + z2 * ratio;
            ratio = 1. / 9 + z2 * ratio;
            ratio = -1. / 7 + z2 * ratio;
            ratio = 1. / 5 + z2 * ratio;
            ratio = -1. / 3 + z2 * ratio;
            ratio = 1 + z2 * ratio;
            sum += .5 * weights[i % 4] * q2 * (1 - q2) * sqrt(2 - delta * q2) *
                   (1 / (A * K) + ratio / (A * A));
        }
        return saturate(max(c, 0) + (2 / ForgeAreaPi) * sn * st / (ct * ct * ct) * delta * delta *
                                        sqrt(delta) * sum);
    }
    float t = sqrt(max(0, st * st - c * c));
    return saturate((c * st * st * atan2(t, -c * ct) - ct * t + atan2(t, ct)) /
                    (ForgeAreaPi * st * st));
}
float ForgeAreaRectangleCosineIntegral(ForgeAreaShape a, float3 n) {
    float3 original[4];
    original[0] = a.Center + float3(-a.HalfSize, 0);
    original[1] = a.Center + float3(a.HalfSize.x, -a.HalfSize.y, 0);
    original[2] = a.Center + float3(a.HalfSize, 0);
    original[3] = a.Center + float3(-a.HalfSize.x, a.HalfSize.y, 0);
    float3 polygon[6];
    uint count = 0;
    float3 previous = original[3];
    float before = dot(previous, n);
    [loop] for (uint i = 0; i < 4; ++i) {
        float3 p = original[i];
        float after = dot(p, n);
        if ((before >= 0) != (after >= 0))
            polygon[count++] = previous + (p - previous) * (before / (before - after));
        if (after >= 0)
            polygon[count++] = p;
        previous = p;
        before = after;
    }
    float3 sum = 0;
    [loop] for (uint j = 0; j < count; ++j) {
        float3 p = normalize(polygon[j]), q = normalize(polygon[(j + 1) % count]),
               axis = cross(p, q);
        float sine = length(axis);
        if (sine > 1e-7)
            sum += axis * (.5 * atan2(sine, dot(p, q)) / sine);
    }
    return max(0, dot(sum, n));
}

// Positive source-plane quadrature for small rectangular apertures. Clip in
// normalized source coordinates, where the rectangle remains well resolved.
// A degree-two triangle rule avoids subtraction of nearly equal edge vectors.
float ForgeAreaSmallRectCosine(ForgeAreaShape a, float3 n, float virtual2) {
    float d2 = dot(a.Center, a.Center), inverseD = rsqrt(d2);
    float3 center = a.Center * inverseD;
    float2 halfSize = a.HalfSize * inverseD;
    float offset = dot(center, n);
    float2 gradient = n.xy * halfSize;
    float2 original[4] = {float2(-1, -1), float2(1, -1), float2(1, 1), float2(-1, 1)}, polygon[6];
    uint count = 0;
    float2 previous = original[3];
    float before = offset + dot(gradient, previous);
    [unroll] for (uint i = 0; i < 4; ++i) {
        float2 p = original[i];
        float after = offset + dot(gradient, p);
        if ((before >= 0) != (after >= 0))
            polygon[count++] = previous + (p - previous) * (before / (before - after));
        if (after >= 0)
            polygon[count++] = p;
        previous = p;
        before = after;
    }
    float integral = 0;
    [loop] for (uint faceIndex = 1; faceIndex + 1 < count; ++faceIndex) {
        float2 v0 = polygon[0], v1 = polygon[faceIndex], v2 = polygon[faceIndex + 1], e1 = v1 - v0,
               e2 = v2 - v0;
        float twiceArea = abs(e1.x * e2.y - e1.y * e2.x);
        float sum = 0;
        [unroll] for (uint nodeIndex = 0; nodeIndex < 3; ++nodeIndex) {
            float2 uv = (v0 + v1 + v2) / 6 + .5 * (nodeIndex == 0 ? v0 : nodeIndex == 1 ? v1 : v2);
            float3 p = center + float3(uv * halfSize, 0);
            float r2 = dot(p, p);
            sum += max(0, offset + dot(gradient, uv)) / (r2 * r2);
        }
        integral += twiceArea * sum / 6;
    }
    return .25 * (virtual2 / d2) * center.z * integral;
}
float ForgeAreaRectCosineNormalized(ForgeAreaShape a, float3 n, float virtual2) {
    float d2 = dot(a.Center, a.Center);
    if (max(a.HalfSize.x, a.HalfSize.y) < .01 * sqrt(d2))
        return ForgeAreaSmallRectCosine(a, n, virtual2);
    return ForgeAreaRectangleCosineIntegral(a, n) * virtual2 / (4 * a.HalfSize.x * a.HalfSize.y);
}
float3 ForgeAreaEmitterSample(ForgeAreaShape a, float2 uv) {
    if (a.Kind != 0)
        return normalize(a.Center + float3((uv * 2 - 1) * a.HalfSize, 0));
    float gap = uv.x * ForgeAreaOneMinusCos(a), z = 1 - gap, r = sqrt(max(0, gap * (2 - gap)));
    return ForgeAreaMap(
        ForgeAreaFrame(normalize(a.Center)),
        float3(r * cos(2 * ForgeAreaPi * uv.y), r * sin(2 * ForgeAreaPi * uv.y), z));
}
float3 ForgeAreaCosineSample(float3 n, float2 uv) {
    float r = sqrt(uv.x);
    return ForgeAreaMap(ForgeAreaFrame(n), float3(r * cos(2 * ForgeAreaPi * uv.y),
                                                  r * sin(2 * ForgeAreaPi * uv.y), sqrt(1 - uv.x)));
}
float ForgeAreaCosinePdf(float3 n, float3 w) { return max(0, dot(n, w)) / ForgeAreaPi; }
// Sample the projected visible sphere, then stretch its normal distribution.
ForgeAreaBasis ForgeAreaAnisotropicFrame(float3 n, float3 tangent) {
    ForgeAreaBasis f = ForgeAreaFrame(n);
    float3 t = tangent - dot(tangent, n) * n;
    if (dot(t, t) > 1e-12) {
        f.X = normalize(t);
        f.Y = cross(n, f.X);
    }
    return f;
}
float3 ForgeAreaGgxSampleAxes(ForgeAreaBasis base, float3 view, float2 alpha, float2 uv) {
    float3 local = float3(dot(view, base.X), dot(view, base.Y), dot(view, base.Z)),
           stretched = normalize(float3(alpha * local.xy, local.z));
    float horizontal = length(stretched.xy);
    float3 toward = horizontal > 1e-6 ? float3(-stretched.z * stretched.xy / horizontal, horizontal)
                                      : float3(1, 0, 0),
           side = cross(stretched, toward);
    float radius = sqrt(uv.x), x = radius * cos(2 * ForgeAreaPi * uv.y),
          y = radius * sin(2 * ForgeAreaPi * uv.y), a = .5 * (1 + stretched.z);
    x = a * x + (1 - a) * sqrt(max(0, 1 - y * y));
    float3 sphere = stretched * sqrt(max(0, 1 - x * x - y * y)) + toward * x + side * y,
           half_normal = ForgeAreaMap(base, normalize(float3(alpha * sphere.xy, max(0, sphere.z))));
    return 2 * dot(view, half_normal) * half_normal - view;
}
float ForgeAreaGgxPdfAxes(ForgeAreaBasis base, float3 view, float2 alpha, float3 w) {
    float3 h = w + view;
    if (dot(h, h) < 1e-12)
        return 0;
    h = normalize(h);
    float nh = dot(base.Z, h), nv = dot(base.Z, view);
    if (nh <= 0 || nv <= 0 || dot(view, h) <= 0)
        return 0;
    float2 xy = float2(dot(base.X, h), dot(base.Y, h)) / alpha,
           vxy = alpha * float2(dot(base.X, view), dot(base.Y, view));
    float denom = dot(xy, xy) + nh * nh, D = 1 / (ForgeAreaPi * alpha.x * alpha.y * denom * denom),
          G1 = 2 * nv / (nv + sqrt(nv * nv + dot(vxy, vxy)));
    return D * G1 / (4 * nv);
}
// Normalize every density by projected area / virtual distance squared.
// The native point light then supplies the matching inverse-square factor.
// This keeps a distant small emitter's density and radiance representable.
float ForgeAreaProjectedArea(ForgeAreaShape a) {
    return a.Kind == 0 ? ForgeAreaPi * a.Radius * a.Radius : 4 * a.HalfSize.x * a.HalfSize.y;
}
float ForgeAreaEmitterDensity(ForgeAreaShape a, float3 w, float virtual2, bool sampledEmitter) {
    float d2 = dot(a.Center, a.Center);
    if (a.Kind == 0) {
        float3 axis = a.Center * rsqrt(d2), sine = cross(w, axis);
        float st2 = a.Radius * a.Radius / d2;
        return (sampledEmitter || (dot(w, axis) > 0 && dot(sine, sine) <= st2 * (1 + 4e-6)))
                   ? d2 * (1 + sqrt(max(0, 1 - st2))) / (2 * virtual2)
                   : 0;
    }
    if (w.z <= 0)
        return 0;
    float t = a.Center.z / w.z;
    float3 q = t * w - a.Center;
    if (!sampledEmitter && any(abs(q.xy) > a.HalfSize * (1 + 1e-6)))
        return 0;
    return (a.Center.z * a.Center.z / virtual2) / (w.z * w.z * w.z);
}
bool ForgeAreaBeam(SurfaceShadingInfo s, PBRLightAttribs light, float3 w, float distance,
#if ENABLE_SHEEN
                   Texture2D sheen, SamplerState sheen_sampler,
#endif
                   out SurfaceLightingInfo result) {
    result = GetDefaultSurfaceLightingInfo();
    s.Pos = 0;
    light.Type = PBR_LIGHT_TYPE_POINT;
    light.PosX = w.x * distance;
    light.PosY = w.y * distance;
    light.PosZ = w.z * distance;
    light.Range4 = 0;
    light.ShadowMapIndex = -1;
    return ForgeApplyPunctualLight(s, light,
#if ENABLE_SHEEN
                                   sheen, sheen_sampler,
#endif
                                   result);
}
bool ForgeAreaNativeLighting(ForgeAreaShape area, SurfaceShadingInfo s, PBRLightAttribs light,
                             uint4 counts, float phase,
#if ENABLE_SHEEN
                             Texture2D sheen, SamplerState sheen_sampler,
#endif
                             out SurfaceLightingInfo lighting) {
    lighting = GetDefaultSurfaceLightingInfo();
    float d2 = dot(area.Center, area.Center), A = ForgeAreaProjectedArea(area);
    if (!isfinite(d2) || !isfinite(A) || A <= 0)
        return false;
    if (d2 <= 0)
        return true;
    if (area.Kind == 0 && d2 <= area.Radius * area.Radius)
        return true;
    if (area.Kind != 0 && area.Center.z <= 0)
        return true;
    float virtual2 = max(d2, A), distance = sqrt(virtual2), inverse_density = A / virtual2;
    bool valid = true;
    // Obtain finite-range fade from the native point-light contract at the actual
    // center distance. Cancel only inverse square; no native attenuation is copied.
    if (light.Range4 > 0) {
        SurfaceShadingInfo probe = (SurfaceShadingInfo)0;
        probe.View = float3(0, 0, 1);
        probe.BaseLayer.Normal = probe.View;
        probe.BaseLayer.NdotV = 1;
        probe.BaseLayer.Srf.DiffuseColor = ForgeAreaPi;
#if ENABLE_CLEAR_COAT
        probe.Clearcoat.Normal = probe.View;
#endif
#if ENABLE_ANISOTROPY
        probe.Anisotropy.Tangent = float3(1, 0, 0);
        probe.Anisotropy.Bitangent = float3(0, 1, 0);
        probe.Anisotropy.AlphaRoughnessT = probe.Anisotropy.AlphaRoughnessB = 1;
#endif
        PBRLightAttribs rangeLight = light;
        rangeLight.Type = PBR_LIGHT_TYPE_POINT;
        rangeLight.PosX = rangeLight.PosY = 0;
        rangeLight.PosZ = sqrt(d2);
        rangeLight.IntensityR = rangeLight.IntensityG = rangeLight.IntensityB = d2;
        rangeLight.ShadowMapIndex = -1;
        SurfaceLightingInfo fade = GetDefaultSurfaceLightingInfo();
        valid = ForgeApplyPunctualLight(probe, rangeLight,
#if ENABLE_SHEEN
                                        sheen, sheen_sampler,
#endif
                                        fade);
        light.IntensityR *= fade.Base.Punctual.x;
        light.IntensityG *= fade.Base.Punctual.x;
        light.IntensityB *= fade.Base.Punctual.x;
    }
    if (all(float3(light.IntensityR, light.IntensityG, light.IntensityB) == 0))
        return valid;
    float3 n = s.BaseLayer.Normal, view = s.View;
    float alpha =
        max(.001, s.BaseLayer.Srf.PerceptualRoughness * s.BaseLayer.Srf.PerceptualRoughness);
    ForgeAreaBasis base = ForgeAreaFrame(n);
    float2 baseAlpha = alpha;
#if ENABLE_ANISOTROPY
    base = ForgeAreaAnisotropicFrame(n, s.Anisotropy.Tangent);
    baseAlpha = max(.001, float2(s.Anisotropy.AlphaRoughnessT, s.Anisotropy.AlphaRoughnessB));
#endif
#if ENABLE_CLEAR_COAT
    ForgeAreaBasis coatFrame = ForgeAreaFrame(s.Clearcoat.Normal);
    float coatAlpha =
        max(.001, s.Clearcoat.Srf.PerceptualRoughness * s.Clearcoat.Srf.PerceptualRoughness);
    if (s.Clearcoat.Factor <= 0 || dot(s.Clearcoat.Normal, view) <= 0)
        counts.w = 0;
#else
    counts.w = 0;
#endif
    SurfaceLightingInfo withDiffuse, withoutDiffuse;
    valid = ForgeAreaBeam(s, light, n, distance,
#if ENABLE_SHEEN
                          sheen, sheen_sampler,
#endif
                          withDiffuse) &&
            valid;
    SurfaceShadingInfo specular = s;
    specular.BaseLayer.Srf.DiffuseColor = 0;
    valid = ForgeAreaBeam(specular, light, n, distance,
#if ENABLE_SHEEN
                          sheen, sheen_sampler,
#endif
                          withoutDiffuse) &&
            valid;
    float3 control = withDiffuse.Base.Punctual - withoutDiffuse.Base.Punctual;
    float normalizedCosine = area.Kind == 0
                                 ? ForgeAreaSphereCosineNormalized(dot(normalize(area.Center), n),
                                                                   area.Radius * rsqrt(d2)) *
                                       virtual2 / d2
                                 : ForgeAreaRectCosineNormalized(area, n, virtual2);
    lighting.Base.Punctual = control * normalizedCosine;
    [loop] for (uint kind = 0; kind < 4; ++kind) {
        uint count = counts[kind];
        [loop] for (uint i = 0; i < count; ++i) {
            float2 uv = float2((i + .5) / count,
                               frac(i * .6180339887498948482 + kind * .2718281828459045 + phase));
            float3 w = kind == 0   ? ForgeAreaEmitterSample(area, uv)
                       : kind == 1 ? ForgeAreaCosineSample(n, uv)
                                   : ForgeAreaGgxSampleAxes(base, view, baseAlpha, uv);
#if ENABLE_CLEAR_COAT
            if (kind == 3)
                w = ForgeAreaGgxSampleAxes(coatFrame, view, coatAlpha, uv);
#endif
            float p = ForgeAreaEmitterDensity(area, w, virtual2, kind == 0),
                  denom =
                      counts.x * p +
                      inverse_density * (counts.y * ForgeAreaCosinePdf(n, w) +
                                         counts.z * ForgeAreaGgxPdfAxes(base, view, baseAlpha, w));
#if ENABLE_CLEAR_COAT
            denom +=
                inverse_density * counts.w * ForgeAreaGgxPdfAxes(coatFrame, view, coatAlpha, w);
#endif
            if (p > 0 && denom > 0) {
                SurfaceLightingInfo result;
                valid = ForgeAreaBeam(s, light, w, distance,
#if ENABLE_SHEEN
                                      sheen, sheen_sampler,
#endif
                                      result) &&
                        valid;
                lighting.Base.Punctual +=
                    (result.Base.Punctual - control * max(0, dot(n, w))) / denom;
#if ENABLE_CLEAR_COAT
                lighting.Clearcoat.Punctual += result.Clearcoat.Punctual / denom;
#endif
#if ENABLE_SHEEN
                lighting.Sheen.Punctual += result.Sheen.Punctual / denom;
#endif
            }
        }
    }
    lighting.Base.Punctual = max(lighting.Base.Punctual, 0);
    return valid && all(isfinite(lighting.Base.Punctual))
#if ENABLE_CLEAR_COAT
           && all(isfinite(lighting.Clearcoat.Punctual))
#endif
#if ENABLE_SHEEN
           && all(isfinite(lighting.Sheen.Punctual))
#endif
        ;
}

float3 ForgeAreaLocalVector(ForgeAreaBasis frame, float3 value) {
    return float3(dot(frame.X, value), dot(frame.Y, value), dot(frame.Z, value));
}
// Packet rows follow the unchanged native Light prefix: radius/full width/full
// height, world right, world up. Light types4/5 append Rectangle/Sphere to1/2/3.
// Rectangle frame Z opposes the emitting forward axis.
bool ForgeApplyAreaLight(SurfaceShadingInfo s, PBRLightAttribs light, float3 size, float3 right,
                         float3 up,
#if ENABLE_SHEEN
                         Texture2D sheen, SamplerState sheen_sampler,
#endif
                         inout SurfaceLightingInfo lighting) {
    if (light.Type != 4 && light.Type != 5)
        return false;
    ForgeAreaShape area;
    area.Center = float3(light.PosX, light.PosY, light.PosZ) - s.Pos;
    area.Radius = size.x;
    area.HalfSize = .5 * size.yz;
    area.Kind = light.Type == 4 ? 1 : 0;
    if (area.Kind == 1) {
        float3 back = cross(right, up);
        if (dot(right, right) < 1e-12 || dot(back, back) < 1e-12)
            return false;
        ForgeAreaBasis frame;
        frame.X = normalize(right);
        frame.Z = normalize(back);
        frame.Y = cross(frame.Z, frame.X);
        area.Center = ForgeAreaLocalVector(frame, area.Center);
        s.View = normalize(ForgeAreaLocalVector(frame, s.View));
        s.BaseLayer.Normal = normalize(ForgeAreaLocalVector(frame, s.BaseLayer.Normal));
        s.BaseLayer.NdotV = saturate(dot(s.BaseLayer.Normal, s.View));
#if ENABLE_CLEAR_COAT
        s.Clearcoat.Normal = normalize(ForgeAreaLocalVector(frame, s.Clearcoat.Normal));
#endif
#if ENABLE_ANISOTROPY
        s.Anisotropy.Tangent = ForgeAreaLocalVector(frame, s.Anisotropy.Tangent);
        s.Anisotropy.Bitangent = ForgeAreaLocalVector(frame, s.Anisotropy.Bitangent);
#endif
    }
    SurfaceLightingInfo contribution;
    bool valid = ForgeAreaNativeLighting(area, s, light, uint4(32, 8, 16, 16), 0,
#if ENABLE_SHEEN
                                         sheen, sheen_sampler,
#endif
                                         contribution);
    if (valid)
        valid = ForgeAccumulatePunctualLighting(contribution, lighting);
    return valid;
}
#endif
