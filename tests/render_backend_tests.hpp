#pragma once
#include "render_backend.hpp"
#include <stdexcept>
inline void check_renderer_backend_policy() {
    using namespace Diligent;
    const auto check = [](bool ok) {
        if (!ok)
            throw std::runtime_error("Renderer backend policy regression");
    };
    const auto reject = [&](auto fn) {
        bool rejected = false;
        try {
            fn();
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected);
    };
    ShaderCreateInfo base;
    base.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    base.Desc.ShaderType = SHADER_TYPE_PIXEL;
    RenderDeviceInfo device;
    for (const auto type : {RENDER_DEVICE_TYPE_D3D12, RENDER_DEVICE_TYPE_VULKAN,
                            RENDER_DEVICE_TYPE_METAL, RENDER_DEVICE_TYPE_WEBGPU}) {
        device.Type = type;
        auto selected = base;
        forge::prepare_renderer_shader(device, selected);
        check(selected.ShaderCompiler ==
              (type == RENDER_DEVICE_TYPE_D3D12 ? SHADER_COMPILER_FXC : SHADER_COMPILER_DEFAULT));
        check(forge::emulated_resource_arrays(device) == (type == RENDER_DEVICE_TYPE_WEBGPU));
        check(bool(selected.WebGPUEmulatedArrayIndexSuffix) == (type == RENDER_DEVICE_TYPE_WEBGPU));
        selected.Desc.ShaderType = SHADER_TYPE_GEOMETRY;
        reject([&] { forge::prepare_renderer_shader(device, selected); });
    }
    device.Type = RENDER_DEVICE_TYPE_VULKAN;
    auto selected = base;
    selected.ShaderCompiler = SHADER_COMPILER_FXC;
    reject([&] { forge::prepare_renderer_shader(device, selected); });
    selected = base;
    selected.Desc.ShaderType = SHADER_TYPE_GEOMETRY;
    device.Features.GeometryShaders = DEVICE_FEATURE_STATE_ENABLED;
    forge::prepare_renderer_shader(device, selected);
    selected.Desc.ShaderType = SHADER_TYPE_COMPUTE;
    reject([&] { forge::prepare_renderer_shader(device, selected); });
    device.Features.ComputeShaders = DEVICE_FEATURE_STATE_ENABLED;
    forge::prepare_renderer_shader(device, selected);
    selected.Desc.ShaderType = SHADER_TYPE_HULL;
    reject([&] { forge::prepare_renderer_shader(device, selected); });
    device.Features.Tessellation = DEVICE_FEATURE_STATE_ENABLED;
    forge::prepare_renderer_shader(device, selected);
    selected = base;
    selected.ShaderCompiler = SHADER_COMPILER_DXC;
    forge::prepare_renderer_shader(device, selected);
    check(selected.HLSLVersion == ShaderVersion{6, 0});
    device.Type = RENDER_DEVICE_TYPE_WEBGPU;
    reject([&] { forge::prepare_renderer_shader(device, selected); });

    SamplerProperties properties;
    properties.MaxAnisotropy = 8;
    properties.LODBiasSupported = properties.BorderSamplingModeSupported = true;
    forge::SamplerBackendLimits limits{-2, 2, forge::SamplerBorderProfile::BlackOrWhite};
    SamplerDesc sampler;
    sampler.MaxAnisotropy = 8;
    sampler.MinLOD = -1; // Legal in both primary and Vulkan normalized-coordinate profiles.
    sampler.MipLODBias = 2;
    sampler.AddressU = TEXTURE_ADDRESS_BORDER;
    forge::prepare_renderer_sampler(properties, limits, sampler);
    sampler.MipLODBias = 3;
    reject([&] { forge::prepare_renderer_sampler(properties, limits, sampler); });
    sampler.MipLODBias = -3;
    reject([&] { forge::prepare_renderer_sampler(properties, limits, sampler); });
    sampler.MipLODBias = 0;
    sampler.MaxAnisotropy = 9;
    reject([&] { forge::prepare_renderer_sampler(properties, limits, sampler); });
    sampler.MaxAnisotropy = 1;
    sampler.BorderColor[0] = .25f;
    reject([&] { forge::prepare_renderer_sampler(properties, limits, sampler); });
    limits.border = forge::SamplerBorderProfile::UnitRange;
    forge::prepare_renderer_sampler(properties, limits, sampler);
    sampler.BorderColor[0] = 2;
    reject([&] { forge::prepare_renderer_sampler(properties, limits, sampler); });
    sampler.AddressU = TEXTURE_ADDRESS_WRAP;
    forge::prepare_renderer_sampler(properties, limits, sampler);
    check(sampler.BorderColor[0] == 0);
    sampler.MipLODBias = 1;
    properties.LODBiasSupported = false;
    reject([&] { forge::prepare_renderer_sampler(properties, limits, sampler); });
    sampler.MipLODBias = 0;
    sampler.AddressU = TEXTURE_ADDRESS_BORDER;
    properties.BorderSamplingModeSupported = false;
    reject([&] { forge::prepare_renderer_sampler(properties, limits, sampler); });
}
