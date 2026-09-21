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
    for (const char* name : {"transmissionFactor", "thicknessFactor", "dispersion"}) {
        auto found = profile.values.parameters.find(name);
        require(found == profile.values.parameters.end() || found->second.value[0] == 0,
                std::string("extended lighting consumer unavailable: ") + name);
    }
    const bool sheen =
        profile.workflow == PbrWorkflow::MetallicRoughness &&
        profile.values.parameters.at("sheenColorFactor").value != std::array<float, 4>{};
    const bool anisotropy = profile.workflow == PbrWorkflow::MetallicRoughness &&
                            profile.values.parameters.at("anisotropyStrength").value[0] > 0;
    for (const auto& [role, slot] : profile.values.textures) {
        (void)slot;
        require(role == "baseColorTexture" || role == "diffuseTexture" ||
                    role == "metallicRoughnessTexture" || role == "specularGlossinessTexture" ||
                    role == "normalTexture" || role == "occlusionTexture" ||
                    role == "emissiveTexture" || role == "specularTexture" ||
                    role == "specularColorTexture" || role == "clearcoatTexture" ||
                    role == "clearcoatRoughnessTexture" || role == "clearcoatNormalTexture" ||
                    role == "iridescenceTexture" || role == "iridescenceThicknessTexture" ||
                    role == "sheenColorTexture" || role == "sheenRoughnessTexture" ||
                    role == "anisotropyTexture",
                "extended texture consumer unavailable: " + role);
    }
    if (profile.values.textures.contains("clearcoatNormalTexture"))
        require((fetch.normal && fetch.tangent) ||
                    profile.values.textures.contains("normalTexture"),
                "clearcoat normal map requires authored normal/tangent or a base normal map");
    if (anisotropy || profile.values.textures.contains("anisotropyTexture"))
        require((fetch.normal && fetch.tangent) ||
                    profile.values.textures.contains("normalTexture"),
                "anisotropy requires authored normal/tangent or a base normal map");
    const auto material = material_shader(profile, fetch.uv_sets);
    const auto uv_count = std::max<std::size_t>(1, fetch.uv_sets.size());
    const std::string varyings =
        "struct ForgeVarying {float4 Position:SV_Position;float3 World:TEXCOORD0;"
        "float3 Normal:TEXCOORD1;float3 Tangent:TEXCOORD2;float3 Bitangent:TEXCOORD3;"
        "float4 Color:COLOR0;float2 UV[" +
        std::to_string(uv_count) + "]:TEXCOORD4;};\n";
    const std::string vs = std::string("#include \"ForgeSurface.fxh\"\n") + object_source +
                           fetch.source + varyings + R"(
ForgeVarying main(uint id:SV_VertexID) {
    ForgeMeshVertex v=ForgeLoadMeshVertex(id);
    ForgeVarying o=(ForgeVarying)0;
    o.World=ForgePoint(v.Position);o.Position=ForgeProject(o.World);o.Color=v.Color;
    float3x3 basis=float3x3(g_Object[3].xyz,g_Object[4].xyz,g_Object[5].xyz);
    ForgeSurfaceFrame frame=ForgeMakeSurfaceFrame(basis,v.Normal,v.Tangent);
    o.Normal=frame.Normal;o.Tangent=frame.Tangent;o.Bitangent=frame.Bitangent;
    )" + "[unroll]for(uint i=0;i<" +
                           std::to_string(uv_count) + ";i++)o.UV[i]=v.UV[i];return o;}\n";
    std::string ps = "#define USE_IBL 0\n#define TEX_COLOR_CONVERSION_MODE 0\n";
    ps += std::string("#define ENABLE_CLEAR_COAT ") +
          (profile.workflow == PbrWorkflow::MetallicRoughness ? "1\n" : "0\n");
    ps += std::string("#define ENABLE_IRIDESCENCE ") +
          (profile.workflow == PbrWorkflow::MetallicRoughness ? "1\n" : "0\n");
    ps += std::string("#define ENABLE_SHEEN ") + (sheen ? "1\n" : "0\n");
    ps += std::string("#define ENABLE_ANISOTROPY ") + (anisotropy ? "1\n" : "0\n");
    ps += "#include \"ForgeSurface.fxh\"\n#include \"ForgeLighting.fxh\"\n";
    if (profile.workflow == PbrWorkflow::MetallicRoughness)
        ps += "#include \"Iridescence.fxh\"\n";
    ps += object_source + varyings + material.source;
    if (sheen)
        ps += "Texture2D g_ForgeSheen;SamplerState g_ForgeSheen_sampler;\n";
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
    for (const auto* role : {"normalTexture", "clearcoatNormalTexture"}) {
        if (!profile.values.textures.contains(role))
            continue;
        const auto slot = std::find_if(material.textures.begin(), material.textures.end(),
                                       [role](const auto& t) { return t.role == role; });
        const std::string name = role;
        ps += "float2 uv_" + name + "=ForgeUV_" + name + "(input.UV[" +
              std::to_string(slot->uv_slot) + "]);float2 dx_" + name + "=ddx(uv_" + name + "),dy_" +
              name + "=ddy(uv_" + name + ");\n";
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
    float face=front?1:-1;
    float3 n=ForgeUnit(input.Normal);
    if(!any(n!=0)) {
        n=ForgeUnit(cross(dpdx,dpdy));
        if(dot(n,s.View)<0)n=-n;
        n*=face;
    }
    if(!any(n!=0)||!any(s.View!=0))return float4(1,0,1,1);
)";
        ps += "float3 geometric=n;\n";
        auto mapped_normal = [&](const char* role, const char* scale, const char* destination) {
            if (!profile.values.textures.contains(role))
                return;
            const std::string name = role;
            ps +=
                "{ForgeSurfaceFrame frame=ForgePixelFrame(geometric,input.Tangent,input.Bitangent,"
                "dpdx,dpdy,dx_" +
                name + ",dy_" + name + ");float3 mapped=sample_" + name +
                ".xyz*2-1;mapped.xy*=ForgeParameter_" + scale + "();" + destination +
                "=ForgePerturbNormal(frame,mapped); }\n";
        };
        mapped_normal("normalTexture", "normalScale", "n");
        ps += "n*=face;s.BaseLayer.Normal=n;s.BaseLayer.NdotV=saturate(dot(n,s.View));\n";
        if (profile.workflow == PbrWorkflow::MetallicRoughness) {
            if (sheen) {
                ps += "s.Sheen.Color=ForgeParameter_sheenColorFactor();"
                      "s.Sheen.Roughness=ForgeParameter_sheenRoughnessFactor();\n";
                sample("sheenColorTexture", "s.Sheen.Color*=sample_sheenColorTexture.rgb");
                sample("sheenRoughnessTexture",
                       "s.Sheen.Roughness*=sample_sheenRoughnessTexture.a");
            }
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
                "float3 dielectric=min(ForgeDielectricF0()*specular,1)*weight;\n"
                "specular=min(ForgeDielectricF0()*specular,1)/.04;\n"
                "s.BaseLayer.Srf=GetSurfaceReflectanceMR(base.rgb,metal,rough,specular,weight);\n";
            ps += "float film=ForgeParameter_iridescenceFactor();"
                  "float thickness=ForgeParameter_iridescenceThicknessMaximum();\n";
            sample("iridescenceTexture", "film*=sample_iridescenceTexture.r");
            sample("iridescenceThicknessTexture",
                   "thickness=lerp(ForgeParameter_iridescenceThicknessMinimum(),"
                   "ForgeParameter_iridescenceThicknessMaximum(),sample_"
                   "iridescenceThicknessTexture.g)");
            ps += R"(
    if(film>0 && thickness>0) {
        float filmIor=ForgeParameter_iridescenceIor();
        float3 dielectricFilm=EvalIridescence(1,filmIor,s.BaseLayer.NdotV,thickness,dielectric);
        float3 metallicFilm=EvalIridescence(1,filmIor,s.BaseLayer.NdotV,thickness,base.rgb);
        s.BaseLayer.Srf.IridescenceFresnel=lerp(dielectricFilm,metallicFilm,metal);
        s.BaseLayer.Srf.IridescenceFactor=film;
        s.Iridescence.Thickness=thickness;
    }
)";
        } else {
            ps += "float4 "
                  "physical=float4(ForgeParameter_specularFactor(),ForgeParameter_glossinessFactor("
                  "));\n";
            sample("specularGlossinessTexture", "physical*=sample_specularGlossinessTexture");
            ps += "s.BaseLayer.Srf=GetSurfaceReflectance(PBR_WORKFLOW_SPECULAR_GLOSSINESS,base,"
                  "physical,s.BaseLayer.Metallic);\n";
        }
        if (anisotropy) {
            const std::string derivatives = profile.values.textures.contains("normalTexture")
                                                ? "dx_normalTexture,dy_normalTexture"
                                                : "float2(0,0),float2(0,0)";
            ps += "ForgeSurfaceFrame anisoFrame=ForgePixelFrame(geometric,input.Tangent,"
                  "input.Bitangent,dpdx,dpdy," +
                  derivatives + ");\n";
            ps += "float2 direction=float2(1,0);float "
                  "strength=ForgeParameter_anisotropyStrength();\n";
            sample(
                "anisotropyTexture",
                "direction=sample_anisotropyTexture.rg*2-1;strength*=sample_anisotropyTexture.b");
            ps += R"(
    float rotation=ForgeParameter_anisotropyRotation();
    direction=float2(direction.x*cos(rotation)-direction.y*sin(rotation),
                     direction.x*sin(rotation)+direction.y*cos(rotation));
    if(!anisoFrame.TangentValid||!any(direction!=0))return float4(1,0,1,1);
    s.Anisotropy.Direction=direction;s.Anisotropy.Strength=strength;
    s.Anisotropy.Tangent=ForgeUnit(anisoFrame.Tangent*direction.x+anisoFrame.Bitangent*direction.y)*face;
    s.Anisotropy.Bitangent=ForgeUnit(cross(geometric*face,s.Anisotropy.Tangent));
    float alpha=s.BaseLayer.Srf.PerceptualRoughness*s.BaseLayer.Srf.PerceptualRoughness;
    s.Anisotropy.AlphaRoughnessT=lerp(alpha,1,strength*strength);
    s.Anisotropy.AlphaRoughnessB=alpha;
)";
        }
        if (profile.workflow == PbrWorkflow::MetallicRoughness) {
            ps += "float "
                  "coat=ForgeParameter_clearcoatFactor(),coatRough=ForgeParameter_"
                  "clearcoatRoughnessFactor();\n";
            sample("clearcoatTexture", "coat*=sample_clearcoatTexture.r");
            sample("clearcoatRoughnessTexture", "coatRough*=sample_clearcoatRoughnessTexture.g");
            ps += "float3 coatNormal=geometric;\n";
            mapped_normal("clearcoatNormalTexture", "clearcoatNormalScale", "coatNormal");
            ps += "s.Clearcoat.Normal=coatNormal*face;s.Clearcoat.Factor=coat;"
                  "s.Clearcoat.Srf=GetSurfaceReflectanceClearCoat(coatRough,1.5);\n";
        }
        ps += "s.Occlusion=1;s.IBLScale=0;\n"
              "s.Emissive=ForgeParameter_emissiveFactor()*ForgeParameter_emissiveStrength();\n";
        sample("occlusionTexture",
               "s.Occlusion=lerp(1,sample_occlusionTexture.r,ForgeParameter_occlusionStrength())");
        sample("emissiveTexture", "s.Emissive*=sample_emissiveTexture.rgb");
        ps += R"(
    SurfaceLightingInfo lighting=GetDefaultSurfaceLightingInfo();
    [loop]for(uint i=0;i<(uint)g_Object[13].x;i++)valid=ForgeApplyPunctualLight(s,g_Lights[i],
#if ENABLE_SHEEN
        g_ForgeSheen,g_ForgeSheen_sampler,
#endif
        lighting)&&valid;
    float3 color=ResolveLighting(s,lighting);
    if(!valid||!all(isfinite(color)))return float4(1,0,1,1);
    return float4(color,base.a);
})";
    }

    return {vs, ps, material, sheen};
}
} // namespace forge
