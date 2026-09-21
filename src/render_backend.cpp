#include "render_backend.hpp"
#include <stdexcept>
namespace forge {
using namespace Diligent;
bool emulated_resource_arrays(const RenderDeviceInfo& device) { return device.IsWebGPUDevice(); }
void prepare_renderer_shader(const RenderDeviceInfo& device, ShaderCreateInfo& shader) {
    const auto require = [](bool ok, const char* message) {
        if (!ok)
            throw std::runtime_error(message);
    };
    if (shader.Desc.ShaderType == SHADER_TYPE_GEOMETRY)
        require(device.Features.GeometryShaders == DEVICE_FEATURE_STATE_ENABLED,
                "Renderer requires geometry shaders for the selected signed skinning path; "
                "this device has no enabled geometry-shader support");
    if (shader.Desc.ShaderType == SHADER_TYPE_COMPUTE)
        require(device.Features.ComputeShaders == DEVICE_FEATURE_STATE_ENABLED,
                "Renderer requires compute shaders for this operation");
    if (shader.Desc.ShaderType == SHADER_TYPE_HULL || shader.Desc.ShaderType == SHADER_TYPE_DOMAIN)
        require(device.Features.Tessellation == DEVICE_FEATURE_STATE_ENABLED,
                "Renderer requires tessellation for this operation");
    if (shader.ByteCode || shader.SourceLanguage != SHADER_SOURCE_LANGUAGE_HLSL)
        return;
    if (device.Type == RENDER_DEVICE_TYPE_D3D12) {
        require(shader.ShaderCompiler == SHADER_COMPILER_DEFAULT ||
                    shader.ShaderCompiler == SHADER_COMPILER_FXC ||
                    shader.ShaderCompiler == SHADER_COMPILER_DXC,
                "Requested shader compiler is unavailable for D3D12");
        if (shader.ShaderCompiler == SHADER_COMPILER_DEFAULT)
            shader.ShaderCompiler = SHADER_COMPILER_FXC;
        if (shader.HLSLVersion == ShaderVersion{})
            shader.HLSLVersion = shader.ShaderCompiler == SHADER_COMPILER_DXC ? ShaderVersion{6, 0}
                                                                              : ShaderVersion{5, 1};
    } else {
        require(shader.ShaderCompiler != SHADER_COMPILER_FXC,
                "FXC is a Direct3D compiler and cannot compile this renderer backend");
        if (device.IsWebGPUDevice())
            require(
                shader.ShaderCompiler == SHADER_COMPILER_DEFAULT ||
                    shader.ShaderCompiler == SHADER_COMPILER_GLSLANG,
                "Pinned WebGPU HLSL compilation uses GLSLang; explicit compiler is unsupported");
        if (device.IsMetalDevice())
            require(shader.ShaderCompiler == SHADER_COMPILER_DEFAULT,
                    "Explicit Metal compiler overrides are not validated by this renderer");
        // Leave the default compiler/language version to the pinned Diligent
        // backend. Explicit DXC remains available for reproducible native probes.
        if (shader.ShaderCompiler == SHADER_COMPILER_DXC && shader.HLSLVersion == ShaderVersion{})
            shader.HLSLVersion = {6, 0};
    }
    if (emulated_resource_arrays(device))
        shader.WebGPUEmulatedArrayIndexSuffix = "_";
}
} // namespace forge
