#include "viewport.hpp"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include <forge/geometry.hpp>
#include <stdexcept>
using namespace Diligent;
namespace forge {
Viewport::Viewport(IRenderDevice* device) : device_(device) {
    const char* vs = R"(
cbuffer ObjectData { float4 centerAspect; float4 eyeNear; float4 rightFocal; float4 upFar; float4 forwardPad; float4 axisX; float4 axisY; float4 axisZ; float4 tint; float4 normalX; float4 normalY; float4 normalZ; };
struct Out { float4 position : SV_POSITION; float3 color : COLOR0; };
Out main(float3 vertex : ATTRIB0, float3 normal : ATTRIB1) {
    float3 world = axisX.xyz*vertex.x + axisY.xyz*vertex.y + axisZ.xyz*vertex.z + centerAspect.xyz;
    float3 offset = world - eyeNear.xyz;
    float3 p=float3(dot(offset,rightFocal.xyz),dot(offset,upFar.xyz),dot(offset,forwardPad.xyz));
    float depthScale=upFar.w/(upFar.w-eyeNear.w);
    Out o; o.position=float4(p.x*rightFocal.w/centerAspect.w,p.y*rightFocal.w,
                             (p.z-eyeNear.w)*depthScale,p.z);
    float3 n=normalize(normalX.xyz*normal.x + normalY.xyz*normal.y + normalZ.xyz*normal.z);
    float light=0.3+0.7*saturate(dot(n,normalize(float3(-0.4,0.8,-0.5))));
    o.color=tint.rgb*light; return o;
})";
    const char* ps =
        "float4 main(float4 p:SV_POSITION,float3 color:COLOR0):SV_TARGET{return float4(color,1);}";
    ShaderCreateInfo shader;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.EntryPoint = "main";
    RefCntAutoPtr<IShader> vertex, pixel;
    shader.Desc.Name = "FORGE blockout VS";
    shader.Desc.ShaderType = SHADER_TYPE_VERTEX;
    shader.Source = vs;
    device_->CreateShader(shader, &vertex);
    shader.Desc.Name = "FORGE blockout PS";
    shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
    shader.Source = ps;
    device_->CreateShader(shader, &pixel);
    if (!vertex || !pixel)
        throw std::runtime_error("Preview shader compilation failed");
    GraphicsPipelineStateCreateInfo pso;
    pso.PSODesc.Name = "FORGE blockout";
    pso.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
    pso.GraphicsPipeline.NumRenderTargets = 1;
    pso.GraphicsPipeline.RTVFormats[0] = TEX_FORMAT_RGBA8_UNORM;
    pso.GraphicsPipeline.DSVFormat = TEX_FORMAT_D32_FLOAT;
    pso.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pso.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;
    LayoutElement layout[] = {{0, 0, 3, VT_FLOAT32, False}, {1, 0, 3, VT_FLOAT32, False}};
    pso.GraphicsPipeline.InputLayout.LayoutElements = layout;
    pso.GraphicsPipeline.InputLayout.NumElements = 2;
    pso.pVS = vertex;
    pso.pPS = pixel;
    device_->CreateGraphicsPipelineState(pso, &pipeline_);
    if (!pipeline_)
        throw std::runtime_error("Preview pipeline creation failed");
    static_assert(sizeof(PrimitiveVertex) == 6 * sizeof(float));
    std::vector<PrimitiveVertex> vertices;
    for (unsigned i = 0; i < 4; ++i) {
        starts_[i] = static_cast<unsigned>(vertices.size());
        const auto& mesh = primitive_meshes()[i];
        counts_[i] = static_cast<unsigned>(mesh.size());
        vertices.insert(vertices.end(), mesh.begin(), mesh.end());
    }
    BufferDesc vertex_buffer;
    vertex_buffer.Name = "FORGE immutable blockout meshes";
    vertex_buffer.Usage = USAGE_IMMUTABLE;
    vertex_buffer.BindFlags = BIND_VERTEX_BUFFER;
    vertex_buffer.Size = vertices.size() * sizeof(PrimitiveVertex);
    BufferData initial;
    initial.pData = vertices.data();
    initial.DataSize = vertex_buffer.Size;
    device_->CreateBuffer(vertex_buffer, &initial, &vertices_);
    if (!vertices_)
        throw std::runtime_error("Primitive vertex buffer creation failed");
    BufferDesc buffer;
    buffer.Name = "FORGE preview position";
    buffer.Size = 192;
    buffer.Usage = USAGE_DYNAMIC;
    buffer.BindFlags = BIND_UNIFORM_BUFFER;
    buffer.CPUAccessFlags = CPU_ACCESS_WRITE;
    device_->CreateBuffer(buffer, nullptr, &constants_);
    if (!constants_)
        throw std::runtime_error("Preview constant buffer creation failed");
    auto* variable = pipeline_->GetStaticVariableByName(SHADER_TYPE_VERTEX, "ObjectData");
    if (!variable)
        throw std::runtime_error("Preview shader constants missing");
    variable->Set(constants_);
    pipeline_->CreateShaderResourceBinding(&resources_, true);
    shader.Desc.Name = "FORGE grid VS";
    shader.Desc.ShaderType = SHADER_TYPE_VERTEX;
    shader.Source = grid_vertex_shader;
    vertex.Release();
    device_->CreateShader(shader, &vertex);
    shader.Desc.Name = "FORGE grid PS";
    shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
    shader.Source = grid_pixel_shader;
    pixel.Release();
    device_->CreateShader(shader, &pixel);
    if (!vertex || !pixel)
        throw std::runtime_error("Grid shader compilation failed");
    GraphicsPipelineStateCreateInfo grid;
    grid.PSODesc.Name = "FORGE world ground grid";
    grid.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
    grid.GraphicsPipeline.NumRenderTargets = 1;
    grid.GraphicsPipeline.RTVFormats[0] = TEX_FORMAT_RGBA8_UNORM;
    grid.GraphicsPipeline.DSVFormat = TEX_FORMAT_D32_FLOAT;
    grid.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    grid.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;
    grid.GraphicsPipeline.DepthStencilDesc.DepthEnable = True;
    grid.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = False;
    grid.GraphicsPipeline.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;
    auto& blend = grid.GraphicsPipeline.BlendDesc.RenderTargets[0];
    blend.BlendEnable = True;
    blend.SrcBlend = BLEND_FACTOR_SRC_ALPHA;
    blend.DestBlend = BLEND_FACTOR_INV_SRC_ALPHA;
    blend.SrcBlendAlpha = BLEND_FACTOR_ONE;
    blend.DestBlendAlpha = BLEND_FACTOR_INV_SRC_ALPHA;
    grid.pVS = vertex;
    grid.pPS = pixel;
    device_->CreateGraphicsPipelineState(grid, &grid_pipeline_);
    if (!grid_pipeline_)
        throw std::runtime_error("Grid pipeline creation failed");
    buffer.Name = "FORGE grid camera";
    buffer.Size = sizeof(GridConstants);
    device_->CreateBuffer(buffer, nullptr, &grid_constants_);
    if (!grid_constants_)
        throw std::runtime_error("Grid camera buffer creation failed");
    variable = grid_pipeline_->GetStaticVariableByName(SHADER_TYPE_PIXEL, "GridView");
    if (!variable)
        throw std::runtime_error("Grid shader camera binding missing");
    variable->Set(grid_constants_);
    grid_pipeline_->CreateShaderResourceBinding(&grid_resources_, true);
}
ITextureView* Viewport::render(IDeviceContext* context, const Json& scene, unsigned width,
                               unsigned height, const EditorCamera& camera,
                               std::uint64_t generation, bool live, GridSettings grid) {
    const auto key = viewport_frame_key(generation, width, height, camera, grid);
    // Static blockout preview only. Play renders continuously; future time-dependent
    // materials/effects must also opt out of retained EDIT frames.
    if (!live && frame_ && *frame_ == key) {
        ++retained;
        return color_->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    }
    if (!color_ || color_->GetDesc().Width != width || color_->GetDesc().Height != height) {
        color_.Release();
        depth_.Release();
        TextureDesc t;
        t.Name = "FORGE scene viewport";
        t.Type = RESOURCE_DIM_TEX_2D;
        t.Width = width;
        t.Height = height;
        t.Format = TEX_FORMAT_RGBA8_UNORM;
        t.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
        device_->CreateTexture(t, nullptr, &color_);
        t.Name = "FORGE scene depth";
        t.Format = TEX_FORMAT_D32_FLOAT;
        t.BindFlags = BIND_DEPTH_STENCIL;
        device_->CreateTexture(t, nullptr, &depth_);
        if (!color_ || !depth_)
            throw std::runtime_error("Viewport allocation failed");
    }
    auto* rtv = color_->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    auto* dsv = depth_->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
    context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    const float clear[] = {0.025f, 0.04f, 0.055f, 1};
    context->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                               RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->SetPipelineState(pipeline_);
    IBuffer* buffers[] = {vertices_};
    Uint64 offset = 0;
    context->SetVertexBuffers(0, 1, buffers, &offset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                              SET_VERTEX_BUFFERS_FLAG_RESET);
    const auto eye = camera.eye(), right = camera.right(), up = camera.up(),
               forward = camera.forward();
    for (const auto& entity : scene.at("entities")) {
        if (entity.value("prefab", false) || !entity.at("components").contains("forge.position"))
            continue;
        if (!entity.value("spatial_resolved", true))
            continue;
        const ObjectTransform world_transform(entity);
        {
            MapHelper<float> data(context, constants_, MAP_WRITE, MAP_FLAG_DISCARD);
            data[0] = world_transform.position[0];
            data[1] = world_transform.position[1];
            data[2] = world_transform.position[2];
            data[3] = float(width) / float(height);
            for (unsigned i = 0; i < 3; ++i) {
                data[4 + i] = eye[i];
                data[8 + i] = right[i];
                data[12 + i] = up[i];
                data[16 + i] = forward[i];
            }
            data[7] = EditorCamera::near_plane;
            data[11] = EditorCamera::focal;
            data[15] = EditorCamera::far_plane;
            data[19] = 0;
            const ObjectTransform transform(entity);
            for (unsigned axis = 0; axis < 3; ++axis) {
                for (unsigned coordinate = 0; coordinate < 3; ++coordinate)
                    data[20 + axis * 4 + coordinate] =
                        transform.axes[axis][coordinate] * transform.scale[axis];
                data[23 + axis * 4] = 1.0f / (transform.scale[axis] * transform.scale[axis]);
            }
            if (transform.derived) {
                const auto inv = inverse(transform.affine);
                for (unsigned axis = 0; axis < 3; ++axis) {
                    for (unsigned coordinate = 0; coordinate < 3; ++coordinate)
                        data[36 + axis * 4 + coordinate] = float(inv.m[4 * axis + coordinate]);
                    data[39 + axis * 4] = 0;
                }
            } else {
                for (unsigned axis = 0; axis < 3; ++axis) {
                    for (unsigned coordinate = 0; coordinate < 3; ++coordinate)
                        data[36 + axis * 4 + coordinate] =
                            transform.axes[axis][coordinate] / transform.scale[axis];
                    data[39 + axis * 4] = 0;
                }
            }
            const auto& c = entity.at("components");
            const auto tint = c.value("forge.tint", Json{{"r", 0.2f}, {"g", 0.6f}, {"b", 0.7f}});
            data[32] = tint.at("r");
            data[33] = tint.at("g");
            data[34] = tint.at("b");
            data[35] = 1;
        }
        context->CommitShaderResources(resources_, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        DrawAttribs draw;
        const auto kind = primitive_kind(entity);
        draw.NumVertices = counts_.at(kind);
        draw.StartVertexLocation = starts_.at(kind);
        draw.Flags = DRAW_FLAG_VERIFY_ALL;
        context->Draw(draw);
    }
    if (grid.visible) {
        {
            MapHelper<GridConstants> data(context, grid_constants_, MAP_WRITE, MAP_FLAG_DISCARD);
            *data = grid_constants(camera, width, height, grid);
        }
        context->SetPipelineState(grid_pipeline_);
        context->CommitShaderResources(grid_resources_, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        DrawAttribs draw;
        draw.NumVertices = 3;
        draw.Flags = DRAW_FLAG_VERIFY_ALL;
        context->Draw(draw);
    }
    ++redraws;
    frame_ = key;
    return color_->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
}
} // namespace forge
