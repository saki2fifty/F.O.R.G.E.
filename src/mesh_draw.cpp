#include "mesh_draw.hpp"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"
#include "mesh_draw_shader.hpp"
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
// Float4 rows match HLSL cbuffer packing; native PBR's integer lanes use bit_cast.
using Row = std::array<float, 4>;

} // namespace
MeshDraw::MeshDraw(DiligentPresentation& presentation, IDeviceContext* context,
                   const GpuMeshPart& mesh, const MaterialData& source, const Textures& textures,
                   TEXTURE_FORMAT color_format, TEXTURE_FORMAT depth_format)
    : mesh_(mesh), shadow_pass_(color_format == TEX_FORMAT_UNKNOWN) {
    const auto profile = prepare_pbr_material(source);
    const auto fetch = mesh_vertex_fetch(mesh, profile);
    require(!fetch.skin, "deformed draw requires an admitted pose binding");
    const auto program = mesh_draw_shader(fetch, profile, shadow_pass_);
    const auto& material = program.material;
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
    auto vertex = compile(SHADER_TYPE_VERTEX, program.vertex),
         pixel = compile(SHADER_TYPE_PIXEL, program.pixel);
    auto* device = presentation.device();
    const bool lit = !shadow_pass_ && profile.workflow != PbrWorkflow::Unlit;
    RefCntAutoPtr<ITextureView> ggx;
    if (lit) {
        ggx = presentation.pbr(context).GetPreintegratedGGX_SRV();
        black_environment_ = presentation.black_environment(context);
        environment_ = buffer(device, "FORGE environment lighting", sizeof(Row));
        shadows_ = buffer(device, "FORGE shadow receiver", sizeof(ShadowLighting::values));
        empty_shadow_ = presentation.empty_shadow(context);
    }
    if (program.transmission) {
        volume_thickness_ = profile.values.parameters.at("thicknessFactor").value[0];
        transmission_ = buffer(device, "FORGE transmission background", 2 * sizeof(Row));
        black_background_ = presentation.pbr(context).GetBlackTexSRV();
        require(bool(black_background_), "native black transmission fallback unavailable");
    }
    RefCntAutoPtr<ITextureView> sheen;
    if (program.sheen) {
        sheen = presentation.pbr(context).GetPreintegratedSheen_SRV();
        require(bool(sheen), "native sheen lookup resource unavailable");
    }
    RefCntAutoPtr<IBuffer> morph_offsets;
    if (fetch.morph_count) {
        require(mesh.morph_defaults.size() == fetch.morph_count,
                "morph defaults differ from the target count");
        morph_offsets =
            buffer(device, "FORGE immutable morph channel offsets",
                   fetch.morph_offsets.size() * sizeof(Row), fetch.morph_offsets.data());
        morph_weights_ = buffer(device, "FORGE copied morph weights",
                                ((fetch.morph_count + 3) / 4) * sizeof(Row));
    }
    object_ = buffer(device, "FORGE camera-relative object", 15 * sizeof(Row));
    lights_ = buffer(device, "FORGE punctual light list", mesh_draw_light_limit * 4 * sizeof(Row));
    auto values = buffer(device, "FORGE material values", material.uniforms.size() * sizeof(Row),
                         material.uniforms.data());
    GraphicsPipelineStateCreateInfo ci;
    ci.PSODesc.Name = "FORGE prepared mesh draw";
    ci.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    // Environment revisions change on a live SRB. Mutable means set-once in
    // Diligent; dynamic bindings are copied safely for each committed draw.
    std::vector<ShaderResourceVariableDesc> environment_variables{
        {SHADER_TYPE_PIXEL, "g_ForgeDiffuse", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {SHADER_TYPE_PIXEL, "g_ForgeSpecular", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {SHADER_TYPE_PIXEL, "g_ForgeShadows", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        {SHADER_TYPE_PIXEL, "g_ForgeCharlie", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}};
    if (lit) {
        if (!program.sheen)
            environment_variables.pop_back();
        if (program.transmission)
            environment_variables.push_back(
                {SHADER_TYPE_PIXEL, "g_ForgeTransmission", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC});
        ci.PSODesc.ResourceLayout.Variables = environment_variables.data();
        ci.PSODesc.ResourceLayout.NumVariables = Uint32(environment_variables.size());
    }

    ci.pVS = vertex;
    ci.pPS = pixel;
    auto& g = ci.GraphicsPipeline;
    g.NumRenderTargets = shadow_pass_ ? 0 : 1;
    g.RTVFormats[0] = color_format;
    g.DSVFormat = depth_format;
    g.PrimitiveTopology = mesh.topology;
    g.DepthStencilDesc.DepthEnable = shadow_pass_ || source.depth_test;
    g.DepthStencilDesc.DepthWriteEnable = shadow_pass_ || source.depth_write;
    if (!shadow_pass_ && source.alpha == MaterialAlpha::Blend) {
        auto& b = g.BlendDesc.RenderTargets[0];
        b.BlendEnable = true;
        b.SrcBlend = BLEND_FACTOR_SRC_ALPHA;
        b.DestBlend = BLEND_FACTOR_INV_SRC_ALPHA;
        b.SrcBlendAlpha = BLEND_FACTOR_ONE;
        b.DestBlendAlpha = BLEND_FACTOR_INV_SRC_ALPHA;
    }
    for (unsigned parity = 0; parity < 3; ++parity) {
        g.RasterizerDesc.CullMode = source.double_sided || parity == 2 || volume_thickness_ > 0
                                        ? CULL_MODE_NONE
                                        : CULL_MODE_BACK;
        g.RasterizerDesc.FrontCounterClockwise = parity == 1;
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
        if (fetch.morph_count) {
            bind(SHADER_TYPE_VERTEX, "g_ForgeMorphDeltas",
                 mesh.morphs->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
            bind(SHADER_TYPE_VERTEX, "ForgeMorphOffsets", morph_offsets);
            bind(SHADER_TYPE_VERTEX, "ForgeMorphWeights", morph_weights_);
        }
        bind(SHADER_TYPE_PIXEL, "ForgeObject", object_, false);
        bind(SHADER_TYPE_PIXEL, "ForgeLights", lights_, false);
        bind(SHADER_TYPE_PIXEL, "ForgeMaterialValues", values, !shadow_pass_);
        if (lit) {
            bind(SHADER_TYPE_PIXEL, "ForgeEnvironment", environment_);
            bind(SHADER_TYPE_PIXEL, "ForgeShadows", shadows_);
            SamplerDesc comparison;
            comparison.MinFilter = comparison.MagFilter = FILTER_TYPE_COMPARISON_LINEAR;
            comparison.MipFilter = FILTER_TYPE_COMPARISON_POINT;
            comparison.ComparisonFunc = COMPARISON_FUNC_LESS_EQUAL;
            comparison.AddressU = comparison.AddressV = comparison.AddressW =
                TEXTURE_ADDRESS_BORDER;
            std::fill(std::begin(comparison.BorderColor), std::end(comparison.BorderColor), 1.f);
            RefCntAutoPtr<ISampler> shadow_sampler;
            device->CreateSampler(comparison, &shadow_sampler);
            require(bool(shadow_sampler), "shadow comparison sampler allocation failed");
            bind(SHADER_TYPE_PIXEL, "g_ForgeShadowSampler", shadow_sampler);
            SamplerDesc sampler;
            sampler.MinFilter = sampler.MagFilter = sampler.MipFilter = FILTER_TYPE_LINEAR;
            sampler.AddressU = sampler.AddressV = sampler.AddressW = TEXTURE_ADDRESS_CLAMP;
            RefCntAutoPtr<ISampler> native;
            device->CreateSampler(sampler, &native);
            require(bool(native), "environment sampler allocation failed");
            bind(SHADER_TYPE_PIXEL, "g_ForgeGGX", ggx);
            bind(SHADER_TYPE_PIXEL, "g_ForgeLightSampler", native);
            for (const auto* name : {"g_ForgeDiffuse", "g_ForgeSpecular", "g_ForgeCharlie"}) {
                bind(SHADER_TYPE_PIXEL, name, black_environment_, false);
            }
        }
        if (program.transmission) {
            bind(SHADER_TYPE_PIXEL, "ForgeTransmission", transmission_);
            bind(SHADER_TYPE_PIXEL, "g_ForgeTransmission", black_background_);
        }
        if (program.sheen) {
            bind(SHADER_TYPE_PIXEL, "g_ForgeSheen", sheen);
        }
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
void MeshDraw::bind_environment(const GpuEnvironment* maps) {
    if (!environment_)
        return;
    if (maps)
        require(maps->diffuse && maps->specular && maps->sheen, "incomplete environment candidate");
    auto view = [&](Diligent::ITexture* texture) {
        return texture ? texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE)
                       : black_environment_.RawPtr();
    };
    for (auto& binding : bindings_)
        for (const auto& [name, value] :
             {std::pair{"g_ForgeDiffuse", view(maps ? maps->diffuse.RawPtr() : nullptr)},
              std::pair{"g_ForgeSpecular", view(maps ? maps->specular.RawPtr() : nullptr)},
              std::pair{"g_ForgeCharlie", view(maps ? maps->sheen.RawPtr() : nullptr)}})
            if (auto* variable = binding->GetVariableByName(SHADER_TYPE_PIXEL, name))
                variable->Set(value);
}
void MeshDraw::bind_shadows(const ShadowLighting* shadows) {
    if (!shadows_)
        return;
    std::array<IDeviceObject*, shadow_light_limit> maps;
    for (unsigned i = 0; i < shadow_light_limit; ++i)
        maps[i] = shadows && shadows->maps[i] ? shadows->maps[i].RawPtr() : empty_shadow_.RawPtr();
    for (auto& binding : bindings_) {
        auto* variable = binding->GetVariableByName(SHADER_TYPE_PIXEL, "g_ForgeShadows");
        require(variable, "shadow resource array unavailable");
        variable->SetArray(maps.data(), 0, shadow_light_limit);
    }
}
void MeshDraw::bind_transmission(const TransmissionLighting* lighting) {
    if (!transmission_)
        return;
    auto* view = lighting ? lighting->background : black_background_.RawPtr();
    require(view, "transmission background unavailable");
    for (auto& binding : bindings_)
        binding->GetVariableByName(SHADER_TYPE_PIXEL, "g_ForgeTransmission")->Set(view);
}
void MeshDraw::draw(IDeviceContext* context, const AffineTransform& world, const CameraView& view,
                    std::span<const LightView> lights, const EnvironmentLighting* environment,
                    const std::array<float, 3>* legacy_tint, const ShadowLighting* shadows,
                    std::span<const int> shadow_slots, const TransmissionLighting* transmission,
                    std::span<const float> morph_weights) {
    require(context && lights.size() <= mesh_draw_light_limit,
            "invalid context or light list exceeds draw profile");
    require(shadow_slots.empty() || shadow_slots.size() == lights.size(),
            "shadow selections differ from the selected light list");
    if (morph_weights_) {
        if (morph_weights.empty())
            morph_weights = mesh_.morph_defaults;
        require(morph_weights.size() == mesh_.morph_targets.size() &&
                    std::all_of(morph_weights.begin(), morph_weights.end(),
                                [](float value) { return std::isfinite(value); }),
                "morph weights must be finite and match every target");
        MapHelper<Row> data(context, morph_weights_, MAP_WRITE, MAP_FLAG_DISCARD);
        std::fill_n(static_cast<Row*>(data), (morph_weights.size() + 3) / 4, Row{});
        for (std::size_t i = 0; i < morph_weights.size(); ++i)
            data[i / 4][i % 4] = morph_weights[i];
    } else {
        require(morph_weights.empty(), "morph weights supplied to a mesh without targets");
    }
    std::array<Row, 15> object{};
    if (legacy_tint) {
        for (unsigned i = 0; i < 3; ++i) {
            require(std::isfinite((*legacy_tint)[i]) && (*legacy_tint)[i] >= 0 &&
                        (*legacy_tint)[i] <= 1,
                    "invalid legacy blockout color");
            object[14][i] = (*legacy_tint)[i];
        }
        object[14][3] = 1;
    }
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
    if (transmission_ && volume_thickness_ > 0 &&
        transform_parity(world) != TransformParity::Singular) {
        // Frobenius norm bounds ||A^T N|| for every unit surface normal. This
        // is GPU admission of derived optical distance, never a LocalScale edit.
        double norm = 0;
        for (unsigned r = 0; r < 3; ++r)
            for (unsigned c = 0; c < 3; ++c)
                norm = std::hypot(norm, world.m[r * 4 + c]);
        require(double(volume_thickness_) * norm <= std::numeric_limits<float>::max(),
                "world-space volume thickness exceeds the finite GPU optical profile");
    }
    object[13][0] = float(lights.size());
    object[13][1] = gpu(largest);
    // Projection W is constant for orthographic views. Surface view direction
    // must then be parallel for every pixel, not aimed at the camera position.
    object[13][2] = view.projection[14] == 0 ? 1.f : 0.f;
    object[13][3] = transform_parity(world) == TransformParity::Singular ? 1.f : 0.f;
    std::array<Row, mesh_draw_light_limit * 4> packed{};
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
        const int slot = shadows && !shadow_slots.empty() ? shadow_slots[i] : -1;
        require(slot >= -1 && slot < int(shadow_light_limit), "shadow light index out of range");
        rows[1][3] = std::bit_cast<float>(std::int32_t(slot));
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
    if (environment_) {
        Row params{0, 0, 1, 0};
        ITextureView *diffuse = black_environment_, *specular = black_environment_,
                     *charlie = black_environment_;
        if (environment) {
            require(environment->maps && std::isfinite(environment->intensity) &&
                        environment->intensity >= 0 && std::isfinite(environment->rotation),
                    "invalid environment lighting parameters");
            const auto& maps = *environment->maps;
            require(maps.diffuse && maps.specular && maps.sheen,
                    "incomplete environment candidate");
            diffuse = maps.diffuse->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
            specular = maps.specular->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
            charlie = maps.sheen->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
            params = {environment->intensity, float(maps.specular->GetDesc().MipLevels - 1),
                      float(std::cos(environment->rotation)),
                      float(std::sin(environment->rotation))};
        }
        MapHelper<Row> values(context, environment_, MAP_WRITE, MAP_FLAG_DISCARD);
        values[0] = params;
        for (const auto& [name, value] :
             {std::pair{"g_ForgeDiffuse", diffuse}, std::pair{"g_ForgeSpecular", specular},
              std::pair{"g_ForgeCharlie", charlie}})
            if (auto* variable = bindings_[index]->GetVariableByName(SHADER_TYPE_PIXEL, name))
                variable->Set(value);
    }
    if (shadows_) {
        {
            MapHelper<Row> data(context, shadows_, MAP_WRITE, MAP_FLAG_DISCARD);
            if (shadows)
                std::copy(shadows->values.begin(), shadows->values.end(), static_cast<Row*>(data));
            else
                std::fill_n(static_cast<Row*>(data),
                            shadow_light_limit * ShadowLighting::rows_per_light, Row{});
        }
        bind_shadows(shadows);
    }
    if (transmission_) {
        require(transmission && transmission->background && transmission->viewport == view.viewport,
                "transmissive draw requires its camera's opaque background snapshot");
        const auto& v = transmission->viewport;
        const auto& desc = transmission->background->GetTexture()->GetDesc();
        require(v.width && v.height && desc.Width == v.width && desc.Height == v.height &&
                    desc.Format == TEX_FORMAT_RGBA16_FLOAT,
                "transmission background dimensions/format differ from its camera");
        {
            MapHelper<Row> data(context, transmission_, MAP_WRITE, MAP_FLAG_DISCARD);
            data[0] = {float(v.x), float(v.y), 1.f / v.width, 1.f / v.height};
            data[1] = {float(desc.MipLevels - 1), 0, 0, 0};
        }
        bind_transmission(transmission);
    }
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
