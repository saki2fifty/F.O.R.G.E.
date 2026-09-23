#include <d3d12.h>

#include "Graphics/GraphicsEngineD3D12/interface/CommandQueueD3D12.h"
#include "Graphics/GraphicsEngineD3D12/interface/EngineFactoryD3D12.h"
#include "game_device.hpp"
#include <SDL3/SDL.h>
#include <dxgi1_4.h>
#include <stdexcept>
#include <wrl/client.h>
namespace forge {
GameDevice::GameDevice(SDL_Window* window, bool software) {
    using namespace Diligent;
    auto* factory = LoadAndGetEngineFactoryD3D12();
    if (!factory)
        throw std::runtime_error("D3D12 backend unavailable");
    EngineD3D12CreateInfo info;
    if (software) {
        // WARP acceptance adapter only. No raw native handles escape this file.
        using Microsoft::WRL::ComPtr;
        auto check = [](HRESULT hr) {
            if (FAILED(hr))
                throw std::runtime_error("WARP device initialization failed");
        };
        ComPtr<IDXGIFactory4> dxgi;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<ID3D12Device> native;
        ComPtr<ID3D12CommandQueue> queue;
        check(CreateDXGIFactory1(IID_PPV_ARGS(&dxgi)));
        check(dxgi->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
        check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&native)));
        D3D12_COMMAND_QUEUE_DESC desc{};
        check(native->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)));
        RefCntAutoPtr<ICommandQueueD3D12> wrapped;
        factory->CreateCommandQueueD3D12(native.Get(), queue.Get(), nullptr, &wrapped);
        if (!wrapped)
            throw std::runtime_error("WARP queue attachment failed");
        ICommandQueueD3D12* queues[]{wrapped};
        factory->AttachToD3D12Device(native.Get(), 1, queues, info, &device, &context);
    } else
        factory->CreateDeviceAndContextsD3D12(info, &device, &context);
    if (!device || !context)
        throw std::runtime_error("Graphics device initialization failed");
    auto* hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                        SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (!hwnd)
        throw std::runtime_error("SDL native window is unavailable");
    SwapChainDesc desc;
    desc.ColorBufferFormat = TEX_FORMAT_RGBA8_UNORM;
    desc.DepthBufferFormat = TEX_FORMAT_UNKNOWN;
    factory->CreateSwapChainD3D12(device, context, desc, FullScreenModeDesc{},
                                  Win32NativeWindow{hwnd}, &swap);
    if (!swap)
        throw std::runtime_error("Graphics swap chain initialization failed");
}
} // namespace forge
