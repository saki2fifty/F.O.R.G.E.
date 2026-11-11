#include "mesh_draw_shader.hpp"
#include "decal_shader.hpp"
#include "distance_fog_shader.hpp"
#include "indirect_lighting_shader.hpp"
#include "sprite_image_shader.hpp"
#include "sprite_material_profile.hpp"
#include "temporal_motion_shader.hpp"
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
constexpr const char* wind_source = R"(
float2 ForgeWindOffset(float height,float4 wind) {
    float h=saturate(height*wind.z);
    return wind.xy*(h*h);
}
void ForgeWindFrame(inout ForgeSurfaceFrame frame,float height,float4 wind) {
    if(!any(wind.xy!=0))return;
    float h=height*wind.z;
    float gradient=h>0 && h<1 ? 2*h*wind.z : 0;
    float handed=dot(cross(frame.Normal,frame.Tangent),frame.Bitangent)<0 ? -1 : 1;
    float3 tangent=frame.Tangent;
    tangent.xz+=wind.xy*(gradient*tangent.y);
    float3 normal=frame.Normal;
    normal.y-=gradient*dot(wind.xy,normal.xz);
    normal=ForgeUnit(normal);
    tangent=ForgeUnit(tangent-normal*dot(normal,tangent));
    frame.Normal=normal;frame.Tangent=tangent;
    frame.Bitangent=handed*ForgeUnit(cross(normal,tangent));
}
)";
std::string shader_varyings(const MeshVertexFetch& fetch, bool sprite_profile,
                            std::string_view name, bool front_face, bool geometry_stage) {
    const auto uv_count = std::max<std::size_t>(1, fetch.uv_sets.size());
    std::string varyings =
        "struct " + std::string(name) + " {float4 Position:SV_Position;float3 World:TEXCOORD0;";
    if (sprite_profile)
        varyings += "nointerpolation float4 SpriteX:TEXCOORD1;"
                    "nointerpolation float4 SpriteY:TEXCOORD2;"
                    "nointerpolation float4 SpriteImage:TEXCOORD3;"
                    "float4 Color:COLOR0;nointerpolation float4 SpriteAtlas:COLOR1;";
    else
        varyings += "float3 Normal:TEXCOORD1;float3 Tangent:TEXCOORD2;float3 Bitangent:TEXCOORD3;"
                    "float4 Color:COLOR0;nointerpolation float4 LegacyTint:COLOR1;";
    if (geometry_stage)
        for (unsigned u = 0; u < uv_count; ++u)
            varyings += "float2 UV" + std::to_string(u) + ":TEXCOORD" + std::to_string(4 + u) + ";";
    else
        varyings += "float2 UV[" + std::to_string(uv_count) + "]:TEXCOORD4;";
    if (sprite_profile)
        varyings += "nointerpolation float4 SpriteClamp:TEXCOORD5;"
                    "nointerpolation float4 SpriteCrop:TEXCOORD6;";
    if (fetch.skin) {
        varyings += "float3 Source:TEXCOORD" + std::to_string(4 + uv_count) + ";";
        if (geometry_stage)
            for (unsigned row = 0; row < 3; ++row)
                varyings += "float3 SkinRow" + std::to_string(row) + ":TEXCOORD" +
                            std::to_string(5 + uv_count + row) + ";";
        else
            varyings += "float3 SkinRows[3]:TEXCOORD" + std::to_string(5 + uv_count) + ";";
    }
    if (fetch.temporal)
        varyings += "float4 PreviousClip:TEXCOORD" + std::to_string(8 + uv_count) + ";";
    if (front_face)
        varyings += "bool FrontFace:SV_IsFrontFace;";
    varyings += "};\n";
    return varyings;
}
} // namespace
std::string mesh_shader_varyings(const MeshVertexFetch& fetch, bool sprite_profile,
                                 std::string_view name, bool front_face) {
    return shader_varyings(fetch, sprite_profile, name, front_face, false);
}
MeshGeometryShader mesh_geometry_shader(const MeshVertexFetch& fetch, bool allow_instances,
                                        bool sprite_profile) {
    const bool instanced = allow_instances && !fetch.skin && !fetch.morph_count;
    require(!fetch.wind || (instanced && !sprite_profile),
            "Wind geometry requires the static Mesh instance profile");
    if (sprite_profile)
        require(!fetch.temporal && instanced && fetch.triangles &&
                    fetch.uv_sets == std::vector<unsigned>{0},
                "Sprite geometry requires a static triangle mesh with UV0");
    const auto uv_count = std::max<std::size_t>(1, fetch.uv_sets.size());
    const std::string skin = fetch.skin ? R"(
// Common positive normalization avoids overflowing weighted linear matrices.
// Translations are independently camera relative before upload.
cbuffer ForgeSkin {float4 g_SkinInfo;float4 g_SkinRows[768];};
)"
                                        : "";
    const auto varyings = mesh_shader_varyings(fetch, sprite_profile);
    std::string vs = std::string("#include \"ForgeSurface.fxh\"\n") + object_source + skin +
                     fetch.source + varyings;
    if (fetch.wind)
        vs += wind_source;
    if (fetch.temporal) {
        vs += temporal_motion_uniform;
        if (fetch.skin)
            vs += "cbuffer ForgePreviousSkin {float4 g_PreviousSkinInfo;"
                  "float4 g_PreviousSkinRows[768];};\n";
    }
    if (instanced) {
        vs += "struct ForgeInstance {float4 R0:ATTRIB0;float4 R1:ATTRIB1;float4 R2:ATTRIB2;"
              "float4 B0:ATTRIB3;float4 B1:ATTRIB4;float4 B2:ATTRIB5;float4 Tint:ATTRIB6;";
        if (sprite_profile)
            vs += "float4 Atlas:ATTRIB7;float4 AtlasClamp:ATTRIB8;float4 Crop:ATTRIB9;";
        if (fetch.temporal)
            vs += "float4 PreviousR0:ATTRIB7;float4 PreviousR1:ATTRIB8;"
                  "float4 PreviousR2:ATTRIB9;float4 PreviousInfo:ATTRIB10;";
        if (fetch.wind) {
            vs += "float4 Wind:ATTRIB" + std::to_string(fetch.temporal ? 11 : 7) + ";";
            if (fetch.temporal)
                vs += "float4 PreviousWind:ATTRIB12;";
        }
        vs += "};\n";
    }
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
              "dot(instance.R1,position4),dot(instance.R2,position4));";
        if (!sprite_profile)
            vs += "basis=float3x3(instance.B0.xyz,instance.B1.xyz,instance.B2.xyz);\n";
    } else
        vs += "o.World=ForgePoint(v.Position);basis=float3x3(g_Object[3].xyz,g_Object[4].xyz,g_"
              "Object[5].xyz);\n";
    if (fetch.wind)
        vs += "float "
              "windHeight=dot(instance.R1.xyz,v.Position);o.World.xz+=ForgeWindOffset(windHeight,"
              "instance.Wind);\n";
    vs += "o.Position=ForgeProject(o.World);o.Color=v.Color;\n";
    if (fetch.temporal) {
        vs += "o.PreviousClip=0;if(g_TemporalObject[11].x!=0";
        if (instanced)
            vs += "&&instance.PreviousInfo.x!=0";
        vs += "){float3 previous=ForgeLoadPreviousPosition(id);\n";
        if (fetch.wind)
            vs += "float previousWindHeight=dot(instance.PreviousR1.xyz,previous);\n";
        if (fetch.skin)
            vs += R"hlsl(
float4 previousRows[3];
[unroll]for(uint r=0;r<3;r++)
    previousRows[r]=weights.x*g_PreviousSkinRows[3*v.Joints.x+r]+
                    weights.y*g_PreviousSkinRows[3*v.Joints.y+r]+
                    weights.z*g_PreviousSkinRows[3*v.Joints.z+r]+
                    weights.w*g_PreviousSkinRows[3*v.Joints.w+r];
previous=mul(float3x3(previousRows[0].xyz,previousRows[1].xyz,previousRows[2].xyz),previous)*
         g_PreviousSkinInfo.x+float3(previousRows[0].w,previousRows[1].w,previousRows[2].w);
)hlsl";
        else if (instanced)
            vs += "float4 p=float4(previous,1);previous=float3(dot(instance.PreviousR0,p),"
                  "dot(instance.PreviousR1,p),dot(instance.PreviousR2,p));\n";
        else
            vs += "float4 p=float4(previous,1);previous=float3(dot(g_TemporalObject[0],p),"
                  "dot(g_TemporalObject[1],p),dot(g_TemporalObject[2],p));\n";
        if (fetch.wind)
            vs += "previous.xz+=ForgeWindOffset(previousWindHeight,instance.PreviousWind);\n";
        vs += "o.PreviousClip=ForgePreviousProject(previous);}\n";
    }
    if (!sprite_profile) {
        vs += instanced ? "o.LegacyTint=instance.Tint;\n" : "o.LegacyTint=g_Object[14];\n";
        if (fetch.wind)
            vs += R"(
    ForgeSurfaceFrame frame=ForgeMakeSurfaceFrame(basis,v.Normal,v.Tangent);
    ForgeWindFrame(frame,windHeight,instance.Wind);
    o.Normal=frame.Normal;o.Tangent=frame.Tangent;o.Bitangent=frame.Bitangent;
 )";
        else
            vs += R"(
    ForgeSurfaceFrame frame=ForgeMakeSurfaceFrame(basis,v.Normal,v.Tangent);
    o.Normal=frame.Normal;o.Tangent=frame.Tangent;o.Bitangent=frame.Bitangent;
 )";
    }
    vs += "[unroll]for(uint i=0;i<" + std::to_string(uv_count) + ";i++)o.UV[i]=v.UV[i];";
    if (sprite_profile)
        vs += "float2 local=float2(v.UV[0].x,1-v.UV[0].y);"
              "if(instance.B2.x==0 && instance.B2.w!=0)local=local.yx;"
              "o.UV[0]=instance.B2.x==0?instance.Atlas.xy+local*instance.Atlas.zw:local;"
              "o.SpriteX=instance.B0;o.SpriteY=instance.B1;o.SpriteImage=instance.B2;"
              "o.SpriteAtlas=instance.Atlas;o.SpriteClamp=instance.AtlasClamp;"
              "o.SpriteCrop=instance.Crop;"
              "o.Color*=instance.Tint;";
    vs += "return o;}\n";
    std::string geometry;
    if (fetch.skin && fetch.triangles)
        // Flatten stage-interface members: nested arrays in geometry IO lose
        // per-vertex UV interpolation on the supported Vulkan path. Semantic
        // registers stay identical to the vertex/pixel array interface.
        geometry = std::string("#include \"ForgeSurface.fxh\"\n") + skin +
                   shader_varyings(fetch, sprite_profile, "ForgeVarying", false, true) + R"(
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
        extension+=mul(float3x3(input[i].SkinRow0,input[i].SkinRow1,input[i].SkinRow2),n)/3;
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
                                bool shadow_pass, MaterialSamplerBinding sampler_binding,
                                const SurfaceShaderDefinition* graph, std::string_view graph_source,
                                bool sprite_profile, std::optional<TextureFilter> sprite_mips,
                                bool indirect_output, bool reflection_output, bool normal_output,
                                bool area_lights, bool projected_decals) {
    const auto& source = profile.values;
    require(!area_lights || (!shadow_pass && !sprite_profile),
            "Area lighting requires the Mesh color pass");
    require(!indirect_output || (!shadow_pass && !sprite_profile),
            "Indirect outputs require the Mesh color pass");
    if (sprite_profile) {
        require(!shadow_pass && !graph && profile.workflow == PbrWorkflow::Unlit,
                "Sprite draw profile requires a built-in unlit color material");
        validate_sprite_material_profile(source);
    }
    const bool transmission = !shadow_pass && material_transmits(profile);
    require(!reflection_output || (indirect_output && !transmission),
            "Reflection outputs require indirect non-transmissive Mesh color");
    require(!normal_output || (indirect_output && !reflection_output && !transmission),
            "Normal-only outputs require non-transmissive indirect Mesh color");
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
    // Graph lighting uses fixed engine PBR defaults, independent of the graph's
    // own adjustable surface parameters. Expose those constants to the optimizer.
    const auto material = material_shader(profile, fetch.uv_sets, sampler_binding, sprite_profile,
                                          sprite_mips, graph != nullptr);
    const auto decal_capacity =
        projected_decals && !shadow_pass && !sprite_profile
            ? decal_shader_capacity(graph ? graph->textures.size() : material.samplers.size(),
                                    profile.workflow != PbrWorkflow::Unlit)
            : 0;
    const auto generated = mesh_geometry_shader(fetch, !transmission, sprite_profile);
    const auto& vs = generated.vertex;
    const auto& geometry = generated.geometry;
    const auto& varyings = generated.varyings;
    const auto& skin =
        fetch.skin ? std::string("cbuffer ForgeSkin {float4 g_SkinInfo;float4 g_SkinRows[768];};\n")
                   : std::string{};
    if (graph) {
        require(graph->physically_based && profile.workflow == PbrWorkflow::MetallicRoughness,
                "Graph lighting requires the shared metallic-roughness model");
        if (shadow_pass)
            return {vs,
                    "#include \"engine/forge.surface.hlsli\"\n#include \"" +
                        std::string(graph_source) +
                        "\"\nvoid ForgeSurfaceDepth(ForgeSurfaceInput input) {\n"
                        "ForgeGraphValues graph=ForgeGraphSurface(input);\n"
                        "if(!all(isfinite(graph.BaseColor)))discard;\n"
                        "if(g_ForgeSurfaceState.x==1 && "
                        "graph.BaseColor.a*input.Color.a<g_ForgeSurfaceState.y)discard;\n}\n",
                    material,
                    false,
                    false,
                    geometry,
                    instanced};
    }
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
    if (area_lights && profile.workflow != PbrWorkflow::Unlit)
        ps += "#include \"ForgeAreaLights.fxh\"\n";
    if (decal_capacity) {
        ps += "#define FORGE_DECAL_CLUSTERED 1\n";
        ps += "#define FORGE_DECAL_CAPACITY " + std::to_string(decal_capacity) + "\n";
        ps += std::string("#define FORGE_DECAL_NAMED_SAMPLERS ") +
              (sampler_binding == MaterialSamplerBinding::NamedElements ? "1\n" : "0\n");
        ps += "#include \"ForgeDecals.fxh\"\n#include \"ForgeLightImages.fxh\"\n";
    }
    if (profile.workflow == PbrWorkflow::MetallicRoughness)
        ps += "#include \"Iridescence.fxh\"\n";
    ps += object_source + skin + varyings + material.source;
    if (graph)
        ps += "#include \"engine/forge.surface.hlsli\"\n#include \"" + std::string(graph_source) +
              "\"\n";
    if (sheen)
        ps += "Texture2D g_ForgeSheen;\n";
    if (profile.workflow != PbrWorkflow::Unlit)
        ps +=
            "#include \"ForgeShadows.fxh\"\nTexture2D g_ForgeGGX;SamplerState g_ForgeLightSampler;"
            "TextureCube g_ForgeDiffuse,g_ForgeSpecular,g_ForgeCharlie;"
            "cbuffer ForgeEnvironment {float4 g_Environment;};\n"
            "#include \"ForgeLocalIrradiance.fxh\"\n";
    if (transmission)
        ps += "#include \"ForgeTransmission.fxh\"\n";
    ps += "cbuffer ForgeLights {PBRLightAttribs g_Lights[" + std::to_string(mesh_draw_light_limit) +
          "];";
    if (area_lights)
        ps += "float4 g_AreaShapes[" + std::to_string(mesh_draw_light_limit * 3) + "];";
    ps += "};\n";
    if (sprite_profile)
        ps += sprite_image_coordinate_function("ForgeVarying", sprite_mips.has_value());
    ps += distance_fog_shader;
    if (reflection_output || normal_output)
        ps += reflection_surface_data;
    if (indirect_output && profile.workflow != PbrWorkflow::Unlit) {
        ps += indirect_lighting_shader;
        if (reflection_output)
            ps += base_reflection_shader;
    }
    ps += R"(
float4 main(ForgeVarying input,bool front:SV_IsFrontFace):SV_Target0 {
    float3 dpdx=ddx(input.World),dpdy=ddy(input.World);
    bool valid=true;
)";
    if (sprite_mips)
        require(sprite_profile, "Sprite mip sampling requires the Sprite draw profile");
    if (sprite_profile)
        ps += "float2 spriteDx,spriteDy;"
              "if(!ForgeSpriteImageCoordinates(input,spriteDx,spriteDy))discard;\n";
    if (graph)
        ps += "ForgeGraphValues graph=ForgeGraphSurface(input);\n"
              "if(!all(isfinite(graph.BaseColor)) || !isfinite(graph.Metallic) || "
              "!isfinite(graph.Roughness) || "
              "!all(isfinite(graph.Normal)) || !all(isfinite(graph.Emissive)) || "
              "!isfinite(graph.Occlusion) || !all(isfinite(graph.LightmapIrradiance)) || "
              "any(graph.LightmapIrradiance<0))return float4(1,0,1,1);\n";
    for (const auto& slot : material.textures) {
        ps += "bool ok_" + slot.role + ";float4 sample_" + slot.role + "=ForgeSample_" + slot.role +
              "(input.UV[" + std::to_string(slot.uv_slot) + "]," +
              (sprite_mips ? "spriteDx,spriteDy," : "") + "ok_" + slot.role + ");valid=valid&&ok_" +
              slot.role + ";\n";
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
    if (graph)
        ps +=
            "base=graph.BaseColor*input.Color;base.rgb=max(base.rgb,0);base.a=saturate(base.a);\n";
    sample("baseColorTexture", "base*=sample_baseColorTexture");
    sample("diffuseTexture", "base*=sample_diffuseTexture");
    if (graph)
        ps += "if(g_ForgeSurfaceState.x==1 && base.a<g_ForgeSurfaceState.y)discard;\n"
              "if(g_ForgeSurfaceState.x!=2)base.a=1;\n";
    else {
        if (source.alpha == MaterialAlpha::Mask)
            ps += "if(base.a<ForgeAlphaCutoff())discard;\n";
        if (source.alpha != MaterialAlpha::Blend)
            ps += "base.a=1;\n";
    }
    if (!sprite_profile)
        ps += "if(input.LegacyTint.w!=0)base.rgb*=input.LegacyTint.rgb;\n";
    if (sprite_profile)
        ps += "return "
              "all(isfinite(base))?ForgeApplyDistanceFog(base,input.World):float4(1,0,1,1);}\n";
    else if (profile.workflow == PbrWorkflow::Unlit)
        ps += R"(
    if(input.LegacyTint.w!=0) {
        float3 n=ForgeUnit(input.Normal);
        if(!any(n!=0)) n=ForgeUnit(cross(dpdx,dpdy));
        base.rgb*=0.3+0.7*saturate(dot(n,normalize(float3(-0.4,0.8,-0.5))));
    }
    return all(isfinite(base))?ForgeApplyDistanceFog(base,input.World):float4(1,0,1,1);}
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
        const auto project_surface = [&] {
            if (decal_capacity)
                ps += "if(!ForgeApplyProjectedSurface(input.World,geometric*face,dpdx,dpdy,"
                      "base.rgb,n,rough))return float4(1,0,1,1);\n";
            ps += "s.BaseLayer.Normal=n;s.BaseLayer.NdotV=saturate(dot(n,s.View));\n";
        };
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
        if (graph)
            ps += "if(any(graph.Normal!=0))n=ForgeUnit(graph.Normal);\n";
        mapped_normal("normalTexture", "normalScale", "n");
        ps += "n*=face;\n";
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
            if (graph)
                ps += "rough=saturate(graph.Roughness);metal=saturate(graph.Metallic);\n";
            project_surface();
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
            ps += "float rough=1-physical.a,originalRoughness=rough;\n";
            project_surface();
            ps += "if(rough!=originalRoughness)physical.a=1-rough;\n";
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
        if (graph)
            ps += "s.Occlusion=saturate(graph.Occlusion);s.Emissive=max(graph.Emissive,0);\n";
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
)";
        if (area_lights)
            ps += R"(
        if(light.Type>3) {
            valid=ForgeApplyAreaLight(s,light,g_AreaShapes[i*3].xyz,
                                     g_AreaShapes[i*3+1].xyz,g_AreaShapes[i*3+2].xyz,
#if ENABLE_SHEEN
                                     g_ForgeSheen,g_ForgeLightSampler,
#endif
                                     lighting)&&valid;
        } else {
)";
        if (decal_capacity)
            ps += R"(
        float3 image;
        valid=ForgeLightImage(i,input.World,dpdx,dpdy,image)&&valid;
        light.IntensityR*=image.r;light.IntensityG*=image.g;light.IntensityB*=image.b;
)";
        ps += R"(
        float visibility=ForgeShadowVisibility(light.ShadowMapIndex,input.World,
                                               geometric*face,dot(g_Object[8].xyz,input.World));
        light.IntensityR*=visibility;light.IntensityG*=visibility;light.IntensityB*=visibility;
        valid=ForgeApplyPunctualLight(s,light,
#if ENABLE_SHEEN
        g_ForgeSheen,g_ForgeLightSampler,
#endif
        lighting)&&valid;
)";
        if (area_lights)
            ps += "}\n";
        ps += R"(
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
    valid=ForgeApplyLocalIrradiance(input.World,g_Environment.x,s,lighting)&&valid;
)";
        if (graph)
            ps += "float3 lightmapDiffuse;valid=ForgeLightmapDiffuse(graph.LightmapIrradiance,s,"
                  "lightmapDiffuse)&&valid;\n";
        ps += "float3 color=ResolveLighting(s,lighting);\n";
        if (graph)
            ps += "color+=lightmapDiffuse;\n";
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
    return ForgeApplyDistanceFog(float4(color,base.a),input.World);
})";
    }

    require(!reflection_output || (indirect_output && !shadow_pass && !sprite_profile),
            "Reflection outputs require indirect Mesh color");
    if (indirect_output) {
        const std::string entry =
            "float4 main(ForgeVarying input,bool front:SV_IsFrontFace):SV_Target0 {";
        const auto at = ps.find(entry);
        require(at != std::string::npos, "Indirect Mesh color lost its shared entry");
        ps.replace(
            at, entry.size(),
            std::string(
                "float4 ForgeIndirectColor(ForgeVarying input,bool front,out float3 indirect") +
                (reflection_output ? ",out ForgeReflectionSurfaceData reflection"
                 : normal_output   ? ",out float4 forgeNormalSurface"
                                   : "") +
                ") {indirect=0;" +
                (reflection_output ? "reflection=(ForgeReflectionSurfaceData)0;"
                 : normal_output   ? "forgeNormalSurface=0;"
                                   : ""));
        if (profile.workflow != PbrWorkflow::Unlit) {
            const std::string resolved = "float3 color=ResolveLighting(s,lighting);";
            const auto light_at = ps.find(resolved);
            require(light_at != std::string::npos,
                    "Indirect Mesh color lost native lighting resolution");
            std::string outputs =
                graph ? "\nfloat3 "
                        "lightmapIndirect=(ForgeIndirectRadiance(s,lighting)+lightmapDiffuse)*"
                        "ForgeDistanceFogTransmittance(input.World);\n"
                        "bool indirectValid=all(isfinite(lightmapIndirect));"
                        "valid=indirectValid&&valid;indirect=indirectValid?lightmapIndirect:0;\n"
                      : "\nindirect=ForgeIndirectRadiance(s,lighting)*"
                        "ForgeDistanceFogTransmittance(input.World);\n";
            if (reflection_output)
                outputs += "reflection=ForgeBaseReflection(s,lighting,float3("
                           "dot(g_Object[6].xyz,s.BaseLayer.Normal),"
                           "dot(g_Object[7].xyz,s.BaseLayer.Normal),"
                           "dot(g_Object[8].xyz,s.BaseLayer.Normal)),"
                           "ForgeDistanceFogTransmittance(input.World));\n";
            if (normal_output)
                outputs += "forgeNormalSurface=ForgeMaterialSurface(float3("
                           "dot(g_Object[6].xyz,s.BaseLayer.Normal),"
                           "dot(g_Object[7].xyz,s.BaseLayer.Normal),"
                           "dot(g_Object[8].xyz,s.BaseLayer.Normal)),"
                           "s.BaseLayer.Srf.PerceptualRoughness);\n";
            ps.insert(light_at + resolved.size(), outputs);
        }
        if (fetch.temporal)
            ps += std::string(temporal_motion_uniform) + temporal_motion_pixel;
        ps += indirect_target_shader(fetch.temporal, reflection_output, normal_output);
        ps += "ForgeIndirectTargets main(ForgeVarying input,bool front:SV_IsFrontFace) {\n"
              "ForgeIndirectTargets result;float3 indirect;\n";
        if (reflection_output)
            ps += "ForgeReflectionSurfaceData reflection;\n";
        else if (normal_output)
            ps += "float4 forgeNormalSurface;\n";
        ps += std::string("result.Color=ForgeIndirectColor(input,front,indirect") +
              (reflection_output ? ",reflection"
               : normal_output   ? ",forgeNormalSurface"
                                 : "") +
              ");\n"
              "result.Indirect=float4(indirect,0);\n";
        if (reflection_output)
            ps += "result.Surface=reflection.Surface;result.Response=float4(reflection.Response,0);"
                  "result.Specular=float4(reflection.Specular,0);\n";
        if (normal_output)
            ps += "result.Surface=forgeNormalSurface;\n";
        if (fetch.temporal)
            ps += "result.Motion=ForgeTemporalMotion(input.PreviousClip,input.Position);\n";
        ps += "return result;}\n";
    } else if (fetch.temporal) {
        require(!shadow_pass && !sprite_profile, "Temporal color requires the Mesh color pass");
        const std::string entry =
            "float4 main(ForgeVarying input,bool front:SV_IsFrontFace):SV_Target0 {";
        const auto at = ps.find(entry);
        require(at != std::string::npos, "Temporal Mesh color lost its shared entry");
        ps.replace(at, entry.size(), "float4 ForgeTemporalColor(ForgeVarying input,bool front) {");
        ps += std::string(temporal_motion_uniform) + temporal_motion_pixel + R"hlsl(
ForgeTemporalTargets main(ForgeVarying input,bool front:SV_IsFrontFace) {
    ForgeTemporalTargets result;
    result.Color=ForgeTemporalColor(input,front);
    result.Motion=ForgeTemporalMotion(input.PreviousClip,input.Position);
    return result;
}
)hlsl";
    }
    return {vs, ps, material, sheen, transmission, geometry, instanced, decal_capacity};
}
std::string material_graph_shader_wrapper(const SurfaceShaderDefinition& definition,
                                          std::string_view source, bool depth_only,
                                          bool indirect_output, bool reflection_output,
                                          bool normal_output, bool area_lights,
                                          bool projected_decals) {
    MaterialData values;
    values.model = "forge.gltf.metallic-roughness.v1";
    const auto profile = prepare_pbr_material(values);
    MeshVertexFetch fetch;
    fetch.uv_sets = definition.uv_sets;
    fetch.normal = fetch.tangent = true;
    auto generated = mesh_draw_shader(
        fetch, profile, depth_only, MaterialSamplerBinding::Array, &definition, source, false, {},
        indirect_output, reflection_output, normal_output, area_lights, projected_decals);
    require(!depth_only || !indirect_output, "Depth graph does not emit indirect color");
    if (indirect_output) {
        const std::string extra = reflection_output ? ",out ForgeReflectionSurfaceData reflection"
                                  : normal_output   ? ",out float4 forgeNormalSurface"
                                                    : "";
        const std::string old =
            "float4 ForgeIndirectColor(ForgeVarying input,bool front,out float3 indirect" + extra +
            ") {";
        const auto at = generated.pixel.find(old);
        require(at != std::string::npos, "Graph wrapper lost shared indirect entry");
        generated.pixel.replace(at, old.size(),
                                "float4 ForgeSurfaceIndirectLighting(ForgeSurfaceInput input,out "
                                "float3 indirect" +
                                    extra + ") {bool front=input.FrontFace;");
        const auto targets = generated.pixel.find("struct ForgeIndirectTargets {");
        require(targets != std::string::npos,
                "Graph wrapper lost Mesh indirect target declaration");
        generated.pixel.resize(targets); // The shared surface wrapper supplies its own entry/MRTs.
    } else if (!depth_only) {
        const std::string old =
            "float4 main(ForgeVarying input,bool front:SV_IsFrontFace):SV_Target0 {";
        const auto at = generated.pixel.find(old);
        require(at != std::string::npos, "Graph wrapper lost shared PBR entry");
        generated.pixel.replace(at, old.size(),
                                "float4 ForgeSurfaceColor(ForgeSurfaceInput input):SV_Target0 { "
                                "bool front=input.FrontFace;");
    }
    return generated.pixel;
}
} // namespace forge
