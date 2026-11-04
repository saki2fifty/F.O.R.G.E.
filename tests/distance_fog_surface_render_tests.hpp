#pragma once
#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"
#include "fog_volume_geometry.hpp"
#include "mesh_draw_shader.hpp"
#include "sprite_draw_render_tests.hpp"
#include <forge/material_graph.hpp>
#include <forge/shader_asset.hpp>
#include <forge/surface_shader.hpp>
#include <iostream>

namespace forge::test_fog_surface {
template <class Readback>
void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
              Readback&& readback, bool sprite = false, bool local_volume = false) {
    using namespace Diligent;
    using test_sprite_draw::check;
    auto* device = presentation.device();
    const auto compile = [&](SHADER_TYPE stage, const std::string& source, const char* entry) {
        ShaderCreateInfo ci;
        ci.Desc.Name = "FORGE distance fog generated surface fixture";
        ci.Desc.ShaderType = stage;
        ci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
        ci.Source = source.c_str();
        ci.EntryPoint = entry;
        ci.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();
        RefCntAutoPtr<IShader> result;
        presentation.shader(ci, &result);
        check(bool(result), "Fog surface compile failed");
        return result;
    };
    const auto insert = [](std::string& source, const std::string& include,
                           const std::string& replacement) {
        const auto at = source.find(include);
        check(at != std::string::npos, "Generated surface fixture lost its engine include");
        source.replace(at, include.size(), replacement);
    };
    MeshVertexFetch interface;
    interface.uv_sets = {0};
    interface.temporal = !sprite;
    // Use the complete engine varying layout. Sparse test outputs do not share
    // the native backend's locations with the actual surface input structure.
    const auto vertex =
        compile(SHADER_TYPE_VERTEX,
                mesh_shader_varyings(interface, sprite) +
                    "ForgeVarying main(uint id:SV_VertexID){ForgeVarying o=(ForgeVarying)0;"
                    "o.Position=float4(id==2?3:-1,id==1?3:-1,0,1);" +
                    (sprite ? "" : "o.PreviousClip=o.Position+float4(-.125,.0625,.4,0);") +
                    "o.World=float3(0,0,2.5);return o;}",
                "main");
    RefCntAutoPtr<ITexture> color;
    TextureDesc desc;
    desc.Name = "FORGE generated surface fog pixels";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = desc.Height = 16;
    desc.Format = TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    device->CreateTexture(desc, nullptr, &color);
    check(bool(color), "Fog surface target failed");
    RefCntAutoPtr<ITexture> motion;
    desc.Format = TEX_FORMAT_RGBA32_FLOAT;
    device->CreateTexture(desc, nullptr, &motion);
    check(bool(motion), "Temporal surface motion target failed");
    const auto buffer = [&](const std::vector<float>& values) {
        BufferDesc desc;
        desc.Name = "FORGE fog surface fixture constants";
        desc.Size = values.size() * sizeof(float);
        desc.Usage = USAGE_IMMUTABLE;
        desc.BindFlags = BIND_UNIFORM_BUFFER;
        BufferData data;
        data.pData = values.data();
        data.DataSize = desc.Size;
        RefCntAutoPtr<IBuffer> result;
        device->CreateBuffer(desc, &data, &result);
        check(bool(result), "Fog surface constants failed");
        return result;
    };
    FogVolume medium;
    medium.color_r = .1f;
    medium.color_g = .3f;
    medium.color_b = .6f;
    medium.density = std::log(2.f) / 2;
    const std::array volume_samples{prepare_fog_volume(medium, {}, {})};
    const auto packed = fog_volume_uniform(local_volume ? std::span(volume_samples)
                                                        : std::span<const FogVolumeSample>{});
    std::vector<float> volume_values;
    for (const auto& row : packed)
        volume_values.insert(volume_values.end(), row.begin(), row.end());
    const auto volumes = buffer(volume_values);
    RefCntAutoPtr<ITexture> indirect_target;
    desc.Format = TEX_FORMAT_RGBA16_FLOAT;
    device->CreateTexture(desc, nullptr, &indirect_target);
    check(bool(indirect_target), "Surface indirect target failed");
    std::array<RefCntAutoPtr<ITexture>, 3> reflection_targets;
    for (auto& target : reflection_targets) {
        device->CreateTexture(desc, nullptr, &target);
        check(bool(target), "Surface reflection target failed");
    }
    SurfaceShaderDefinition definition;
    definition.uv_sets = {0};
    if (sprite) {
        definition.sprite_image = "image";
        definition.textures["image"].semantic = TextureSemantic::Color;
    }
    struct Case {
        const char* expression;
        float alpha_mode, enabled;
        std::array<float, 4> expected;
        bool diagnostic = false;
    };
    for (const auto& item :
         std::array{Case{"float4(.8,.2,.1,.25)", 2, 1, {.45f, .3f, .4f, .25f}},
                    Case{"float4(.8,.2,.1,.25)", 0, 1, {.45f, .3f, .4f, 1}},
                    Case{"float4(.8,.2,.1,.25)", 1, 1, {0, 0, 0, 0}},
                    Case{"float4(.8,.2,.1,.25)", 2, 0, {.8f, .2f, .1f, .25f}},
                    Case{"float4(asfloat(0x7fc00000),0,0,.25)", 2, 1, {1, 0, 1, 1}, true},
                    Case{"float4(1,0,1,1)", 2, 1, {.55f, .2f, .85f, 1}}})
        for (const bool variant : {false, true})
            for (unsigned output = 0; output < 4; ++output) {
                if (sprite && output != 0)
                    continue;
                const bool temporal = variant && !sprite;
                const bool frame_mips = variant && sprite;
                const bool indirect = output != 0, reflection = output == 2, normal = output == 3;
                auto source =
                    sprite ? sprite_surface_shader_wrapper("function.hlsli", "surface", frame_mips)
                           : surface_shader_wrapper("function.hlsli", "surface", false, indirect,
                                                    reflection, normal);
                if (indirect)
                    source =
                        indirect_surface_wrapper(std::move(source), temporal, reflection, normal);
                else if (temporal)
                    source = temporal_surface_wrapper(std::move(source));
                if (temporal) {
                    insert(source, "#include \"engine/forge.surface.temporal.hlsli\"",
                           surface_shader_header(definition, false, true));
                }
                const auto include = frame_mips
                                         ? "#include \"engine/forge.surface.sprite-mips.hlsli\""
                                         : "#include \"engine/forge.surface.hlsli\"";
                insert(source, include,
                       sprite ? sprite_surface_shader_header(definition, frame_mips)
                              : surface_shader_header(definition));
                insert(source, "#include \"function.hlsli\"",
                       std::string(sprite ? "float4 surface(ForgeSpriteInput input){return "
                                          : "float4 surface(ForgeSurfaceInput input){return ") +
                           item.expression + ";}");
                const auto pixel = compile(
                    SHADER_TYPE_PIXEL, source,
                    surface_shader_entry(
                        sprite ? (frame_mips ? ShaderEntryRole::SurfaceSpriteMipColor
                                             : ShaderEntryRole::SurfaceSpriteColor)
                               : surface_color_role(temporal, indirect, reflection, normal)));
                const auto fog = buffer({.1f, .4f, .7f, item.enabled, 1, 4, 0, 0});
                const auto state = buffer({item.alpha_mode, .5f, 0, 0});
                std::vector<float> temporal_values(13 * 4);
                temporal_values[40] = .01f;
                temporal_values[41] = -.015f;
                temporal_values[42] = -.02f;
                temporal_values[43] = .025f;
                temporal_values[50] = temporal_values[51] = 16;
                const auto previous = buffer(temporal_values);
                GraphicsPipelineStateCreateInfo ci;
                ci.PSODesc.Name = "FORGE generated surface fog test";
                ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
                ci.PSODesc.ResourceLayout.DefaultVariableType =
                    SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
                ci.pVS = vertex;
                ci.pPS = pixel;
                ci.GraphicsPipeline.NumRenderTargets =
                    Diligent::Uint8(1 + unsigned(temporal) + unsigned(indirect) +
                                    (reflection ? 3
                                     : normal   ? 1
                                                : 0));
                ci.GraphicsPipeline.RTVFormats[0] = TEX_FORMAT_RGBA8_UNORM;
                if (temporal)
                    ci.GraphicsPipeline.RTVFormats[1] = TEX_FORMAT_RGBA32_FLOAT;
                if (indirect)
                    ci.GraphicsPipeline.RTVFormats[temporal ? 2 : 1] = TEX_FORMAT_RGBA16_FLOAT;
                if (reflection || normal)
                    for (unsigned i = 0; i < (reflection ? 3u : 1u); ++i)
                        ci.GraphicsPipeline.RTVFormats[2 + unsigned(temporal) + i] =
                            TEX_FORMAT_RGBA16_FLOAT;
                ci.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                ci.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;
                ci.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
                RefCntAutoPtr<IPipelineState> pipeline;
                presentation.graphics(ci, &pipeline);
                RefCntAutoPtr<IShaderResourceBinding> binding;
                pipeline->CreateShaderResourceBinding(&binding, true);
                // A constant invalid user function can optimize fog/state bindings out.
                if (auto* var = binding->GetVariableByName(SHADER_TYPE_PIXEL, "ForgeDistanceFog"))
                    var->Set(fog);
                if (auto* var = binding->GetVariableByName(SHADER_TYPE_PIXEL, "ForgeFogVolumes"))
                    var->Set(volumes);
                if (auto* var = binding->GetVariableByName(SHADER_TYPE_PIXEL, "ForgeSurfaceState"))
                    var->Set(state);
                if (temporal) {
                    auto* variable =
                        binding->GetVariableByName(SHADER_TYPE_PIXEL, "ForgeTemporalObject");
                    check(variable != nullptr, "Temporal surface lost its engine motion binding");
                    variable->Set(previous);
                }
                presentation.begin_frame();
                auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
                std::vector<ITextureView*> targets{rtv};
                if (temporal)
                    targets.push_back(motion->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
                if (indirect)
                    targets.push_back(indirect_target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
                if (reflection || normal)
                    for (unsigned i = 0; i < (reflection ? 3u : 1u); ++i)
                        targets.push_back(
                            reflection_targets[i]->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
                context->SetRenderTargets(Uint32(targets.size()), targets.data(), nullptr,
                                          RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                const float clear[4]{};
                context->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                for (std::size_t i = 1; i < targets.size(); ++i)
                    context->ClearRenderTarget(targets[i], clear,
                                               RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                const Diligent::Viewport viewport{0, 0, 16, 16, 0, 1};
                context->SetViewports(1, &viewport, 16, 16);
                context->SetPipelineState(pipeline);
                context->CommitShaderResources(binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                DrawAttribs draw;
                draw.NumVertices = 3;
                draw.Flags = DRAW_FLAG_VERIFY_ALL;
                context->Draw(draw);
                presentation.end_frame(context);
                const auto pixels = readback(color);
                if (indirect) {
                    const auto contribution = readback(indirect_target);
                    check(std::all_of(
                              contribution.begin(), contribution.end(),
                              [](const auto& p) { return p == std::array<unsigned char, 4>{}; }),
                          "Custom final-color surface reported indirect radiance");
                }
                if (reflection || normal)
                    for (unsigned i = 0; i < (reflection ? 3u : 1u); ++i) {
                        const auto data = readback(reflection_targets[i]);
                        check(std::all_of(data.begin(), data.end(),
                                          [](const auto& p) {
                                              return p == std::array<unsigned char, 4>{};
                                          }),
                              "Custom final-color surface fabricated reflection material data");
                    }
                context->FinishFrame();
                device->ReleaseStaleResources();
                auto expected_color = item.expected;
                if (local_volume && !item.diagnostic && expected_color[3] != 0)
                    for (unsigned lane = 0; lane < 3; ++lane)
                        expected_color[lane] =
                            (expected_color[lane] +
                             std::array{medium.color_r, medium.color_g, medium.color_b}[lane]) *
                            .5f;
                for (const auto& value : pixels)
                    for (unsigned lane = 0; lane < 4; ++lane)
                        check(std::abs(int(value[lane]) -
                                       int(std::lround(expected_color[lane] * 255))) <= 1,
                              "Custom surface fog, alpha or diagnostic pixels differ");
                if (temporal) {
                    const auto value =
                        test_sprite_draw::read_float4_pixel(device, context, motion, 8, 8);
                    const auto expected = item.alpha_mode == 1
                                              ? std::array<float, 4>{}
                                              : std::array<float, 4>{.095f, -.0225f, .4f, 1};
                    for (unsigned lane = 0; lane < 4; ++lane)
                        if (!(std::abs(value[lane] - expected[lane]) < .00001f)) {
                            std::cerr << "Surface case=" << item.expression
                                      << " alpha=" << item.alpha_mode << " observed=";
                            for (auto v : value)
                                std::cerr << v << ',';
                            std::cerr << " expected=";
                            for (auto v : expected)
                                std::cerr << v << ',';
                            std::cerr << '\n';
                            throw std::runtime_error("Surface color/motion wrapper lost jitter, "
                                                     "previous clip or alpha discard");
                        }
                }
            }
    if (sprite) {
        std::cout << "Generated Sprite raw/mip wrapper fog/alpha/diagnostic pixels PASS; "
                     "source-only fixture, not a cooked Shader backend\n";
        return;
    }
    // Compile actual shared PBR graph color/depth wrappers. This is native
    // generated-source proof; it does not load D3D12 Shader cooks on Vulkan.
    const auto graph = compile_material_graph(create_material_graph());
    for (const bool depth : {false, true}) {
        auto source = material_graph_shader_wrapper(graph.surface, "graph.hlsli", depth);
        insert(source, "#include \"engine/forge.surface.hlsli\"",
               surface_shader_header(graph.surface));
        insert(source, "#include \"graph.hlsli\"", graph.source);
        (void)compile(SHADER_TYPE_PIXEL, source, depth ? "ForgeSurfaceDepth" : "ForgeSurfaceColor");
    }
    auto temporal_graph = temporal_surface_wrapper(
        material_graph_shader_wrapper(graph.surface, "graph.hlsli", false));
    insert(temporal_graph, "#include \"engine/forge.surface.temporal.hlsli\"",
           surface_shader_header(graph.surface, false, true));
    insert(temporal_graph, "#include \"engine/forge.surface.hlsli\"",
           surface_shader_header(graph.surface));
    insert(temporal_graph, "#include \"graph.hlsli\"", graph.source);
    (void)compile(SHADER_TYPE_PIXEL, temporal_graph, "ForgeSurfaceTemporalColor");
    for (bool temporal : {false, true})
        for (unsigned output = 0; output < 3; ++output) {
            const bool reflection = output == 1, normal = output == 2;
            auto source = indirect_surface_wrapper(
                material_graph_shader_wrapper(graph.surface, "graph.hlsli", false, true, reflection,
                                              normal),
                temporal, reflection, normal);
            if (temporal)
                insert(source, "#include \"engine/forge.surface.temporal.hlsli\"",
                       surface_shader_header(graph.surface, false, true));
            insert(source, "#include \"engine/forge.surface.hlsli\"",
                   surface_shader_header(graph.surface));
            insert(source, "#include \"graph.hlsli\"", graph.source);
            (void)compile(
                SHADER_TYPE_PIXEL, source,
                surface_shader_entry(surface_color_role(temporal, true, reflection, normal)));
        }
    std::cout << "Generated custom surface fog/alpha/diagnostic/indirect/reflection pixels and "
                 "nine graph "
                 "roles PASS\n";
}
} // namespace forge::test_fog_surface
