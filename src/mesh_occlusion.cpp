#include "mesh_occlusion.hpp"
#include "Graphics/GraphicsEngine/interface/BufferView.h"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "mesh_occlusion_shaders.hpp"
#include <algorithm>
#include <stdexcept>
namespace forge {
using namespace Diligent;
namespace {
struct alignas(16) Parameters {
    std::array<std::uint32_t, 4> input{}, output{}, viewport{}, queries{};
};
static_assert(sizeof(Parameters) == 64);
} // namespace
bool MeshOcclusion::supported() const {
    const auto& commands = device_->GetAdapterInfo().DrawCommand;
    return device_->GetDeviceInfo().Features.ComputeShaders == DEVICE_FEATURE_STATE_ENABLED &&
           (commands.CapFlags & DRAW_COMMAND_CAP_FLAG_DRAW_INDIRECT) !=
               DRAW_COMMAND_CAP_FLAG_NONE &&
           commands.MaxDrawIndirectCount >= 1;
}
Diligent::ITexture* MeshOcclusion::prepare_depth(unsigned width, unsigned height) {
    active_ = false;
    if (!supported() || !mesh_occlusion_image_plan(
                            width, height, device_->GetAdapterInfo().Texture.MaxTexture2DDimension))
        return nullptr;
    if (depth_ && depth_->GetDesc().Width == width && depth_->GetDesc().Height == height)
        return depth_;
    TextureDesc desc;
    desc.Name = "FORGE current-view opaque occluder depth";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = width;
    desc.Height = height;
    desc.Format = TEX_FORMAT_D32_FLOAT;
    desc.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> candidate;
    device_->CreateTexture(desc, nullptr, &candidate);
    if (!candidate || !candidate->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL) ||
        !candidate->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE))
        throw std::runtime_error("Occluder depth allocation unavailable");
    depth_ = std::move(candidate);
    return depth_;
}
void MeshOcclusion::prepare() {
    if (values_)
        return;
    std::array<RefCntAutoPtr<IPipelineState>, 3> pipelines;
    std::array<RefCntAutoPtr<IShaderResourceBinding>, 3> bindings;
    const char* entries[]{"seed", "reduce", "classify"};
    const std::array<const char*, 3> resources[]{{"SourceDepth", "PyramidWrite", nullptr},
                                                 {"PyramidWrite", nullptr, nullptr},
                                                 {"PyramidRead", "QueryBounds", "DrawArguments"}};
    for (unsigned i = 0; i < 3; ++i) {
        ShaderCreateInfo shader;
        shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
        shader.EntryPoint = entries[i];
        shader.Desc.Name = entries[i];
        shader.Desc.ShaderType = SHADER_TYPE_COMPUTE;
        shader.Source = mesh_occlusion_compute;
        RefCntAutoPtr<IShader> native;
        presentation_.shader(shader, &native);
        ComputePipelineStateCreateInfo info;
        info.PSODesc.Name = entries[i];
        info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
        std::array<ShaderResourceVariableDesc, 3> variables{};
        unsigned count = 0;
        for (const auto* name : resources[i])
            if (name)
                variables[count++] = {SHADER_TYPE_COMPUTE, name,
                                      SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC};
        info.PSODesc.ResourceLayout.Variables = variables.data();
        info.PSODesc.ResourceLayout.NumVariables = count;
        info.pCS = native;
        presentation_.compute(info, &pipelines[i]);
        if (!pipelines[i])
            throw std::runtime_error("Occlusion compute pipeline unavailable");
        pipelines[i]->CreateShaderResourceBinding(&bindings[i], true);
        if (!bindings[i] || !bindings[i]->GetVariableByName(SHADER_TYPE_COMPUTE, "OcclusionValues"))
            throw std::runtime_error("Occlusion compute parameters unavailable");
        for (const auto* name : resources[i])
            if (name && !bindings[i]->GetVariableByName(SHADER_TYPE_COMPUTE, name))
                throw std::runtime_error("Occlusion compute resources unavailable");
    }
    BufferDesc desc;
    desc.Name = "FORGE occlusion parameters";
    desc.Size = sizeof(Parameters);
    desc.Usage = USAGE_DYNAMIC;
    desc.BindFlags = BIND_UNIFORM_BUFFER;
    desc.CPUAccessFlags = CPU_ACCESS_WRITE;
    RefCntAutoPtr<IBuffer> values;
    device_->CreateBuffer(desc, nullptr, &values);
    if (!values)
        throw std::runtime_error("Occlusion parameter allocation failed");
    for (auto& binding : bindings)
        binding->GetVariableByName(SHADER_TYPE_COMPUTE, "OcclusionValues")->Set(values);
    pipelines_ = std::move(pipelines);
    bindings_ = std::move(bindings);
    values_ = std::move(values);
}
bool MeshOcclusion::build(IDeviceContext* context, ITextureView* source,
                          const PixelViewport& viewport,
                          std::span<const MeshOcclusionQuery> queries,
                          std::span<const MeshOcclusionCommand> commands) {
    active_ = false;
    if (!context || !source || !supported() || queries.empty() ||
        queries.size() != commands.size() || queries.size() > mesh_occlusion_query_limit)
        return false;
    const auto& desc = source->GetTexture()->GetDesc();
    const auto limit = device_->GetAdapterInfo().Texture.MaxTexture2DDimension;
    const auto capacity = mesh_occlusion_image_plan(desc.Width, desc.Height, limit);
    const auto plan = mesh_occlusion_image_plan(viewport.width, viewport.height, limit);
    if (!capacity || !plan || desc.Type != RESOURCE_DIM_TEX_2D || desc.SampleCount != 1 ||
        source->GetDesc().ViewType != TEXTURE_VIEW_SHADER_RESOURCE ||
        source->GetDesc().Format != TEX_FORMAT_R32_FLOAT ||
        source->GetDesc().MostDetailedMip != 0 ||
        (desc.Format != TEX_FORMAT_R32_FLOAT && desc.Format != TEX_FORMAT_D32_FLOAT &&
         desc.Format != TEX_FORMAT_R32_TYPELESS) ||
        std::uint64_t(viewport.x) + viewport.width > desc.Width ||
        std::uint64_t(viewport.y) + viewport.height > desc.Height)
        return false;
    prepare();
    const auto count = unsigned(queries.size());
    const auto create = [&](RefCntAutoPtr<IBuffer> previous, const char* name, Uint64 bytes,
                            Uint32 stride, BIND_FLAGS flags, bool dynamic = false) {
        if (previous && previous->GetDesc().Size >= bytes)
            return previous;
        BufferDesc data;
        data.Name = name;
        data.Size = bytes;
        data.Mode = BUFFER_MODE_STRUCTURED;
        data.ElementByteStride = stride;
        data.BindFlags = flags;
        if (dynamic) {
            data.Usage = USAGE_DYNAMIC;
            data.CPUAccessFlags = CPU_ACCESS_WRITE;
        }
        RefCntAutoPtr<IBuffer> candidate;
        device_->CreateBuffer(data, nullptr, &candidate);
        if (!candidate)
            throw std::runtime_error("Occlusion scratch allocation failed");
        return candidate;
    };
    const auto next_hierarchy =
        create(hierarchy_, "FORGE far-depth hierarchy", capacity->pyramid_bytes, 4,
               BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS);
    const auto next_queries =
        create(queries_, "FORGE occlusion rectangles",
               std::uint64_t(std::bit_ceil(count)) * sizeof(MeshOcclusionQuery),
               sizeof(MeshOcclusionQuery), BIND_SHADER_RESOURCE, true);
    const auto next_arguments =
        create(arguments_, "FORGE occlusion indirect commands",
               std::uint64_t(std::bit_ceil(count)) * sizeof(MeshOcclusionCommand), 4,
               BIND_UNORDERED_ACCESS | BIND_INDIRECT_DRAW_ARGS);
    if (!next_hierarchy->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE) ||
        !next_hierarchy->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS) ||
        !next_queries->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE) ||
        !next_arguments->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS))
        throw std::runtime_error("Occlusion scratch views unavailable");
    hierarchy_ = next_hierarchy;
    queries_ = next_queries;
    arguments_ = next_arguments;
    try {
        {
            MapHelper<MeshOcclusionQuery> mapped(context, queries_, MAP_WRITE, MAP_FLAG_DISCARD);
            if (!static_cast<MeshOcclusionQuery*>(mapped))
                throw std::runtime_error("Occlusion query upload unavailable");
            std::copy(queries.begin(), queries.end(), static_cast<MeshOcclusionQuery*>(mapped));
        }
        context->UpdateBuffer(arguments_, 0, commands.size_bytes(), commands.data(),
                              RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        bindings_[0]->GetVariableByName(SHADER_TYPE_COMPUTE, "SourceDepth")->Set(source);
        for (unsigned i = 0; i < 2; ++i)
            bindings_[i]
                ->GetVariableByName(SHADER_TYPE_COMPUTE, "PyramidWrite")
                ->Set(hierarchy_->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));
        bindings_[2]
            ->GetVariableByName(SHADER_TYPE_COMPUTE, "PyramidRead")
            ->Set(hierarchy_->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
        bindings_[2]
            ->GetVariableByName(SHADER_TYPE_COMPUTE, "QueryBounds")
            ->Set(queries_->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
        bindings_[2]
            ->GetVariableByName(SHADER_TYPE_COMPUTE, "DrawArguments")
            ->Set(arguments_->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));
        Parameters parameters;
        parameters.viewport = {viewport.x, viewport.y, viewport.width, viewport.height};
        parameters.queries[0] = count;
        const auto upload = [&] {
            MapHelper<Parameters> mapped(context, values_, MAP_WRITE, MAP_FLAG_DISCARD);
            if (!static_cast<Parameters*>(mapped))
                throw std::runtime_error("Occlusion parameter upload unavailable");
            mapped[0] = parameters;
        };
        context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
        unsigned width = plan->pyramid_width, height = plan->pyramid_height;
        unsigned offset = 0, parent = 0, parent_width = 0, parent_height = 0;
        for (unsigned level = 0; level < plan->levels; ++level) {
            if (level) {
                StateTransitionDesc barrier(hierarchy_, RESOURCE_STATE_UNORDERED_ACCESS,
                                            RESOURCE_STATE_UNORDERED_ACCESS);
                context->TransitionResourceStates(1, &barrier);
            }
            parameters.input = {parent_width, parent_height, parent, 0};
            parameters.output = {width, height, offset, plan->levels};
            upload();
            const unsigned stage = level == 0 ? 0 : 1;
            context->SetPipelineState(pipelines_[stage]);
            context->CommitShaderResources(bindings_[stage],
                                           RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            DispatchComputeAttribs dispatch;
            dispatch.ThreadGroupCountX = (width + 7) / 8;
            dispatch.ThreadGroupCountY = (height + 7) / 8;
            dispatch.MtlThreadGroupSizeX = dispatch.MtlThreadGroupSizeY = 8;
            dispatch.MtlThreadGroupSizeZ = 1;
            context->DispatchCompute(dispatch);
            parent = offset;
            parent_width = width;
            parent_height = height;
            offset += width * height;
            width = std::max(width / 2, 1u);
            height = std::max(height / 2, 1u);
        }
        parameters.output = {plan->pyramid_width, plan->pyramid_height, 0, plan->levels};
        upload();
        context->SetPipelineState(pipelines_[2]);
        context->CommitShaderResources(bindings_[2], RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        DispatchComputeAttribs dispatch;
        dispatch.ThreadGroupCountX = (count + 63) / 64;
        dispatch.MtlThreadGroupSizeX = 64;
        dispatch.MtlThreadGroupSizeY = dispatch.MtlThreadGroupSizeZ = 1;
        context->DispatchCompute(dispatch);
        StateTransitionDesc indirect(arguments_, RESOURCE_STATE_UNORDERED_ACCESS,
                                     RESOURCE_STATE_INDIRECT_ARGUMENT,
                                     STATE_TRANSITION_FLAG_UPDATE_STATE);
        context->TransitionResourceStates(1, &indirect);
        bindings_[0]->GetVariableByName(SHADER_TYPE_COMPUTE, "SourceDepth")->Set(nullptr);
        plan_ = *plan;
        active_ = true;
        return true;
    } catch (...) {
        bindings_[0]->GetVariableByName(SHADER_TYPE_COMPUTE, "SourceDepth")->Set(nullptr);
        throw;
    }
}
} // namespace forge
