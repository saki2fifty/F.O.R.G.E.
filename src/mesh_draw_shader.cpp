#include "mesh_draw_shader.hpp"
#include <algorithm>
#include <stdexcept>
namespace forge {
namespace {
void require(bool ok, const std::string& message) {
    if (!ok)
        throw std::runtime_error("Mesh draw: " + message);
}
constexpr const char* object_source = R"(
cbuffer ForgeObject {float4 g_Object[14];};
float3 ForgePoint(float3 p) {
    float4 v=float4(p,1);
    return float3(dot(g_Object[0],v),dot(g_Object[1],v),dot(g_Object[2],v));
}
float4 ForgeProject(float3 p) {
    float4 v=float4(dot(g_Object[6].xyz,p),dot(g_Object[7].xyz,p),dot(g_Object[8].xyz,p),1);
    return float4(dot(g_Object[9],v),dot(g_Object[10],v),dot(g_Object[11],v),dot(g_Object[12],v));
}
)";
} // namespace
MeshDrawShader mesh_draw_shader(const MeshVertexFetch& fetch, const PbrMaterialProfile& profile) {
    const auto& source = profile.values;
    // These checks remain until the full extended lighting/pass consumer is wired.
    // Never render authored extension controls as if ignored values were supported.
    for (const char* name :
         {"clearcoatFactor", "sheenRoughnessFactor", "anisotropyStrength", "iridescenceFactor",
          "transmissionFactor", "thicknessFactor", "dispersion"}) {
        auto found = profile.values.parameters.find(name);
        require(found == profile.values.parameters.end() || found->second.value[0] == 0,
                std::string("extended lighting consumer unavailable: ") + name);
    }
    if (auto found = profile.values.parameters.find("sheenColorFactor");
        found != profile.values.parameters.end())
        require(found->second.value == std::array<float, 4>{},
                "sheen lighting consumer unavailable");
    for (const auto& [role, slot] : profile.values.textures) {
        (void)slot;
        require(role == "baseColorTexture" || role == "diffuseTexture" ||
                    role == "metallicRoughnessTexture" || role == "specularGlossinessTexture" ||
                    role == "normalTexture" || role == "occlusionTexture" ||
                    role == "emissiveTexture" || role == "specularTexture" ||
                    role == "specularColorTexture",
                "extended texture consumer unavailable: " + role);
    }
    const auto material = material_shader(profile, fetch.uv_sets);
    const auto uv_count = std::max<std::size_t>(1, fetch.uv_sets.size());
    const std::string varyings =
        "struct ForgeVarying {float4 Position:SV_Position;float3 World:TEXCOORD0;"
        "float3 Normal:TEXCOORD1;float4 Color:COLOR0;float2 UV[" +
        std::to_string(uv_count) + "]:TEXCOORD2;};\n";
    const std::string vs = std::string("#include \"ForgeSurface.fxh\"\n") + object_source +
                           fetch.source + varyings + R"(
ForgeVarying main(uint id:SV_VertexID) {
    ForgeMeshVertex v=ForgeLoadMeshVertex(id);
    ForgeVarying o=(ForgeVarying)0;
    o.World=ForgePoint(v.Position);o.Position=ForgeProject(o.World);o.Color=v.Color;
    float3x3 basis=float3x3(g_Object[3].xyz,g_Object[4].xyz,g_Object[5].xyz);
    o.Normal=ForgeMakeSurfaceFrame(basis,v.Normal,v.Tangent).Normal;
    )" + "[unroll]for(uint i=0;i<" +
                           std::to_string(uv_count) + ";i++)o.UV[i]=v.UV[i];return o;}\n";
    std::string ps = "#define USE_IBL 0\n#define TEX_COLOR_CONVERSION_MODE 0\n"
                     "#include \"ForgeSurface.fxh\"\n#include \"ForgeLighting.fxh\"\n";
    ps += object_source + varyings + material.source;
    ps += "cbuffer ForgeLights {PBRLightAttribs g_Lights[" + std::to_string(mesh_draw_light_limit) +
          "];};\n";
    ps += R"(
float4 main(ForgeVarying input,bool front:SV_IsFrontFace):SV_Target0 {
    float3 dpdx=ddx(input.World),dpdy=ddy(input.World);
    bool valid=true;
)";
    for (const auto& slot : material.textures) {
        ps += "bool ok_" + slot.role + ";float4 sample_" + slot.role + "=ForgeSample_" + slot.role +
              "(input.UV[" + std::to_string(slot.uv_slot) + "],ok_" + slot.role +
              ");valid=valid&&ok_" + slot.role + ";\n";
    }
    if (profile.values.textures.contains("normalTexture")) {
        const auto slot = std::find_if(material.textures.begin(), material.textures.end(),
                                       [](const auto& t) { return t.role == "normalTexture"; });
        ps += "float2 uv=ForgeUV_normalTexture(input.UV[" + std::to_string(slot->uv_slot) +
              "]);float2 dx=ddx(uv),dy=ddy(uv);\n";
    }
    ps += "if(!valid)return float4(1,0,1,1);\nfloat4 "
          "base=ForgeParameter_baseColorFactor()*input.Color;\n";
    auto sample = [&](const char* role, const char* expression) {
        if (profile.values.textures.contains(role))
            ps += std::string(expression) + ";\n";
    };
    sample("baseColorTexture", "base*=sample_baseColorTexture");
    sample("diffuseTexture", "base*=sample_diffuseTexture");
    if (source.alpha == MaterialAlpha::Mask)
        ps += "if(base.a<ForgeAlphaCutoff())discard;\n";
    if (source.alpha != MaterialAlpha::Blend)
        ps += "base.a=1;\n";
    if (profile.workflow == PbrWorkflow::Unlit)
        ps += "return all(isfinite(base))?base:float4(1,0,1,1);}\n";
    else {
        ps += R"(
    SurfaceShadingInfo s=(SurfaceShadingInfo)0;
    s.Pos=input.World;s.View=ForgeUnit(-input.World);
    float3 n=ForgeUnit(input.Normal);
    if(!any(n!=0)) {
        n=ForgeUnit(cross(dpdx,dpdy));
        if(dot(n,s.View)<0)n=-n;
    } else if(!front)n=-n;
    if(!any(n!=0)||!any(s.View!=0))return float4(1,0,1,1);
)";
        if (profile.values.textures.contains("normalTexture")) {
            ps += R"(
    float det=dx.x*dy.y-dx.y*dy.x;
    float3 p=dpdx,q=dpdy;
    if(isfinite(det)&&abs(det)>0) {
        float3 t=ForgeUnit((p*dy.y-q*dx.y)*(det<0?-1:1));
        t=ForgeUnit(t-n*dot(n,t));
        float3 b=ForgeUnit((q*dx.x-p*dy.x)*(det<0?-1:1));
        float sign=dot(cross(n,t),b);
        if(abs(sign)>1e-6) {
            float3 sampled=sample_normalTexture.xyz*2-1;
            sampled.xy*=ForgeParameter_normalScale();
            float3 candidate=ForgeUnit(t*sampled.x+cross(n,t)*(sign<0?-1:1)*sampled.y+n*sampled.z);
            if(any(candidate!=0))n=candidate;
        }
    }
)";
        }
        ps += "s.BaseLayer.Normal=n;s.BaseLayer.NdotV=saturate(dot(n,s.View));\n";
        if (profile.workflow == PbrWorkflow::MetallicRoughness) {
            ps += "float "
                  "rough=ForgeParameter_roughnessFactor(),metal=ForgeParameter_metallicFactor();\n"
                  "float weight=ForgeParameter_specularFactor();float3 "
                  "specular=ForgeParameter_specularColorFactor();\n";
            sample("metallicRoughnessTexture", "rough*=sample_metallicRoughnessTexture.g;metal*="
                                               "sample_metallicRoughnessTexture.b");
            sample("specularTexture", "weight*=sample_specularTexture.a");
            sample("specularColorTexture", "specular*=sample_specularColorTexture.rgb");
            ps +=
                "s.BaseLayer.Metallic=metal;\n"
                // Native five-argument MR fixes dielectric F0 at .04. Adapt its
                // color input to the admitted IOR F0 without copying the BRDF.
                "specular=min(ForgeDielectricF0()*specular,1)/.04;\n"
                "s.BaseLayer.Srf=GetSurfaceReflectanceMR(base.rgb,metal,rough,specular,weight);\n";
        } else {
            ps += "float4 "
                  "physical=float4(ForgeParameter_specularFactor(),ForgeParameter_glossinessFactor("
                  "));\n";
            sample("specularGlossinessTexture", "physical*=sample_specularGlossinessTexture");
            ps += "s.BaseLayer.Srf=GetSurfaceReflectance(PBR_WORKFLOW_SPECULAR_GLOSSINESS,base,"
                  "physical,s.BaseLayer.Metallic);\n";
        }
        ps += "s.Occlusion=1;s.IBLScale=0;\n"
              "s.Emissive=ForgeParameter_emissiveFactor()*ForgeParameter_emissiveStrength();\n";
        sample("occlusionTexture",
               "s.Occlusion=lerp(1,sample_occlusionTexture.r,ForgeParameter_occlusionStrength())");
        sample("emissiveTexture", "s.Emissive*=sample_emissiveTexture.rgb");
        ps += R"(
    SurfaceLightingInfo lighting=GetDefaultSurfaceLightingInfo();
    [loop]for(uint i=0;i<(uint)g_Object[13].x;i++)valid=ForgeApplyPunctualLight(s,g_Lights[i],lighting)&&valid;
    float3 color=ResolveLighting(s,lighting);
    if(!valid||!all(isfinite(color)))return float4(1,0,1,1);
    return float4(color,base.a);
})";
    }

    return {vs, ps, material};
}
} // namespace forge
