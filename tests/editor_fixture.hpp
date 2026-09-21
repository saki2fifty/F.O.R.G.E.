#pragma once
// Compiled only into the automated editor fixture executable, never the shipped editor.
// Native D3D declarations must precede the Diligent command queue interface.
#include <d3d12.h>

#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngineD3D12/interface/CommandQueueD3D12.h"
#include "Graphics/GraphicsEngineD3D12/interface/EngineFactoryD3D12.h"
#include "document.hpp"
#include <SDL3/SDL.h>
#include <dxgi1_4.h>
#include <fstream>
#include <iostream>
#include <windows.h>
#include <wrl/client.h>
namespace forge::test {
struct EditorFixture {
    std::filesystem::path output, project, config;
    unsigned stage = 0, frames = 0;
    Uint64 started = SDL_GetTicks(), stage_started = started;
    bool prepared = false;
    bool scene_create = false, hierarchy_create = false;
    float scene_image_y = 0;
    AssetId prefab;
    explicit EditorFixture(int argc, char** argv) {
        if (argc != 2)
            throw std::runtime_error("Expected fixture output directory");
        output = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(output);
        project = output / ("project-" + AssetId::generate().str());
        config = project / ".fixture-preferences";
        SceneDocument::create_project(project, "UX fixture");
        std::filesystem::create_directories(config);
    }
    ~EditorFixture() {
        std::error_code ec;
        std::filesystem::remove_all(project, ec);
    }
    static void device(Diligent::IEngineFactoryD3D12* factory, Diligent::IRenderDevice** device,
                       Diligent::IDeviceContext** context) {
        using Microsoft::WRL::ComPtr;
        auto check = [](HRESULT result) {
            if (FAILED(result))
                throw std::runtime_error("WARP fixture device failed");
        };
        ComPtr<IDXGIFactory4> dxgi;
        check(CreateDXGIFactory1(IID_PPV_ARGS(&dxgi)));
        ComPtr<IDXGIAdapter> warp;
        check(dxgi->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
        ComPtr<ID3D12Device> native;
        check(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&native)));
        ComPtr<ID3D12CommandQueue> native_queue;
        D3D12_COMMAND_QUEUE_DESC desc{};
        check(native->CreateCommandQueue(&desc, IID_PPV_ARGS(&native_queue)));
        Diligent::RefCntAutoPtr<Diligent::ICommandQueueD3D12> queue;
        factory->CreateCommandQueueD3D12(native.Get(), native_queue.Get(), nullptr, &queue);
        if (!queue)
            throw std::runtime_error("WARP fixture queue failed");
        Diligent::ICommandQueueD3D12* queues[] = {queue};
        factory->AttachToD3D12Device(native.Get(), 1, queues, {}, device, context);
    }
    void capture(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
                 Diligent::ITextureView* view, bool complete = true) {
        using namespace Diligent;
        const char* names[] = {"default-scene-inspector",
                               "add-component",
                               "content",
                               "problems",
                               "prefab-source",
                               "project-settings",
                               "game-play",
                               "game-paused-captured",
                               "narrow-100",
                               "narrow-200",
                               "high-scale-200",
                               "ultrawide",
                               "empty-editor",
                               "selected-primitive",
                               "scene-create",
                               "hierarchy-create",
                               "command-palette",
                               "scale-125",
                               "scale-150",
                               "ecs-statistics",
                               "ecs-metrics",
                               "flecs-script",
                               "flecs-script-200",
                               "workspace-folded-200",
                               "workspace-expanded-200",
                               "workspace-restored-100",
                               "texture-import",
                               "texture-import-200",
                               "material-editor",
                               "material-editor-200",
                               "model-preview",
                               "model-preview-200",
                               "mesh-preview",
                               "mesh-preview-200",
                               "imported-material-preview",
                               "imported-material-preview-200",
                               "content-thumbnails",
                               "content-thumbnails-200",
                               "content-file-review",
                               "content-file-review-200"};
        auto* texture = view->GetTexture();
        auto desc = texture->GetDesc();
        desc.Usage = USAGE_STAGING;
        desc.BindFlags = BIND_NONE;
        desc.CPUAccessFlags = CPU_ACCESS_READ;
        RefCntAutoPtr<ITexture> staging;
        device->CreateTexture(desc, nullptr, &staging);
        if (!staging)
            throw std::runtime_error("Editor fixture readback allocation failed");
        context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
        CopyTextureAttribs copy;
        copy.pSrcTexture = texture;
        copy.pDstTexture = staging;
        copy.SrcTextureTransitionMode = copy.DstTextureTransitionMode =
            RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        context->CopyTexture(copy);
        context->WaitForIdle();
        MappedTextureSubresource data;
        context->MapTextureSubresource(staging, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr,
                                       data);
        if (!data.pData)
            throw std::runtime_error("Editor fixture readback map failed");
        std::ofstream image(
            output / (std::string(complete ? "editor-" : "stalled-") + names[stage] + ".ppm"),
            std::ios::binary);
        image << "P6\n" << desc.Width << ' ' << desc.Height << "\n255\n";
        for (unsigned y = 0; y < desc.Height; ++y)
            for (unsigned x = 0; x < desc.Width; ++x)
                image.write(static_cast<const char*>(data.pData) + y * data.Stride + x * 4, 3);
        context->UnmapTextureSubresource(staging, 0, 0);
        if (!image)
            throw std::runtime_error("Editor fixture image write failed");
        std::cout << (complete ? "Captured editor stage " : "Captured stalled backbuffer ") << stage
                  << " (" << names[stage] << ") in " << SDL_GetTicks() - stage_started << " ms\n"
                  << std::flush;
        if (!complete)
            return;
        ++stage;
        stage_started = SDL_GetTicks();
        frames = 0;
        prepared = false;
    }
};
} // namespace forge::test
