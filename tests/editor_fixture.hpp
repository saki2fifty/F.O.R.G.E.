#pragma once
#include <atomic>
#include <cstdio>
#include <cstring>
#include <limits>
// Compiled only into the automated editor fixture executable, never the shipped editor.
// Native D3D declarations must precede the Diligent command queue interface.
#include <d3d12.h>

#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/Fence.h"
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
    bool prepared = false, workflow = false, physics = false, sdk_play = false;
    bool scene_create = false, hierarchy_create = false;
    bool component_inspection_requested = false;
    float scene_image_y = 0;
    AssetId prefab;
    // SDK reference fixture paths. The constructor populates these
    // when --sdk-play is supplied. project points at an ordinary
    // editable reference project (NEVER deleted by the fixture);
    // sdk_root points at the installed shared SDK; user_data points
    // at a private writable base passed via the PlaySession
    // user-data override so this run does not touch the real
    // user's saves.
    std::filesystem::path sdk_root, user_data;
    explicit EditorFixture(int argc, char** argv) {
        physics = argc == 4 && std::string(argv[2]) == "--physics";
        // --sdk-play needs EXE OUTPUT --sdk-play PROJECT SDK USERDATA: 6 args.
        sdk_play = argc == 6 && std::string(argv[2]) == "--sdk-play";
        if (!physics && !sdk_play && argc != 2 &&
            (argc != 3 || std::string(argv[2]) != "--workflow"))
            throw std::runtime_error(
                "Expected fixture output directory [--workflow | --physics PROJECT | "
                "--sdk-play PROJECT SDK USERDATA]");
        workflow = argc == 3 || physics;
        output = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(output);
        if (sdk_play) {
            project = std::filesystem::absolute(std::filesystem::u8path(argv[3]));
            sdk_root = std::filesystem::absolute(std::filesystem::u8path(argv[4]));
            // argv[5] is the private user-data base; main hands it
            // to PlaySession via set_user_data_override.
            user_data = std::filesystem::absolute(std::filesystem::u8path(argv[5]));
            std::filesystem::create_directories(user_data);
            config = output / ".fixture-preferences";
        } else {
            project = physics ? std::filesystem::absolute(std::filesystem::u8path(argv[3]))
                              : output / ("project-" + AssetId::generate().str());
            config = physics ? output / ".fixture-preferences" : project / ".fixture-preferences";
            if (!physics)
                SceneDocument::create_project(project, "UX fixture");
        }
        std::filesystem::create_directories(config);
    }
    ~EditorFixture() {
        active_wait_probe = nullptr;
        std::error_code ec;
        if (!physics && !sdk_play)
            std::filesystem::remove_all(project, ec);
        if (sdk_play)
            std::filesystem::remove_all(user_data, ec);
    }
    const char* focused_document() const {
        if (workflow)
            return nullptr;
        switch (stage) {
        case 41:
            return "###Texture import";
        case 42:
        case 64:
        case 65:
        case 66:
            return "###Material";
        case 43:
        case 59:
        case 67:
        case 68:
        case 69:
        case 73:
        case 74:
        case 75:
            return "###Model import";
        case 44:
        case 45:
        case 60:
        case 61:
        case 62:
            return "###Asset viewer";
        case 46:
            return "Content";
        case 47:
        case 48:
        case 49:
            return "###Audio clip";
        case 50:
        case 51:
        case 52:
        case 63:
            return "###Shader import";
        case 53:
        case 54:
        case 55:
            return "Loaded resources";
        case 56:
        case 57:
        case 58:
        case 70:
        case 71:
        case 72:
        case 76:
        case 77:
        case 78:
        case 85:
        case 86:
        case 87:
        case 88:
        case 89:
        case 90:
        case 91:
        case 92:
        case 93:
            return "Inspector";
        case 94:
        case 95:
        case 96:
            return "Scene lighting";
        case 79:
        case 80:
        case 81:
        case 82:
        case 83:
        case 84:
            return "###Native";
        default:
            return nullptr;
        }
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
    inline static std::atomic<unsigned long long> diagnostic_frame{0}, diagnostic_flags{0};
    inline static std::atomic<int> diagnostic_phase{0};
    inline static std::atomic<unsigned long long> wait_error_frame{0}, unexpected_errors{0};
    inline static thread_local Diligent::IFence* active_wait_probe = nullptr;
    inline static std::atomic<unsigned long long> wait_completed{0};
    Diligent::RefCntAutoPtr<Diligent::IFence> frame_fence;
    unsigned long long first_stalled_fence = 0, submitted_frame = 0, backlog_waits = 0,
                       unexplained_waits = 0;
    Uint64 present_started = 0;
    static void DILIGENT_CALL_TYPE message(Diligent::DEBUG_MESSAGE_SEVERITY severity,
                                           const char* text, const char* function, const char* file,
                                           int line) {
        if (severity == Diligent::DEBUG_MESSAGE_SEVERITY_ERROR && text &&
            std::strstr(text, "Timeout elapsed while waiting for the frame waitable object.")) {
            // Pinned WaitForFrame invokes this callback synchronously inside
            // Present. A thread-local borrow is valid only across that call;
            // callbacks from another thread cannot borrow this frame's fence.
            wait_completed = active_wait_probe ? active_wait_probe->GetCompletedValue()
                                               : std::numeric_limits<unsigned long long>::max();
            wait_error_frame = diagnostic_frame.load();
        } else if (severity >= Diligent::DEBUG_MESSAGE_SEVERITY_ERROR)
            ++unexpected_errors;
        std::fprintf(stderr,
                     "FORGE graphics diagnostic t=%llu frame=%llu phase=%d flags=%llu severity=%d "
                     "%s (%s:%d %s)\n",
                     static_cast<unsigned long long>(SDL_GetTicks()), diagnostic_frame.load(),
                     diagnostic_phase.load(), diagnostic_flags.load(), int(severity), text,
                     file ? file : "", line, function ? function : "");
    }
    void graphics_context(SDL_Window* window, int phase, Diligent::IRenderDevice* device = nullptr,
                          Diligent::IDeviceContext* context = nullptr) {
        if (phase == 0)
            ++diagnostic_frame;
        diagnostic_flags = SDL_GetWindowFlags(window);
        diagnostic_phase = phase;
        const auto frame = diagnostic_frame.load();
        if (phase == 1) {
            if (!frame_fence) {
                Diligent::FenceDesc desc;
                desc.Name = "FORGE fixture frame completion probe";
                device->CreateFence(desc, &frame_fence);
                if (!frame_fence)
                    throw std::runtime_error("Fixture completion probe unavailable");
            }
            // Exact pinned EnqueueSignal does not flush or wait. Present flushes
            // this marker with its normal commands; the shipped editor is unchanged.
            context->EnqueueSignal(frame_fence, frame);
            submitted_frame = frame;
            active_wait_probe = frame_fence.RawPtr();
            present_started = SDL_GetTicks();
        } else if (phase == 2 && frame_fence) {
            active_wait_probe = nullptr;
            const auto completed = frame_fence->GetCompletedValue();
            const bool timeout = wait_error_frame.load() == frame;
            if (timeout) {
                if (wait_completed.load() < frame)
                    ++backlog_waits;
                else
                    ++unexplained_waits;
            }
            const bool recovered = first_stalled_fence && completed >= first_stalled_fence;
            if (timeout || recovered) {
                std::fprintf(
                    stderr,
                    "FORGE GPU completion frame=%llu completed=%llu at_error=%llu present_ms=%llu "
                    "wait_error=%d previous_stall_completed=%d\n",
                    frame, static_cast<unsigned long long>(completed), wait_completed.load(),
                    static_cast<unsigned long long>(SDL_GetTicks() - present_started), int(timeout),
                    int(recovered));
                if (!first_stalled_fence && timeout)
                    first_stalled_fence = frame;
                if (recovered && !timeout)
                    first_stalled_fence = 0;
            }
        }
    }
    // Called only after the normal final Flush/WaitForIdle. Keep all raw messages;
    // only a wait observed with unfinished work AND full later completion is classified.
    void validate_graphics() const {
        const auto completed = frame_fence ? frame_fence->GetCompletedValue() : 0;
        std::fprintf(stderr,
                     "FORGE graphics acceptance backlog_waits=%llu unexplained_waits=%llu "
                     "unexpected_errors=%llu submitted=%llu completed=%llu\n",
                     backlog_waits, unexplained_waits, unexpected_errors.load(), submitted_frame,
                     static_cast<unsigned long long>(completed));
        if (unexpected_errors.load() || unexplained_waits || completed != submitted_frame)
            throw std::runtime_error("Unclassified graphics error or incomplete GPU work; inspect "
                                     "the retained native diagnostic trace");
    }
    void capture(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
                 Diligent::ITextureView* view, bool complete = true,
                 const std::string& label = {}) {
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
                               "content-file-review-200",
                               "content-file-review-150",
                               "texture-import-150",
                               "material-editor-150",
                               "model-preview-150",
                               "mesh-preview-150",
                               "imported-material-preview-150",
                               "content-thumbnails-150",
                               "audio-import",
                               "audio-import-150",
                               "audio-import-200",
                               "shader-import",
                               "shader-import-150",
                               "shader-import-200",
                               "loaded-resources",
                               "loaded-resources-150",
                               "loaded-resources-200",
                               "asset-picker",
                               "asset-picker-150",
                               "asset-picker-200",
                               "model-lod-import",
                               "mesh-lods",
                               "mesh-lods-150",
                               "mesh-lods-200",
                               "surface-shader-import",
                               "surface-material",
                               "surface-material-150",
                               "surface-material-200",
                               "model-variant-placement",
                               "model-variant-placement-150",
                               "model-variant-placement-200",
                               "model-variant-inspector",
                               "model-variant-inspector-150",
                               "model-variant-inspector-200",
                               "model-import-notes",
                               "model-import-notes-150",
                               "model-import-notes-200",
                               "custom-component-inspector",
                               "custom-component-inspector-150",
                               "custom-component-inspector-200",
                               "custom-component-tools",
                               "custom-component-tools-150",
                               "custom-component-tools-200",
                               "custom-component-migration",
                               "custom-component-migration-150",
                               "custom-component-migration-200",
                               "camera-inspector",
                               "camera-inspector-150",
                               "camera-inspector-200",
                               "light-inspector",
                               "light-inspector-150",
                               "light-inspector-200",
                               "mesh-renderer-inspector",
                               "mesh-renderer-inspector-150",
                               "mesh-renderer-inspector-200",
                               "scene-lighting",
                               "scene-lighting-150",
                               "scene-lighting-200",
                               "source-import-choose",
                               "source-import-choose-150",
                               "source-import-choose-200",
                               "source-import-review",
                               "source-import-review-150",
                               "source-import-review-200"};
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
        const std::string name = label.empty() ? names[stage] : label;
        std::ofstream image(output /
                                (std::string(complete ? "editor-" : "stalled-") + name + ".ppm"),
                            std::ios::binary);
        image << "P6\n" << desc.Width << ' ' << desc.Height << "\n255\n";
        for (unsigned y = 0; y < desc.Height; ++y)
            for (unsigned x = 0; x < desc.Width; ++x)
                image.write(static_cast<const char*>(data.pData) + y * data.Stride + x * 4, 3);
        context->UnmapTextureSubresource(staging, 0, 0);
        if (!image)
            throw std::runtime_error("Editor fixture image write failed");
        std::cout << (complete ? "Captured editor stage " : "Captured stalled backbuffer ") << stage
                  << " (" << name << ") in " << SDL_GetTicks() - stage_started << " ms\n"
                  << std::flush;
        if (!complete || !label.empty())
            return;
        ++stage;
        stage_started = SDL_GetTicks();
        frames = 0;
        prepared = false;
    }
};
} // namespace forge::test
