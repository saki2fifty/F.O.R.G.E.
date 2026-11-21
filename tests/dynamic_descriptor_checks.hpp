#pragma once
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "presentation_diligent.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace forge::test_dynamic_descriptors {
using namespace Diligent;
using Pixel = std::array<float, 4>;
inline void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
inline void exercise(DiligentPresentation& presentation, IDeviceContext* context) {
    auto* device = presentation.device();
    constexpr unsigned dispatches = 384;
    const std::array<Pixel, 2> colors{{{.125f, .25f, .5f, 1}, {.75f, .5f, .25f, 1}}};
    std::array<RefCntAutoPtr<ITexture>, 2> images;
    for (unsigned i = 0; i < images.size(); ++i) {
        TextureDesc desc;
        desc.Name = "FORGE descriptor snapshot input";
        desc.Type = RESOURCE_DIM_TEX_2D;
        desc.Width = desc.Height = 1;
        desc.Format = TEX_FORMAT_RGBA32_FLOAT;
        desc.Usage = USAGE_IMMUTABLE;
        desc.BindFlags = BIND_SHADER_RESOURCE;
        TextureSubResData level{colors[i].data(), sizeof(Pixel)};
        Diligent::TextureData initial{&level, 1};
        device->CreateTexture(desc, &initial, &images[i]);
        check(bool(images[i]), "Descriptor input creation failed");
    }
    std::array<RefCntAutoPtr<ISampler>, 2> samplers;
    const auto sampler = [&](float border, bool clamp) {
        SamplerDesc desc;
        desc.Name = "FORGE descriptor snapshot sampler";
        desc.MinFilter = desc.MagFilter = desc.MipFilter = FILTER_TYPE_POINT;
        desc.AddressU = desc.AddressV = desc.AddressW =
            clamp ? TEXTURE_ADDRESS_CLAMP : TEXTURE_ADDRESS_BORDER;
        for (auto& value : desc.BorderColor)
            value = border;
        RefCntAutoPtr<ISampler> result;
        device->CreateSampler(desc, &result);
        check(bool(result), "Descriptor sampler creation failed");
        return result;
    };
    samplers[0] = sampler(0, false);
    samplers[1] = sampler(0, true);
    struct Controls {
        unsigned row;
        float gain;
        unsigned padding[2]{};
    };
    BufferDesc desc;
    desc.Name = "FORGE descriptor snapshot changing root buffer";
    desc.Size = sizeof(Controls);
    desc.Usage = USAGE_DYNAMIC;
    desc.BindFlags = BIND_UNIFORM_BUFFER;
    desc.CPUAccessFlags = CPU_ACCESS_WRITE;
    RefCntAutoPtr<IBuffer> controls;
    device->CreateBuffer(desc, nullptr, &controls);
    desc.Name = "FORGE descriptor snapshot changing array views";
    desc.BindFlags = BIND_SHADER_RESOURCE;
    desc.Mode = BUFFER_MODE_STRUCTURED;
    desc.ElementByteStride = sizeof(Pixel);
    std::array<RefCntAutoPtr<IBuffer>, 2> changing;
    std::array<IDeviceObject*, 2> changing_views{};
    for (unsigned i = 0; i < changing.size(); ++i) {
        device->CreateBuffer(desc, nullptr, &changing[i]);
        check(bool(changing[i]), "Dynamic descriptor input creation failed");
        changing_views[i] = changing[i]->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE);
    }
    desc.Name = "FORGE descriptor snapshot output";
    desc.Size = dispatches * sizeof(Pixel);
    desc.Usage = USAGE_DEFAULT;
    desc.BindFlags = BIND_UNORDERED_ACCESS;
    desc.CPUAccessFlags = CPU_ACCESS_NONE;
    desc.Mode = BUFFER_MODE_STRUCTURED;
    desc.ElementByteStride = sizeof(Pixel);
    RefCntAutoPtr<IBuffer> output, staging;
    device->CreateBuffer(desc, nullptr, &output);
    desc.Name = "FORGE descriptor snapshot readback";
    desc.Usage = USAGE_STAGING;
    desc.BindFlags = BIND_NONE;
    desc.CPUAccessFlags = CPU_ACCESS_READ;
    desc.Mode = BUFFER_MODE_UNDEFINED;
    desc.ElementByteStride = 0;
    device->CreateBuffer(desc, nullptr, &staging);
    check(controls && output && staging, "Descriptor fixture buffer creation failed");
    std::array<RefCntAutoPtr<IPipelineState>, 2> pipelines;
    std::array<RefCntAutoPtr<IShaderResourceBinding>, 2> bindings;
    for (unsigned i = 0; i < pipelines.size(); ++i) {
        // Literal sampler indices keep this independent of resource-array
        // indexing extensions. Every lane contributes a distinct binary weight.
        std::string source = "cbuffer Controls{uint Row;float Gain;uint2 Padding;}"
                             "Texture2D<float4> Image;SamplerState States[8];"
                             "StructuredBuffer<float4> Changing[2];"
                             "RWStructuredBuffer<float4> Output;"
                             "[numthreads(1,1,1)]void main(){float4 value=0;";
        for (unsigned lane = 0; lane < 8; ++lane)
            source += "value+=Image.SampleLevel(States[" + std::to_string(lane) +
                      "],float2(-.25,.5),0)*" + std::to_string(1u << lane) + ";";
        source += "Output[Row]=value*(Gain/256)+" + std::string(i ? ".25" : ".125") +
                  "+Changing[0][0]+Changing[1][0];}";
        ShaderCreateInfo shader;
        shader.Desc.Name = "FORGE descriptor snapshot compute";
        shader.Desc.ShaderType = SHADER_TYPE_COMPUTE;
        shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
        // This array fixture uses the existing DXC SPIR-V compiler on Vulkan.
        // Native D3D12 must exercise the production default compiler/profile.
        if (device->GetDeviceInfo().Type == RENDER_DEVICE_TYPE_VULKAN)
            shader.ShaderCompiler = SHADER_COMPILER_DXC;
        shader.Source = source.c_str();
        shader.EntryPoint = "main";
        RefCntAutoPtr<IShader> compiled;
        presentation.shader(shader, &compiled);
        check(bool(compiled), "Descriptor fixture shader creation failed");
        ComputePipelineStateCreateInfo info;
        info.PSODesc.Name = "FORGE descriptor snapshot pipeline";
        info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;
        info.pCS = compiled;
        presentation.compute(info, &pipelines[i]);
        check(bool(pipelines[i]), "Descriptor fixture pipeline creation failed");
        pipelines[i]->CreateShaderResourceBinding(&bindings[i], true);
        check(bool(bindings[i]), "Descriptor fixture binding creation failed");
        bindings[i]->GetVariableByName(SHADER_TYPE_COMPUTE, "Controls")->Set(controls);
        bindings[i]
            ->GetVariableByName(SHADER_TYPE_COMPUTE, "Output")
            ->Set(output->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));
        bindings[i]
            ->GetVariableByName(SHADER_TYPE_COMPUTE, "Changing")
            ->SetArray(changing_views.data(), 0, Uint32(changing_views.size()));
    }
    const auto dispatch = [&](unsigned row, unsigned pipeline, unsigned image, float gain,
                              const std::array<IDeviceObject*, 8>& states) {
        {
            MapHelper<Controls> mapped(context, controls, MAP_WRITE, MAP_FLAG_DISCARD);
            check(bool(static_cast<Controls*>(mapped)), "Changing root buffer map failed");
            *mapped = Controls{row, gain};
        }
        for (unsigned i = 0; i < changing.size(); ++i) {
            MapHelper<Pixel> mapped(context, changing[i], MAP_WRITE, MAP_FLAG_DISCARD);
            check(bool(static_cast<Pixel*>(mapped)), "Dynamic array view map failed");
            mapped->fill(float((row + i) % 8) / 16);
        }
        auto* binding = bindings[pipeline].RawPtr();
        binding->GetVariableByName(SHADER_TYPE_COMPUTE, "Image")
            ->Set(images[image]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
        binding->GetVariableByName(SHADER_TYPE_COMPUTE, "States")->SetArray(states.data(), 0, 8);
        context->SetPipelineState(pipelines[pipeline]);
        context->CommitShaderResources(binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->DispatchCompute(DispatchComputeAttribs{1, 1, 1});
    };
    const auto verify = [&](const std::vector<Pixel>& expected) {
        context->CopyBuffer(output, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging, 0,
                            expected.size() * sizeof(Pixel),
                            RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->WaitForIdle();
        MapHelper<Pixel> mapped(context, staging, MAP_READ, MAP_FLAG_DO_NOT_WAIT);
        check(bool(static_cast<Pixel*>(mapped)), "Descriptor snapshot readback failed");
        for (unsigned i = 0; i < expected.size(); ++i)
            check(mapped[i] == expected[i],
                  "Descriptor reuse changed texture, sampler or root buffer");
    };
    const auto frame = [&](bool flush) {
        std::vector<Pixel> expected;
        for (unsigned row = 0; row < dispatches; ++row) {
            const unsigned pipeline = row % 2, image = (row / 3) % 2, pattern = (row / 2) % 8;
            const float gain = float(1u << (row % 3));
            std::array<IDeviceObject*, 8> states{};
            unsigned weight = 0;
            for (unsigned lane = 0; lane < 8; ++lane) {
                const bool clamp = (pattern >> (lane % 3)) & 1;
                states[lane] = samplers[clamp];
                if (clamp)
                    weight += 1u << lane;
            }
            Pixel value{};
            for (unsigned c = 0; c < value.size(); ++c)
                value[c] = colors[image][c] * float(weight) * gain / 256 +
                           (pipeline ? .25f : .125f) + float(row % 8) / 16 +
                           float((row + 1) % 8) / 16;
            expected.push_back(value);
            dispatch(row, pipeline, image, gain, states);
            if (flush && (row == 173 || row == 300))
                context->Flush();
        }
        verify(expected);
        context->FinishFrame();
    };
    frame(false);
    frame(true);
    if (device->GetDeviceInfo().Type == RENDER_DEVICE_TYPE_D3D12) {
        // Unique live samplers deliberately exceed the original finite budget.
        // Exhaustion must be a recoverable exception before a null native copy.
        std::vector<RefCntAutoPtr<ISampler>> unique;
        std::vector<Pixel> expected;
        bool exhausted = false;
        for (unsigned row = 0; row < dispatches; ++row) {
            const float border = float(row + 1) / 1024;
            unique.push_back(sampler(border, false));
            std::array<IDeviceObject*, 8> states{};
            states.fill(unique.back());
            try {
                dispatch(row, row % 2, 0, 1, states);
            } catch (const std::exception& error) {
                check(std::string(error.what()).find("descriptor budget exhausted") !=
                          std::string::npos,
                      "Descriptor stress failed outside the finite allocation guard");
                exhausted = true;
                break;
            }
            Pixel value{};
            value.fill(border * 255 / 256 + (row % 2 ? .25f : .125f) + float(row % 8) / 16 +
                       float((row + 1) % 8) / 16);
            expected.push_back(value);
        }
        check(exhausted && !expected.empty(), "Finite descriptor budget was not exercised");
        verify(expected);
        context->FinishFrame();
        frame(false);
        std::cout << "Native descriptor exhaustion and recovery: PASS\n";
    }
    std::cout
        << "Alternating PSO/SRB descriptors, live root buffers, flush and frame retirement: PASS\n";
}
} // namespace forge::test_dynamic_descriptors
