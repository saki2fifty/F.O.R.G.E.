#ifndef FORGE_TRANSMISSION_FXH
#define FORGE_TRANSMISSION_FXH
// Camera-cropped HDR background. Never aliases the current render target.
Texture2D g_ForgeTransmission;
cbuffer ForgeTransmission {float4 g_TransmissionViewport;float4 g_TransmissionInfo;};

float3 ForgeBackground(float2 uv,float lod) {
    return g_ForgeTransmission.SampleLevel(g_ForgeLightSampler,uv,lod).rgb;
}
float ForgeNormalThickness(float thickness,float3 geometric) {
    // A = largest * basis. ||A^T N|| is the affine scale of normal
    // thickness. This supports shear/reflections without an inverse. A surviving
    // rank-two surface has zero normal thickness; keep it a thin surface.
    if(g_Object[13].w!=0 || thickness==0)return 0;
    float3x3 basis=float3x3(g_Object[3].xyz,g_Object[4].xyz,g_Object[5].xyz);
    return thickness*g_Object[13].y*length(mul(geometric,basis));
}
float3 ForgeTransportChannel(float3 position,float3 view,float3 normal,float3 geometric,
                              float2 sightline,float normalThickness,float ior,float roughness,
                              float attenuationDistance,float3 attenuationColor,bool front) {
    float3 ray=-view;
    float distance=0;
    if(normalThickness>0) {
        // Entry and exit differ. A zero refraction vector means total internal
        // reflection: no transmitted radiance may pass this interface.
        ray=refract(-view,normal,front?1/ior:ior);
        if(!any(ray!=0))return 0;
        float cosine=abs(dot(ray,geometric));
        if(cosine==0)return 0;
        distance=normalThickness/cosine;
        if(!isfinite(distance))return float3(0,0,0);
    }
    float2 uv=sightline;
    if(distance>0) {
        float4 clip=ForgeProject(position+ray*distance);
        if(all(isfinite(clip)) && clip.w>0) {
            float2 refracted=clip.xy/clip.w*float2(.5,-.5)+.5;
            // Opaque screen-space data does not exist outside this camera.
            // Fall back to the unshifted sightline instead of edge streaks or
            // sampling a neighboring camera's image.
            if(all(refracted>=0) && all(refracted<=1))uv=refracted;
        }
    }
    float lod=roughness*saturate(2*(ior-1))*g_TransmissionInfo.x;
    float3 radiance=ForgeBackground(uv,lod);
    if(distance>0 && attenuationDistance>0) {
        float ratio=distance/attenuationDistance;
        // Separate exact zero/one endpoints, avoiding log(0) and 0*infinity.
        [unroll]for(uint c=0;c<3;c++) {
            if(attenuationColor[c]<=0)radiance[c]=0;
            else if(attenuationColor[c]<1)
                radiance[c]*=exp(log(attenuationColor[c])*ratio);
        }
    }
    return radiance;
}
float3 ForgeTransport(float3 position,float3 view,float3 normal,float3 geometric,
                      float2 pixel,float thickness,float ior,float dispersion,float roughness,
                      float attenuationDistance,float3 attenuationColor,bool front) {
    float2 uv=(pixel-g_TransmissionViewport.xy)*g_TransmissionViewport.zw;
    float worldThickness=ForgeNormalThickness(thickness,geometric);
    if(dispersion==0 || worldThickness==0)
        return ForgeTransportChannel(position,view,normal,geometric,uv,worldThickness,ior,
                                      roughness,attenuationDistance,attenuationColor,front);
    float spread=(ior-1)*(.025*dispersion);
    float3 indices=float3(max(1,ior-spread),ior,ior+spread);
    float3 result=0;
    [unroll]for(uint c=0;c<3;c++)
        result[c]=ForgeTransportChannel(position,view,normal,geometric,uv,worldThickness,indices[c],
                                       roughness,attenuationDistance,attenuationColor,front)[c];
    return result;
}
#endif
