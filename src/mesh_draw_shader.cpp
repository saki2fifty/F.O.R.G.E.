#include "mesh_draw_shader.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace forge {
namespace {
void require(bool ok, const std::string& message) {
    if (!ok)
        throw std::runtime_error("Mesh draw: " + message);
}
constexpr const char* object_source = R"(
cbuffer ForgeObject {float4 g_Object[15];};
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
MeshGeometryShader mesh_geometry_shader(const MeshVertexFetch& fetch, bool allow_instances) {
    const bool instanced = allow_instances && !fetch.skin && !fetch.morph_count;
    const auto uv_count = std::max<std::size_t>(1, fetch.uv_sets.size());
    const std::string skin = fetch.skin ? R"(
// Common positive normalization avoids overflowing weighted linear matrices.
// Translations are independently camera relative before upload.
cbuffer ForgeSkin {float4 g_SkinInfo;float4 g_SkinRows[768];};
)"
                                        : "";
    std::string varyings =
        "struct ForgeVarying {float4 Position:SV_Position;float3 World:TEXCOORD0;"
        "float3 Normal:TEXCOORD1;float3 Tangent:TEXCOORD2;float3 Bitangent:TEXCOORD3;"
        "float4 Color:COLOR0;nointerpolation float4 LegacyTint:COLOR1;float2 UV[" +
        std::to_string(uv_count) + "]:TEXCOORD4;";
    if (fetch.skin)
        varyings += "float3 Source:TEXCOORD" + std::to_string(4 + uv_count) +
                    ";float3 SkinRows[3]:TEXCOORD" + std::to_string(5 + uv_count) + ";";
    varyings += "};\n";
    std::string vs = std::string("#include \"ForgeSurface.fxh\"\n") + object_source + skin +
                     fetch.source + varyings;
    if (instanced)
        vs += "struct ForgeInstance {float4 R0:ATTRIB0;float4 R1:ATTRIB1;float4 R2:ATTRIB2;"
              "float4 B0:ATTRIB3;float4 B1:ATTRIB4;float4 B2:ATTRIB5;float4 Tint:ATTRIB6;};\n";
    vs += std::string("ForgeVarying main(uint id:SV_VertexID") +
          (instanced ? ",ForgeInstance instance" : "") + R"() {
    ForgeMeshVertex v=ForgeLoadMeshVertex(id);
    ForgeVarying o=(ForgeVarying)0;
    float3x3 basis;
)";
    if (fetch.skin) {
        vs += R"(
    float4 weights=v.Weights/dot(v.Weights,float4(1,1,1,1));
    float4 rows[3];
    [unroll]for(uint r=0;r<3;r++) {
        rows[r]=weights.x*g_SkinRows[3*v.Joints.x+r]+weights.y*g_SkinRows[3*v.Joints.y+r]+
                weights.z*g_SkinRows[3*v.Joints.z+r]+weights.w*g_SkinRows[3*v.Joints.w+r];
    }
    basis=float3x3(rows[0].xyz,rows[1].xyz,rows[2].xyz);
    o.World=mul(basis,v.Position)*g_SkinInfo.x+float3(rows[0].w,rows[1].w,rows[2].w);
)";
        vs += "o.Source=v.Position;[unroll]for(uint k=0;k<3;k++)o.SkinRows[k]=basis[k];\n";
    } else if (instanced) {
        vs += "float4 position4=float4(v.Position,1);o.World=float3(dot(instance.R0,position4),"
              "dot(instance.R1,position4),dot(instance.R2,position4));"
              "basis=float3x3(instance.B0.xyz,instance.B1.xyz,instance.B2.xyz);\n";
    } else
        vs += "o.World=ForgePoint(v.Position);basis=float3x3(g_Object[3].xyz,g_Object[4].xyz,g_"
              "Object[5].xyz);\n";
    vs += instanced ? "o.LegacyTint=instance.Tint;\n" : "o.LegacyTint=g_Object[14];\n";
    vs += R"(
    o.Position=ForgeProject(o.World);o.Color=v.Color;
    ForgeSurfaceFrame frame=ForgeMakeSurfaceFrame(basis,v.Normal,v.Tangent);
    o.Normal=frame.Normal;o.Tangent=frame.Tangent;o.Bitangent=frame.Bitangent;
)"
          "[unroll]for(uint i=0;i<" +
          std::to_string(uv_count) + ";i++)o.UV[i]=v.UV[i];return o;}\n";
    std::string geometry;
    if (fetch.skin && fetch.triangles)
        geometry = std::string("#include \"ForgeSurface.fxh\"\n") + skin + varyings + R"(
float ForgeMagnitude(float3 a) {return max(abs(a.x),max(abs(a.y),abs(a.z)));}
[maxvertexcount(3)]
void main(triangle ForgeVarying input[3],inout TriangleStream<ForgeVarying> output) {
    float sourceScale=max(ForgeMagnitude(input[0].Source),max(ForgeMagnitude(input[1].Source),ForgeMagnitude(input[2].Source)));
    float worldScale=max(ForgeMagnitude(input[0].World),max(ForgeMagnitude(input[1].World),ForgeMagnitude(input[2].World)));
    if(!(sourceScale>0) || !(worldScale>0))return;
    float3 p0=input[0].Source/sourceScale,p1=input[1].Source/sourceScale,p2=input[2].Source/sourceScale;
    float3 q0=input[0].World/worldScale,q1=input[1].World/worldScale,q2=input[2].World/worldScale;
    float3 n=ForgeUnit(cross(ForgeUnit(p1-p0),ForgeUnit(p2-p0)));
    float3 area=cross(ForgeUnit(q1-q0),ForgeUnit(q2-q0));
    if(!any(n!=0) || !any(area!=0))return;
    float3 extension=0;
    [unroll]for(uint i=0;i<3;i++)
        extension+=mul(float3x3(input[i].SkinRows[0],input[i].SkinRows[1],input[i].SkinRows[2]),n)/3;
    // Piecewise affine triangle map extended in its source-normal direction.
    // For uniform A this is det(A)*|n|², including shear and reflection.
    float orientation=dot(area,extension);
    float error=32*1.192092896e-7*dot(abs(area),abs(extension));
    bool swap=orientation < -error;
    if(abs(orientation)<=error) {
        // A surviving rank-two surface has no volume side. Emit its camera-facing
        // winding instead of disabling culling for every skinned triangle.
        float3 a=input[0].Position.xyw,b=input[1].Position.xyw,c=input[2].Position.xyw;
        float largest=max(ForgeMagnitude(a),max(ForgeMagnitude(b),ForgeMagnitude(c)));
        if(!(largest>0))return;
        float projected=dot(a/largest,cross(b/largest,c/largest));
        // FORGE's admitted +Z-front source triangle has negative signed
        // projected area. The ordinary PSO treats this as its front side.
        swap=(projected>0)!=(g_SkinInfo.y!=0);
    }
    // Literal vertex indices keep every SV_Position assignment visible to FXC.
    output.Append(input[0]);
    if(swap) {output.Append(input[2]);output.Append(input[1]);}
    else {output.Append(input[1]);output.Append(input[2]);}
    output.RestartStrip();
}
)";
    return {std::move(vs), std::move(geometry), std::move(varyings), instanced};
}
MeshDrawShader mesh_draw_shader(const MeshVertexFetch& fetch, const PbrMaterialProfile& profile,
                                bool shadow_pass, MaterialSamplerBinding sampler_binding) {
    const auto& source = profile.values;
    const bool transmission = !shadow_pass && material_transmits(profile);
    const bool instanced = !fetch.skin && !fetch.morph_count && !transmission;
    if (transmission) {
        const double ior = source.parameters.at("ior").value[0];
        const double spread = (ior - 1) * (.025 * source.parameters.at("dispersion").value[0]);
        require(ior + spread <= std::numeric_limits<float>::max(),
                "dispersed IOR exceeds the finite GPU optical profile");
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
                    role == "anisotropyTexture" || role == "transmissionTexture" ||
                    role == "thicknessTexture",
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
    const auto material = material_shader(profile, fetch.uv_sets, sampler_binding);
    const auto generated = mesh_geometry_shader(fetch, !transmission);
    const auto& vs = generated.vertex;
    const auto& geometry = generated.geometry;
    const auto& varyings = generated.varyings;
    const auto& skin =
        fetch.skin ? std::string("cbuffer ForgeSkin {float4 g_SkinInfo;float4 g_SkinRows[768];};\n")
                   : std::string{};
    if (shadow_pass) {
        std::string depth = varyings + material.source + "void main(ForgeVarying input) {\n";
        if (source.alpha == MaterialAlpha::Mask) {
            depth += "float alpha=ForgeParameter_baseColorFactor().a*input.Color.a;\n";
            for (const auto& slot : material.textures)
                if (slot.role == "baseColorTexture" || slot.role == "diffuseTexture")
                    depth += "bool valid;alpha*=ForgeSample_" + slot.role + "(input.UV[" +
                             std::to_string(slot.uv_slot) + "],valid).a;if(!valid)discard;\n";
            depth += "if(alpha<ForgeAlphaCutoff())discard;\n";
        }
        depth += "}\n";
        return {vs, depth, material, false, false, geometry, instanced};
    }
    std::string ps =
        "#define USE_IBL 1\n#define USE_HDR_IBL_CUBEMAPS 1\n#define TEX_COLOR_CONVERSION_MODE 0\n";
    ps += std::string("#define ENABLE_CLEAR_COAT ") +
          (profile.workflow == PbrWorkflow::MetallicRoughness ? "1\n" : "0\n");
    ps += std::string("#define ENABLE_IRIDESCENCE ") +
          (profile.workflow == PbrWorkflow::MetallicRoughness ? "1\n" : "0\n");
    ps += std::string("#define ENABLE_SHEEN ") + (sheen ? "1\n" : "0\n");
    ps += std::string("#define ENABLE_ANISOTROPY ") + (anisotropy ? "1\n" : "0\n");
    ps += std::string("#define ENABLE_TRANSMISSION ") + (transmission ? "1\n" : "0\n");
    ps += "#include \"ForgeSurface.fxh\"\n#include \"ForgeLighting.fxh\"\n";
    if (profile.workflow == PbrWorkflow::MetallicRoughness)
        ps += "#include \"Iridescence.fxh\"\n";
    ps += object_source + skin + varyings + material.source;
    if (sheen)
        ps += "Texture2D g_ForgeSheen;\n";
    if (profile.workflow != PbrWorkflow::Unlit)
        ps +=
            "#include \"ForgeShadows.fxh\"\nTexture2D g_ForgeGGX;SamplerState g_ForgeLightSampler;"
            "TextureCube g_ForgeDiffuse,g_ForgeSpecular,g_ForgeCharlie;"
            "cbuffer ForgeEnvironment {float4 g_Environment;};\n";
    if (transmission)
        ps += "#include \"ForgeTransmission.fxh\"\n";
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
        ps += R"(
    if(input.LegacyTint.w!=0) {
        float3 n=ForgeUnit(input.Normal);
        if(!any(n!=0)) n=ForgeUnit(cross(dpdx,dpdy));
        base.rgb*=input.LegacyTint.rgb*(0.3+0.7*saturate(dot(n,normalize(float3(-0.4,0.8,-0.5)))));
    }
    return all(isfinite(base))?base:float4(1,0,1,1);}
)";
    else {
        ps += R"(
    SurfaceShadingInfo s=(SurfaceShadingInfo)0;
    s.Pos=input.World;s.View=g_Object[13].z!=0?-g_Object[8].xyz:ForgeUnit(-input.World);
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
        ps += "s.Occlusion=1;s.IBLScale=g_Environment.x;\n"
              "s.Emissive=ForgeParameter_emissiveFactor()*ForgeParameter_emissiveStrength();\n";
        sample("occlusionTexture",
               "s.Occlusion=lerp(1,sample_occlusionTexture.r,ForgeParameter_occlusionStrength())");
        sample("emissiveTexture", "s.Emissive*=sample_emissiveTexture.rgb");
        if (transmission) {
            ps += "s.Transmission=ForgeParameter_transmissionFactor();\n";
            sample("transmissionTexture", "s.Transmission*=sample_transmissionTexture.r");
        }
        ps += R"(
    SurfaceLightingInfo lighting=GetDefaultSurfaceLightingInfo();
    [loop]for(uint i=0;i<(uint)g_Object[13].x;i++) {
        PBRLightAttribs light=g_Lights[i];
        float visibility=ForgeShadowVisibility(light.ShadowMapIndex,input.World,
                                               geometric*face,dot(g_Object[8].xyz,input.World));
        light.IntensityR*=visibility;light.IntensityG*=visibility;light.IntensityB*=visibility;
        valid=ForgeApplyPunctualLight(s,light,
#if ENABLE_SHEEN
        g_ForgeSheen,g_ForgeLightSampler,
#endif
        lighting)&&valid;
    }
    if(g_Environment.x>0) {
        ApplyIBL(s,g_Environment.y,g_Environment.zw,
                 g_ForgeGGX,g_ForgeLightSampler,g_ForgeDiffuse,g_ForgeLightSampler,
                 g_ForgeSpecular,g_ForgeLightSampler,
#if ENABLE_SHEEN
                 g_ForgeSheen,g_ForgeLightSampler,g_ForgeCharlie,g_ForgeLightSampler,
#endif
                 lighting);
    }
    float3 color=ResolveLighting(s,lighting);
)";
        if (transmission) {
            ps += "float volumeThickness=ForgeParameter_thicknessFactor();\n";
            sample("thicknessTexture", "volumeThickness*=sample_thicknessTexture.g");
            const std::string attenuation = source.parameters.contains("attenuationDistance")
                                                ? "ForgeParameter_attenuationDistance()"
                                                : "0";
            if (fetch.skin)
                ps +=
                    "float3x3 "
                    "volumeBasis=float3x3(input.SkinRows[0],input.SkinRows[1],input.SkinRows[2]);\n"
                    "float volumeDet=dot(volumeBasis[0],cross(volumeBasis[1],volumeBasis[2]));\n"
                    "float "
                    "volumeError=32*1.192092896e-7*dot(abs(volumeBasis[0]),abs(volumeBasis[1].yzx*"
                    "volumeBasis[2].zxy)+abs(volumeBasis[1].zxy*volumeBasis[2].yzx));\n"
                    "bool volumeSingular=abs(volumeDet)<=volumeError;float "
                    "volumeScale=g_SkinInfo.x;\n";
            else
                ps += "float3x3 "
                      "volumeBasis=float3x3(g_Object[3].xyz,g_Object[4].xyz,g_Object[5].xyz);\n"
                      "bool volumeSingular=g_Object[13].w!=0;float volumeScale=g_Object[13].y;\n";
            ps += "float3 transported=ForgeTransport(input.World,s.View,n,geometric*face,"
                  "input.Position.xy,volumeThickness,ForgeParameter_ior(),ForgeParameter_"
                  "dispersion(),"
                  "s.BaseLayer.Srf.PerceptualRoughness," +
                  attenuation +
                  ","
                  "ForgeParameter_attenuationColor(),front,volumeBasis,volumeScale,volumeSingular);"
                  "\n";
            ps += R"(
    IBLSamplingInfo reflection=GetIBLSamplingInfo(s.BaseLayer.Srf,g_ForgeGGX,
        g_ForgeLightSampler,n,s.View);
    float3 reflected=GetSpecularIBL_GGX(s.BaseLayer.Srf,reflection,float3(1,1,1));
    transported*=base.rgb*(1-s.BaseLayer.Metallic)*s.Transmission*saturate(1-reflected);
#if ENABLE_SHEEN
    transported*=1-max(s.Sheen.Color.r,max(s.Sheen.Color.g,s.Sheen.Color.b))*
        SamplePreintegratedSheenBRDF(g_ForgeSheen,g_ForgeLightSampler,s.BaseLayer.NdotV,
                                    s.Sheen.Roughness).g;
#endif
    // Resolve the added base transport through the same native clearcoat layer.
    // It is light transmitted through the surface, never authored emission.
    float ccV=max(dot(s.Clearcoat.Normal,s.View),.1);
    transported*=1-s.Clearcoat.Factor*SchlickReflection(ccV,
        s.Clearcoat.Srf.Reflectance0.x,s.Clearcoat.Srf.Reflectance90.x);
    color+=transported;
)";
        }
        ps += R"(
    if(!valid||!all(isfinite(color)))return float4(1,0,1,1);
    return float4(color,base.a);
})";
    }

    return {vs, ps, material, sheen, transmission, geometry, instanced};
}
} // namespace forge
