#pragma once
#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"
// Requires the native presentation test helpers from the same WARP fixture.
void check_diligentfx_patch(forge::DiligentPresentation& presentation,
                            Diligent::IDeviceContext* context) {
    using namespace Diligent;
    ShaderCreateInfo shader;
    shader.Desc.Name = "FORGE upstream differential proof (original FXC warnings expected)";
    shader.Desc.ShaderType = SHADER_TYPE_COMPUTE;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.ShaderCompiler = SHADER_COMPILER_FXC;
    shader.HLSLVersion = {5, 1};
    shader.EntryPoint = "main";
    shader.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();
    const auto read = [](const char* relative) {
        std::ifstream file(std::filesystem::path(FORGE_TEST_FX_SOURCE) / relative);
        require(bool(file), "Exact upstream reference shader missing");
        return std::string(std::istreambuf_iterator<char>(file), {});
    };
    auto iridescence = read("Shaders/PBR/private/Iridescence.fxh");
    const auto common = read("Shaders/Common/public/PBR_Common.fxh");
    const auto begin = common.find("float LambdaSheenNumericHelper(");
    const auto end = common.find("float LambdaSheen(", begin);
    require(begin != std::string::npos && end != std::string::npos,
            "Reference sheen function changed");
    auto sheen = common.substr(begin, end - begin);
    const auto replace = [](std::string& text, const std::string& from, const std::string& to) {
        for (std::size_t pos = 0; (pos = text.find(from, pos)) != std::string::npos;
             pos += to.size())
            text.replace(pos, from.size(), to);
    };
    for (const auto* name : {"_IRIDESCENCE_FXH_", "EvalIridescence", "EvalSensitivity",
                             "Fresnel0ToIor", "IorToFresnel0", "Sqr"})
        replace(iridescence, name, std::string("Reference_") + name);
    replace(sheen, "LambdaSheenNumericHelper", "Reference_LambdaSheenNumericHelper");
    const std::string source =
        std::string("#include \"PBR_Common.fxh\"\n#include \"Iridescence.fxh\"\n") + iridescence +
        sheen + R"(
RWStructuredBuffer<float4> Destination;
[numthreads(1,1,1)]
void main(uint id:SV_DispatchThreadID) {
    float outside=1, eta=1.3, cosine=(id%9)/8.0, thickness=400;
    float3 f0=float3(.04,.3,.8);
    if(id==9){outside=1.5;eta=1;cosine=.1;}
    if(id==10)thickness=0;
    if(id>=11 && id<17) {
        float special=asfloat(id%3==0?0x7fc00001u:(id%3==1?0x7f800000u:0xff800000u));
        if(id<14)cosine=special;else eta=special;
    }
    if(id>=17 && id<20)thickness=asfloat(id==17?0x7fc00001u:(id==18?0x7f800000u:0xff800000u));
    if(id>=20)f0.x=asfloat(id==20?0x7fc00001u:(id==21?0x7f800000u:0xff800000u));
    float x=(id%9)/8.0;
    Destination[id*2]=float4(EvalIridescence(outside,eta,cosine,thickness,f0),LambdaSheenNumericHelper(x,.5));
    Destination[id*2+1]=float4(Reference_EvalIridescence(outside,eta,cosine,thickness,f0),Reference_LambdaSheenNumericHelper(x,.5));
})";
    shader.Source = source.c_str();
    RefCntAutoPtr<IShader> compute;
    presentation.shader(shader, &compute);
    ComputePipelineStateCreateInfo pso;
    pso.PSODesc.Name = "FORGE shader patch shader admission";
    pso.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;
    pso.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    pso.pCS = compute;
    RefCntAutoPtr<IPipelineState> pipeline;
    presentation.compute(pso, &pipeline);
    BufferDesc desc;
    desc.Name = "FORGE shader patch results";
    desc.Size = 46 * 4 * sizeof(float);
    desc.Mode = BUFFER_MODE_STRUCTURED;
    desc.ElementByteStride = 4 * sizeof(float);
    desc.BindFlags = BIND_UNORDERED_ACCESS;
    RefCntAutoPtr<IBuffer> output, staging;
    presentation.device()->CreateBuffer(desc, nullptr, &output);
    desc.Name = "FORGE shader patch readback";
    desc.Mode = BUFFER_MODE_UNDEFINED;
    desc.ElementByteStride = 0;
    desc.BindFlags = BIND_NONE;
    desc.Usage = USAGE_STAGING;
    desc.CPUAccessFlags = CPU_ACCESS_READ;
    presentation.device()->CreateBuffer(desc, nullptr, &staging);
    require(output && staging, "Shader patch buffers unavailable");
    RefCntAutoPtr<IShaderResourceBinding> bindings;
    pipeline->CreateShaderResourceBinding(&bindings, true);
    auto* target = bindings->GetVariableByName(SHADER_TYPE_COMPUTE, "Destination");
    require(target != nullptr, "Shader patch output binding absent");
    target->Set(output->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));
    context->SetPipelineState(pipeline);
    context->CommitShaderResources(bindings, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->DispatchCompute(DispatchComputeAttribs{23, 1, 1});
    context->CopyBuffer(output, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging, 0, desc.Size,
                        RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->WaitForIdle();
    void* mapped = nullptr;
    context->MapBuffer(staging, MAP_READ, MAP_FLAG_DO_NOT_WAIT, mapped);
    require(mapped != nullptr, "Shader patch readback unavailable");
    std::array<std::array<float, 4>, 46> result;
    std::memcpy(result.data(), mapped, sizeof(result));
    context->UnmapBuffer(staging, MAP_READ);
    context->FinishFrame();
    for (unsigned i = 0; i < 23; ++i) {
        std::cout << "BRDF differential case " << i << ":";
        for (unsigned c = 0; c < 4; ++c)
            std::cout << " " << result[i * 2][c] << "/" << result[i * 2 + 1][c];
        std::cout << '\n';
    }
    for (unsigned i = 0; i < 23; ++i) {
        for (unsigned c = 0; c < 4; ++c) {
            const float actual = result[i * 2][c], expected = result[i * 2 + 1][c];
            require(std::isnan(actual) == std::isnan(expected) &&
                        std::isinf(actual) == std::isinf(expected),
                    "Shader patch changed NaN/Inf semantics");
            if (std::isfinite(expected))
                require(std::abs(actual - expected) <= 2e-5f * std::max(1.f, std::abs(expected)),
                        "Shader patch changed supported BRDF output");
            else if (std::isinf(expected))
                require(std::signbit(actual) == std::signbit(expected),
                        "Shader patch changed infinity sign");
        }
    }
    for (unsigned c = 0; c < 3; ++c)
        require(result[18][c] == 1, "Shader patch lost total internal reflection");
}
