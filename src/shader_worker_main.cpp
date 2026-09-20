// Native types must precede Diligent's concrete command-queue interface.
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include "Graphics/GraphicsEngineD3D12/interface/CommandQueueD3D12.h"
#include "Graphics/GraphicsEngineD3D12/interface/EngineFactoryD3D12.h"
#include "shader_diligent.hpp"
#include "shader_pipeline.hpp"
int main(int argc, char** argv) {
    if (argc != 2 || std::string_view(argv[1]) != "--build-asset")
        return 2;
    using namespace Diligent;
    using namespace forge::asset_detail;
    using Microsoft::WRL::ComPtr;
    const auto staging = std::filesystem::current_path();
    try {
        auto require = [](bool ok, const char* message) {
            if (!ok)
                throw std::runtime_error(message);
        };
        require(read_import_process_recipe(staging) == "forge.shader.diligent",
                "Unsupported shader worker recipe");
        const auto request = read_import_process_request(staging, shader_worker_limits());
        // A software device supplies the pinned Diligent compiler interface without
        // depending on the user's installed adapter or touching the editor device.
        ComPtr<IDXGIFactory4> dxgi;
        require(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&dxgi))), "Shader DXGI factory failed");
        ComPtr<IDXGIAdapter> warp;
        require(SUCCEEDED(dxgi->EnumWarpAdapter(IID_PPV_ARGS(&warp))),
                "Shader WARP adapter unavailable");
        ComPtr<ID3D12Device> native;
        require(
            SUCCEEDED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&native))),
            "Shader WARP device failed");
        ComPtr<ID3D12CommandQueue> native_queue;
        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        require(SUCCEEDED(native->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&native_queue))),
                "Shader WARP queue failed");
        auto* factory = LoadAndGetEngineFactoryD3D12();
        require(factory != nullptr, "Shader Diligent backend unavailable");
        RefCntAutoPtr<ICommandQueueD3D12> queue;
        factory->CreateCommandQueueD3D12(native.Get(), native_queue.Get(), nullptr, &queue);
        require(bool(queue), "Shader Diligent queue failed");
        ICommandQueueD3D12* queues[]{queue};
        EngineD3D12CreateInfo info;
        info.Features.ComputeShaders = DEVICE_FEATURE_STATE_ENABLED;
        info.Features.GeometryShaders = DEVICE_FEATURE_STATE_ENABLED;
        info.Features.Tessellation = DEVICE_FEATURE_STATE_ENABLED;
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        factory->AttachToD3D12Device(native.Get(), 1, queues, info, &device, &context);
        require(device && context, "Shader Diligent device failed");
        const auto input = decode_shader_process_request(
            request, {diligent_shader_compiler_digest(), diligent_shader_compiler_debug()});
        const auto compiled =
            compile_diligent_shader(device, input.program, input.sources, input.permutation);
        require(compiled.data.build_key == input.build_key,
                "Shader compiler input identity changed");
        require(!std::filesystem::exists(staging / "cancel.request"),
                "Shader compilation cancelled");
        write_import_process_result(staging,
                                    {{"program.shader", forge::encode_shader(compiled.data)}},
                                    shader_worker_limits());
        return 0;
    } catch (const std::exception& error) {
        write_import_process_error(staging, "shader.compile.failed", error.what());
        return 1;
    }
}
