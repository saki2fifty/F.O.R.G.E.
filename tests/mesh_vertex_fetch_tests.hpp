#pragma once
#include "mesh_vertex_fetch.hpp"
void check_mesh_vertex_fetch(forge::DiligentPresentation& presentation,
                             Diligent::IDeviceContext* context) {
    using namespace Diligent;
    forge::MeshPart part;
    part.vertices = 3;
    part.indices = {2, 0, 1};
    part.streams = {{"POSITION", 3, std::vector<float>{-1, -1, .5f, 3, -1, .5f, -1, 3, .5f}},
                    {"NORMAL", 3, std::vector<float>{0, 0, 1, 0, 0, 1, 0, 0, 1}},
                    {"TANGENT", 4, std::vector<float>{1, 0, 0, -1, 1, 0, 0, -1, 1, 0, 0, -1}},
                    {"COLOR_0", 3, std::vector<float>{1, 1, 1, 1, 1, 1, 1, 1, 1}},
                    {"TEXCOORD_0", 2, std::vector<float>{.9f, .9f, .9f, .9f, .9f, .9f}},
                    {"TEXCOORD_19", 2, std::vector<float>{.2f, .4f, .2f, .4f, .2f, .4f}},
                    {"JOINTS_0", 4, std::vector<std::uint32_t>{0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3}},
                    {"WEIGHTS_0", 4, std::vector<float>(12, .25f)}};
    part.joint_palette = {0, 1, 2, 3};
    for (unsigned i = 0; i < 48; ++i)
        part.streams.push_back(
            {"_CUSTOM_" + std::to_string(i), 16, std::vector<float>(48, float(i))});
    part.bounds = forge::mesh_bounds(part);
    forge::MeshData mesh;
    mesh.lods = {{1, {part}}};
    auto gpu = forge::upload_mesh(presentation.device(), mesh);
    const auto& uploaded = gpu.lods[0].parts[0];
    require(uploaded.stride > 2048, "Wide stream fixture did not exceed ordinary IA stride");
    forge::MaterialData material;
    material.model = "forge.gltf.unlit.v1";
    auto& texture = material.textures["baseColorTexture"];
    texture.semantic = forge::TextureSemantic::Color;
    texture.uv_set = 19;
    auto profile = forge::prepare_pbr_material(material);
    const auto fetch = forge::mesh_vertex_fetch(uploaded, profile);
    require(fetch.uv_sets == std::vector<unsigned>{19} && fetch.normal && fetch.tangent &&
                fetch.color && fetch.skin,
            "Vertex fetch lost selected UV or vertex channels");
    auto shader = [&](SHADER_TYPE stage, const std::string& source) {
        ShaderCreateInfo ci;
        ci.Desc.Name = "FORGE indexed vertex-fetch acceptance";
        ci.Desc.ShaderType = stage;
        ci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
        ci.ShaderCompiler = SHADER_COMPILER_FXC;
        ci.HLSLVersion = {5, 1};
        ci.EntryPoint = "main";
        ci.Source = source.c_str();
        RefCntAutoPtr<IShader> result;
        presentation.shader(ci, &result);
        require(bool(result), "Vertex-fetch shader compilation failed");
        return result;
    };
    const auto vs = shader(SHADER_TYPE_VERTEX, fetch.source + R"(
struct Output {float4 Position:SV_Position; float4 Color:COLOR0;};
Output main(uint id:SV_VertexID) {
    ForgeMeshVertex v=ForgeLoadMeshVertex(id);
    Output o; o.Position=float4(v.Position,1);
    o.Color=float4(v.UV[0],float(v.Joints.z)/4,1);
    if(v.Tangent.w != -1 || v.Weights.x != .25 || v.Color.a != 1 || v.Normal.z != 1)
        o.Color=float4(1,0,1,1);
    return o;
})");
    const auto ps =
        shader(SHADER_TYPE_PIXEL, "float4 main(float4 color:COLOR0):SV_Target0 {return color;}");
    GraphicsPipelineStateCreateInfo pso;
    pso.PSODesc.Name = "FORGE indexed raw vertex draw";
    pso.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    pso.pVS = vs;
    pso.pPS = ps;
    auto& graphics = pso.GraphicsPipeline;
    graphics.NumRenderTargets = 1;
    graphics.RTVFormats[0] = TEX_FORMAT_RGBA8_UNORM;
    graphics.PrimitiveTopology = uploaded.topology;
    graphics.RasterizerDesc.CullMode = CULL_MODE_NONE;
    graphics.DepthStencilDesc.DepthEnable = false;
    RefCntAutoPtr<IPipelineState> pipeline;
    presentation.graphics(pso, &pipeline);
    require(bool(pipeline), "Vertex-fetch pipeline failed");
    RefCntAutoPtr<IShaderResourceBinding> binding;
    pipeline->CreateShaderResourceBinding(&binding, true);
    auto* vertices = binding->GetVariableByName(SHADER_TYPE_VERTEX, "g_MeshVertices");
    require(vertices != nullptr, "Vertex-fetch raw binding absent");
    vertices->Set(uploaded.vertices->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
    TextureDesc desc;
    desc.Name = "FORGE vertex-fetch result";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = desc.Height = 32;
    desc.Format = TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = BIND_RENDER_TARGET;
    RefCntAutoPtr<ITexture> target;
    presentation.device()->CreateTexture(desc, nullptr, &target);
    require(bool(target), "Vertex-fetch target failed");
    auto* rtv = target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    context->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    const float black[4]{0, 0, 0, 1};
    context->ClearRenderTarget(rtv, black, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Viewport viewport{0.f, 0.f, 32.f, 32.f, 0.f, 1.f};
    context->SetViewports(1, &viewport, 32, 32);
    context->SetPipelineState(pipeline);
    context->CommitShaderResources(binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->SetIndexBuffer(uploaded.indices, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    DrawIndexedAttribs draw;
    draw.NumIndices = uploaded.index_count;
    draw.IndexType = VT_UINT32;
    draw.Flags = DRAW_FLAG_VERIFY_ALL;
    context->DrawIndexed(draw);
    const auto pixels = readback(presentation.device(), context, rtv);
    const auto pixel = pixels.at(16 * 32 + 16);
    require(std::abs(int(pixel[0]) - 51) <= 1 && std::abs(int(pixel[1]) - 102) <= 1 &&
                std::abs(int(pixel[2]) - 128) <= 1 && pixel[3] == 255,
            "Indexed raw fetch changed UV19, tangent sign, integer joints or color width");
    material.textures["baseColorTexture"].uv_set = 17;
    bool rejected = false;
    try {
        forge::mesh_vertex_fetch(uploaded, forge::prepare_pbr_material(material));
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "Missing material UV set silently replaced with UV0");
    auto invalid = uploaded;
    invalid.attributes[0].offset = invalid.stride;
    rejected = false;
    try {
        forge::mesh_vertex_fetch(invalid, profile);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && uploaded.vertices->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE),
            "Invalid vertex metadata accepted or previous native resource damaged");
}
