#include "particle_gpu_order.hpp"
#include "Graphics/GraphicsEngine/interface/BufferView.h"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "mesh_instance_layout.hpp"
#include <bit>
#include <cmath>
#include <forge/particle_effect.hpp>
#include <stdexcept>
#include <string>
namespace forge {
using namespace Diligent;
namespace {
struct alignas(16) OrderValues {
    std::array<unsigned, 4> limits{};
    MeshInstanceRow forward{}, center{};
};
static_assert(sizeof(OrderValues) == 48);
static_assert(mesh_static_instance_rows == 7);
constexpr std::uint64_t packet_bytes = mesh_static_instance_rows * sizeof(MeshInstanceRow);
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
constexpr const char* order_source = R"(
cbuffer SortValues { uint4 Limits;float4 Forward,MeshCenter; };
StructuredBuffer<float4> SourceRows;
RWStructuredBuffer<uint4> SortKeys;
RWStructuredBuffer<float4> SortedRows;
bool Before(uint4 a,uint4 b) {
    if(a.w!=b.w)return a.w>b.w;
    if(a.w!=0) {
        float first=asfloat(a.x),second=asfloat(b.x);
        if(first!=second)return first>second;
    }
    return a.y<b.y;
}
[numthreads(64,1,1)]void main(uint3 thread:SV_DispatchThreadID) {
    uint id=thread.x;if(id>=Limits.y)return;
#if SORT_PASS==0
    if(id>=Limits.x){SortKeys[id]=uint4(0,id,0,0);return;}
    uint row=7*id;float3 center;
    if(SourceRows[row+6].w==4)center=.5*SourceRows[row].xyz+.5*SourceRows[row+1].xyz;
    else {
        float4 position4=float4(MeshCenter.xyz,1);
        center=float3(dot(SourceRows[row],position4),dot(SourceRows[row+1],position4),dot(SourceRows[row+2],position4));
    }
    float depth=dot(center,Forward.xyz);
    SortKeys[id]=uint4(asuint(depth),id,0,isfinite(depth)?1:0);
#elif SORT_PASS==1
    uint other=id^Limits.w;if(other<=id||other>=Limits.y)return;
    uint4 first=SortKeys[id],second=SortKeys[other];bool ascending=(id&Limits.z)==0;
    if(ascending?Before(second,first):Before(first,second)){
        SortKeys[id]=second;SortKeys[other]=first;
    }
#else
    uint4 key=SortKeys[id];
    [unroll]for(uint row=0;row<7;++row)
        SortedRows[7*id+row]=key.w!=0?SourceRows[7*key.y+row]:float4(0,0,0,row==6?4:0);
#endif
}
)";
} // namespace
ParticleGpuOrder::ParticleGpuOrder(DiligentPresentation& presentation)
    : presentation_(presentation) {}
void ParticleGpuOrder::prepare() {
    if (ordered_)
        return;
    require(presentation_.device()->GetDeviceInfo().Features.ComputeShaders ==
                DEVICE_FEATURE_STATE_ENABLED,
            "Ordered GPU particles require compute shader support");
    RefCntAutoPtr<IBuffer> combined, keys, ordered, values;
    BufferDesc desc;
    desc.Name = "FORGE ordered particle input";
    desc.Size = particle_scene_capacity * packet_bytes;
    desc.Mode = BUFFER_MODE_STRUCTURED;
    desc.ElementByteStride = sizeof(MeshInstanceRow);
    desc.Usage = USAGE_DEFAULT;
    desc.BindFlags = BIND_SHADER_RESOURCE;
    presentation_.device()->CreateBuffer(desc, nullptr, &combined);
    desc.Name = "FORGE ordered particle keys";
    desc.Size = particle_scene_capacity * sizeof(MeshInstanceRow);
    desc.BindFlags = BIND_UNORDERED_ACCESS;
    presentation_.device()->CreateBuffer(desc, nullptr, &keys);
    desc.Name = "FORGE ordered particle instances";
    desc.Size = particle_scene_capacity * packet_bytes;
    desc.BindFlags = BIND_UNORDERED_ACCESS | BIND_VERTEX_BUFFER;
    presentation_.device()->CreateBuffer(desc, nullptr, &ordered);
    desc.Name = "FORGE ordered particle constants";
    desc.Size = sizeof(OrderValues);
    desc.Mode = BUFFER_MODE_UNDEFINED;
    desc.ElementByteStride = 0;
    desc.Usage = USAGE_DYNAMIC;
    desc.CPUAccessFlags = CPU_ACCESS_WRITE;
    desc.BindFlags = BIND_UNIFORM_BUFFER;
    presentation_.device()->CreateBuffer(desc, nullptr, &values);
    require(combined && keys && ordered && values, "Ordered GPU particle allocation failed");
    std::array<RefCntAutoPtr<IPipelineState>, 3> pipelines;
    std::array<RefCntAutoPtr<IShaderResourceBinding>, 3> bindings;
    for (unsigned pass = 0; pass < pipelines.size(); ++pass) {
        ShaderCreateInfo shader;
        shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
        shader.EntryPoint = "main";
        shader.Desc.Name = "FORGE ordered particle instances";
        shader.Desc.ShaderType = SHADER_TYPE_COMPUTE;
        shader.Source = order_source;
        const auto selected = std::to_string(pass);
        const ShaderMacro macro{"SORT_PASS", selected.c_str()};
        shader.Macros = {&macro, 1};
        RefCntAutoPtr<IShader> native;
        presentation_.shader(shader, &native);
        require(bool(native), "Ordered GPU particle shader unavailable");
        ComputePipelineStateCreateInfo info;
        info.PSODesc.Name = "FORGE ordered particle instances";
        info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
        info.pCS = native;
        presentation_.compute(info, &pipelines[pass]);
        require(bool(pipelines[pass]), "Ordered GPU particle pipeline unavailable");
        pipelines[pass]->CreateShaderResourceBinding(&bindings[pass], true);
        require(bool(bindings[pass]), "Ordered GPU particle binding unavailable");
        const auto set = [&](const char* name, IDeviceObject* object) {
            auto* variable = bindings[pass]->GetVariableByName(SHADER_TYPE_COMPUTE, name);
            require(variable != nullptr, "Ordered GPU particle resource binding missing");
            variable->Set(object);
        };
        set("SortValues", values);
        set("SortKeys", keys->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));
        if (pass != 1)
            set("SourceRows", combined->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
        if (pass == 2)
            set("SortedRows", ordered->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));
    }
    combined_ = std::move(combined);
    keys_ = std::move(keys);
    values_ = std::move(values);
    pipelines_ = std::move(pipelines);
    bindings_ = std::move(bindings);
    ordered_ = std::move(ordered);
}
void ParticleGpuOrder::evaluate(IDeviceContext* context, std::span<const Source> sources,
                                const CameraView& camera, const std::array<float, 3>& mesh_center) {
    count_ = 0;
    require(context != nullptr && sources.size() <= particle_emitter_limit,
            "Ordered GPU particle submission exceeds the source limit");
    OrderValues input;
    for (unsigned axis = 0; axis < 3; ++axis) {
        input.forward[axis] = float(camera.forward[axis]);
        input.center[axis] = mesh_center[axis];
        require(std::isfinite(input.forward[axis]) && std::isfinite(input.center[axis]),
                "Ordered GPU particle depth inputs must be finite");
    }
    unsigned total = 0;
    for (const auto& source : sources) {
        require(source.count <= particle_scene_capacity - total,
                "Ordered GPU particles exceed the 8192-instance view budget");
        if (source.count) {
            require(source.buffer && source.buffer != combined_ && source.buffer != keys_ &&
                        source.buffer != ordered_,
                    "Ordered GPU particle source buffer is unavailable or aliases its output");
            const auto& desc = source.buffer->GetDesc();
            require(desc.Mode == BUFFER_MODE_STRUCTURED &&
                        desc.ElementByteStride == sizeof(MeshInstanceRow) &&
                        desc.Size >= source.count * packet_bytes,
                    "Ordered GPU particle source range or row layout is invalid");
        }
        total += source.count;
    }
    if (!total)
        return;
    prepare();
    context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    unsigned offset = 0;
    for (const auto& source : sources) {
        if (source.count)
            context->CopyBuffer(source.buffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                combined_, offset * packet_bytes, source.count * packet_bytes,
                                RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        offset += source.count;
    }
    const unsigned padded = std::bit_ceil(total);
    input.limits = {total, padded, 0, 0};
    const auto dispatch = [&](unsigned pass, unsigned span = 0, unsigned distance = 0) {
        input.limits[2] = span;
        input.limits[3] = distance;
        {
            MapHelper<OrderValues> mapped(context, values_, MAP_WRITE, MAP_FLAG_DISCARD);
            require(static_cast<OrderValues*>(mapped) != nullptr,
                    "Ordered GPU particle constant upload failed");
            *mapped = input;
        }
        context->SetPipelineState(pipelines_[pass]);
        context->CommitShaderResources(bindings_[pass], RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->DispatchCompute({(padded + 63) / 64, 1, 1});
        StateTransitionDesc barrier(keys_, RESOURCE_STATE_UNORDERED_ACCESS,
                                    RESOURCE_STATE_UNORDERED_ACCESS);
        context->TransitionResourceStates(1, &barrier);
    };
    dispatch(0);
    for (unsigned span = 2; span <= padded; span *= 2)
        for (unsigned distance = span / 2; distance; distance /= 2)
            dispatch(1, span, distance);
    dispatch(2);
    count_ = total;
}
void ParticleGpuOrder::clear() {
    count_ = 0;
    bindings_ = {};
    pipelines_ = {};
    combined_.Release();
    keys_.Release();
    ordered_.Release();
    values_.Release();
}
} // namespace forge
