#include "render_backend.hpp"
#include <cstdlib>
#include <stdexcept>
#include <string_view>
namespace forge {
using namespace Diligent;
std::chrono::seconds frame_completion_budget(const GraphicsAdapterInfo& adapter) {
    // Software first-use work shares the host CPU; keep its finite allowance
    // separate from hardware while preserving the same submission bound.
    return std::chrono::seconds(adapter.Type == ADAPTER_TYPE_SOFTWARE ? 30 : 5);
}
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
        if (shader.ShaderCompiler == SHADER_COMPILER_DEFAULT) {
            shader.ShaderCompiler = SHADER_COMPILER_FXC;
            // Isolated source-only differential. Explicit compiler/version and
            // optimization requests retain their ordinary behavior.
            if (shader.Desc.ShaderType == SHADER_TYPE_COMPUTE &&
                shader.HLSLVersion == ShaderVersion{} &&
                shader.ShaderOptimizationLevel == SHADER_OPTIMIZATION_LEVEL_DEFAULT) {
                if (const char* selected = std::getenv("FORGE_NATIVE_FXC_PROBE")) {
                    const std::string_view mode{selected};
                    if (mode == "dxc")
                        shader.ShaderCompiler = SHADER_COMPILER_DXC;
                    else if (mode == "fxc-50")
                        shader.HLSLVersion = {5, 0};
                    else if (mode == "fxc-50-disabled") {
                        shader.HLSLVersion = {5, 0};
                        shader.ShaderOptimizationLevel = SHADER_OPTIMIZATION_LEVEL_DISABLED;
                    } else if (mode == "fxc-disabled")
                        shader.ShaderOptimizationLevel = SHADER_OPTIMIZATION_LEVEL_DISABLED;
                    else if (mode == "fxc-0")
                        shader.ShaderOptimizationLevel = SHADER_OPTIMIZATION_LEVEL_0;
                    else if (mode == "fxc-2")
                        shader.ShaderOptimizationLevel = SHADER_OPTIMIZATION_LEVEL_2;
                    else if (mode == "fxc-3")
                        shader.ShaderOptimizationLevel = SHADER_OPTIMIZATION_LEVEL_3;
                    else
                        require(mode == "fxc-default", "Unknown isolated compiler probe mode");
                }
            }
        }
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
