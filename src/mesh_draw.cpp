#include "mesh_draw.hpp"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"
#include "texture_gpu.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace forge {
namespace {
using namespace Diligent;
void require(bool ok, const std::string& message) {
    if (!ok)
        throw std::runtime_error("Mesh draw: " + message);
}
float gpu(double x) {
    require(std::isfinite(x) && std::abs(x) <= std::numeric_limits<float>::max(),
            "value is outside GPU finite representation");
    return float(x);
}
RefCntAutoPtr<IBuffer> buffer(IRenderDevice* device, const char* name, Uint64 bytes,
                              const void* initial = nullptr) {
    BufferDesc desc;
    desc.Name = name;
    desc.Size = bytes;
    desc.BindFlags = BIND_UNIFORM_BUFFER;
    desc.Usage = initial ? USAGE_IMMUTABLE : USAGE_DYNAMIC;
    desc.CPUAccessFlags = initial ? CPU_ACCESS_NONE : CPU_ACCESS_WRITE;
    BufferData data{initial, bytes};
    RefCntAutoPtr<IBuffer> result;
    device->CreateBuffer(desc, initial ? &data : nullptr, &result);
    require(bool(result), "uniform buffer allocation failed");
    return result;
}
constexpr unsigned max_lights = 64;
// Float4 rows match HLSL cbuffer packing; native PBR's integer lanes use bit_cast.
using Row = std::array<float, 4>;
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
MeshDraw::MeshDraw(DiligentPresentation& presentation, const GpuMeshPart& mesh,
                   const MaterialData& source, const Textures& textures,
                   TEXTURE_FORMAT color_format, TEXTURE_FORMAT depth_format)
    : mesh_(mesh) {
    const auto profile = prepare_pbr_material(source);
    const auto fetch = mesh_vertex_fetch(mesh, profile);
    require(!fetch.skin && mesh.morph_targets.empty(),
            "deformed draw requires an admitted pose binding");
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
    ps += "cbuffer ForgeLights {PBRLightAttribs g_Lights[" + std::to_string(max_lights) + "];};\n";
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
    auto compile = [&](SHADER_TYPE stage, const std::string& code) {
        ShaderCreateInfo ci;
        ci.Desc.Name = "FORGE prepared mesh draw";
        ci.Desc.ShaderType = stage;
        ci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
        ci.ShaderCompiler = SHADER_COMPILER_FXC;
        ci.HLSLVersion = {5, 1};
        ci.EntryPoint = "main";
        ci.Source = code.c_str();
        ci.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();
        RefCntAutoPtr<IShader> result;
        presentation.shader(ci, &result);
        return result;
    };
    auto vertex = compile(SHADER_TYPE_VERTEX, vs), pixel = compile(SHADER_TYPE_PIXEL, ps);
    auto* device = presentation.device();
    object_ = buffer(device, "FORGE camera-relative object", 14 * sizeof(Row));
    lights_ = buffer(device, "FORGE punctual light list", max_lights * 4 * sizeof(Row));
    auto values = buffer(device, "FORGE material values", material.uniforms.size() * sizeof(Row),
                         material.uniforms.data());
    GraphicsPipelineStateCreateInfo ci;
    ci.PSODesc.Name = "FORGE prepared mesh draw";
    ci.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    ci.pVS = vertex;
    ci.pPS = pixel;
    auto& g = ci.GraphicsPipeline;
    g.NumRenderTargets = 1;
    g.RTVFormats[0] = color_format;
    g.DSVFormat = depth_format;
    g.PrimitiveTopology = mesh.topology;
    g.DepthStencilDesc.DepthEnable = source.depth_test;
    g.DepthStencilDesc.DepthWriteEnable = source.depth_write;
    if (source.alpha == MaterialAlpha::Blend) {
        auto& b = g.BlendDesc.RenderTargets[0];
        b.BlendEnable = true;
        b.SrcBlend = BLEND_FACTOR_SRC_ALPHA;
        b.DestBlend = BLEND_FACTOR_INV_SRC_ALPHA;
        b.SrcBlendAlpha = BLEND_FACTOR_ONE;
        b.DestBlendAlpha = BLEND_FACTOR_INV_SRC_ALPHA;
    }
    for (unsigned parity = 0; parity < 3; ++parity) {
        g.RasterizerDesc.CullMode =
            source.double_sided || parity == 2 ? CULL_MODE_NONE : CULL_MODE_BACK;
        g.RasterizerDesc.FrontCounterClockwise = parity == 0;
        presentation.graphics(ci, &pipelines_[parity]);
        pipelines_[parity]->CreateShaderResourceBinding(&bindings_[parity], true);
        auto bind = [&](SHADER_TYPE stage, const char* name, IDeviceObject* value,
                        bool required = true) {
            auto* var = bindings_[parity]->GetVariableByName(stage, name);
            require(var || !required, std::string("shader binding missing: ") + name);
            if (var)
                var->Set(value);
        };
        bind(SHADER_TYPE_VERTEX, "g_MeshVertices",
             mesh.vertices->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
        bind(SHADER_TYPE_VERTEX, "ForgeObject", object_);
        bind(SHADER_TYPE_PIXEL, "ForgeObject", object_, false);
        bind(SHADER_TYPE_PIXEL, "ForgeLights", lights_, false);
        bind(SHADER_TYPE_PIXEL, "ForgeMaterialValues", values);
        for (const auto& slot : material.textures) {
            const auto found = textures.find(slot.role);
            require(found != textures.end() && found->second,
                    "texture resource unavailable: " + slot.role);
            const auto& desc = found->second->GetTexture()->GetDesc();
            require(desc.Type == RESOURCE_DIM_TEX_2D, "material texture requires a 2D resource");
            bind(SHADER_TYPE_PIXEL, slot.texture_variable.c_str(), found->second, false);
            auto sampler = upload_sampler(device, slot.settings.sampler);
            bind(SHADER_TYPE_PIXEL, slot.sampler_variable.c_str(), sampler, false);
        }
    }
}
void MeshDraw::draw(IDeviceContext* context, const AffineTransform& world, const CameraView& view,
                    std::span<const LightView> lights) {
    require(context && lights.size() <= max_lights,
            "invalid context or light list exceeds draw profile");
    std::array<Row, 14> object{};
    double largest = 0;
    for (unsigned r = 0; r < 3; ++r)
        for (unsigned c = 0; c < 3; ++c)
            largest = std::max(largest, std::abs(world.m[r * 4 + c]));
    for (unsigned r = 0; r < 3; ++r) {
        for (unsigned c = 0; c < 3; ++c) {
            object[r][c] = gpu(world.m[r * 4 + c]);
            object[3 + r][c] = largest > 0 ? gpu(world.m[r * 4 + c] / largest) : 0;
        }
        object[r][3] = gpu(world.m[r * 4 + 3] - view.position[r]);
        object[6][r] = gpu(view.right[r]);
        object[7][r] = gpu(view.up[r]);
        object[8][r] = gpu(view.forward[r]);
    }
    for (unsigned r = 0; r < 4; ++r)
        for (unsigned c = 0; c < 4; ++c)
            object[9 + r][c] = view.projection[r * 4 + c];
    object[13][0] = float(lights.size());
    std::array<Row, max_lights * 4> packed{};
    for (std::size_t i = 0; i < lights.size(); ++i) {
        const auto& light = lights[i];
        auto* rows = packed.data() + i * 4;
        require(light.kind <= 2, "unknown punctual light kind");
        rows[0][0] = std::bit_cast<float>(light.kind + 1);
        for (unsigned c = 0; c < 3; ++c) {
            rows[0][c + 1] = gpu(light.position[c] - view.position[c]);
            rows[1][c] = gpu(light.direction[c]);
            rows[2][c] = gpu(double(light.color[c]) * light.intensity);
        }
        rows[1][3] = std::bit_cast<float>(std::int32_t(-1));
        const double range = light.range;
        rows[2][3] = gpu(range * range * range * range);
        require(range == 0 || rows[2][3] >= std::numeric_limits<float>::min(),
                "light range underflows native attenuation");
        if (light.kind == std::uint32_t(LightKind::Spot)) {
            rows[3][0] = gpu(1. / (double(light.cosine_inner) - light.cosine_outer));
            rows[3][1] = gpu(-double(light.cosine_outer) * rows[3][0]);
        }
    }
    {
        MapHelper<Row> values(context, object_, MAP_WRITE, MAP_FLAG_DISCARD);
        std::copy(object.begin(), object.end(), static_cast<Row*>(values));
    }
    {
        MapHelper<Row> values(context, lights_, MAP_WRITE, MAP_FLAG_DISCARD);
        std::copy(packed.begin(), packed.end(), static_cast<Row*>(values));
    }
    const auto parity = transform_parity(world);
    const unsigned index =
        parity == TransformParity::Singular
            ? 2
            : unsigned((parity == TransformParity::Negative) != view.orientation_reversed);
    context->SetPipelineState(pipelines_[index]);
    context->CommitShaderResources(bindings_[index], RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->SetIndexBuffer(mesh_.indices, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    DrawIndexedAttribs draw;
    draw.NumIndices = mesh_.index_count;
    draw.IndexType = VT_UINT32;
    draw.Flags = DRAW_FLAG_VERIFY_ALL;
    context->DrawIndexed(draw);
}
} // namespace forge
