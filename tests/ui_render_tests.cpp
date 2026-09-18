// Independent RmlUi/Diligent host: deliberately no ImGui/editor linkage.
// Diligent's native queue interface requires the Direct3D declarations first.
#include <d3d12.h>

#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngineD3D12/interface/CommandQueueD3D12.h"
#include "Graphics/GraphicsEngineD3D12/interface/EngineFactoryD3D12.h"
#include <RmlUi/Core.h>
#include <cstring>
#include <dxgi1_4.h>
#include <forge/ui_diligent.hpp>
#include <forge/ui_presenter.hpp>
#include <fstream>
#include <iostream>
#include <windows.h>
#include <wrl/client.h>
using namespace Diligent;
using Microsoft::WRL::ComPtr;
namespace {
void require(bool value, const char* text) {
    if (!value)
        throw std::runtime_error(text);
}
void check(HRESULT result, const char* text) { require(SUCCEEDED(result), text); }
using Pixels = std::vector<std::array<unsigned char, 4>>;
Pixels readback(IRenderDevice* device, IDeviceContext* context, ITextureView* view) {
    auto* texture = view->GetTexture();
    auto desc = texture->GetDesc();
    desc.Name = "FORGE test readback";
    desc.Usage = USAGE_STAGING;
    desc.BindFlags = BIND_NONE;
    desc.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<ITexture> staging;
    device->CreateTexture(desc, nullptr, &staging);
    require(bool(staging), "Readback texture allocation failed");
    context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    CopyTextureAttribs copy;
    copy.pSrcTexture = texture;
    copy.pDstTexture = staging;
    copy.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    copy.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    context->CopyTexture(copy);
    context->WaitForIdle();
    MappedTextureSubresource data;
    context->MapTextureSubresource(staging, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr, data);
    require(data.pData != nullptr, "Readback map failed");
    Pixels result(desc.Width * desc.Height);
    for (unsigned y = 0; y < desc.Height; ++y)
        std::memcpy(result.data() + y * desc.Width,
                    static_cast<const unsigned char*>(data.pData) + y * data.Stride,
                    desc.Width * 4);
    context->UnmapTextureSubresource(staging, 0, 0);
    context->FinishFrame();
    return result;
}
void save(const Pixels& pixels, unsigned width, unsigned height,
          const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary);
    output << "P6\n" << width << ' ' << height << "\n255\n";
    for (auto p : pixels)
        output.write(reinterpret_cast<const char*>(p.data()), 3);
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need image output and font");
        const std::filesystem::path images(argv[1]);
        std::filesystem::create_directories(images);
        ComPtr<IDXGIFactory4> dxgi;
        check(CreateDXGIFactory1(IID_PPV_ARGS(&dxgi)), "DXGI factory failed");
        ComPtr<IDXGIAdapter> warp;
        check(dxgi->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "WARP adapter unavailable");
        ComPtr<ID3D12Device> native;
        check(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&native)),
              "WARP D3D12 device failed");
        ComPtr<ID3D12CommandQueue> native_queue;
        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        check(native->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&native_queue)),
              "WARP queue failed");
        auto* factory = LoadAndGetEngineFactoryD3D12();
        require(factory != nullptr, "Diligent backend unavailable");
        RefCntAutoPtr<ICommandQueueD3D12> queue;
        factory->CreateCommandQueueD3D12(native.Get(), native_queue.Get(), nullptr, &queue);
        require(bool(queue), "Diligent queue attachment failed");
        ICommandQueueD3D12* queues[] = {queue};
        EngineD3D12CreateInfo engine;
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        factory->AttachToD3D12Device(native.Get(), 1, queues, engine, &device, &context);
        require(device && context, "Diligent device attachment failed");

        forge::UiDiligentRenderer renderer(device);
        auto& r = renderer.render_interface();
        TextureDesc desc;
        desc.Name = "UI fixture target";
        desc.Type = RESOURCE_DIM_TEX_2D;
        desc.Width = 320;
        desc.Height = 160;
        desc.Format = TEX_FORMAT_RGBA8_UNORM;
        desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
        RefCntAutoPtr<ITexture> target;
        device->CreateTexture(desc, nullptr, &target);
        require(bool(target), "UI target allocation");
        auto* rtv = target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        auto start = [&] {
            context->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            float blue[] = {0, 0, 1, 1};
            context->ClearRenderTarget(rtv, blue, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            renderer.begin(context, rtv, 320, 160);
        };
        auto quad = [&](float x, float y, float w, float h, Rml::ColourbPremultiplied color) {
            Rml::Vertex v[] = {{{x, y}, color, {0, 0}},
                               {{x + w, y}, color, {1, 0}},
                               {{x + w, y + h}, color, {1, 1}},
                               {{x, y + h}, color, {0, 1}}};
            int i[] = {0, 1, 2, 0, 2, 3};
            return r.CompileGeometry({v, 4}, {i, 6});
        };
        auto red = quad(20, 20, 80, 60, {128, 0, 0, 128});
        require(red != 0, "Geometry compilation");
        start();
        r.RenderGeometry(red, {0, 0}, 0);
        renderer.end();
        auto pixels = readback(device, context, rtv);
        auto p = pixels[30 * 320 + 30];
        require(p[0] >= 127 && p[0] <= 129 && p[2] >= 126 && p[2] <= 128,
                "Premultiplied alpha blend");
        save(pixels, 320, 160, images / "ui-alpha.ppm");
        start();
        r.EnableScissorRegion(true);
        r.SetScissorRegion(Rml::Rectanglei::FromPositionSize({40, 20}, {20, 40}));
        r.RenderGeometry(red, {0, 0}, 0);
        renderer.end();
        pixels = readback(device, context, rtv);
        require(pixels[30 * 320 + 30][0] == 0 && pixels[30 * 320 + 50][0] > 120,
                "Scissor clipping");
        save(pixels, 320, 160, images / "ui-scissor.ppm");
        start();
        auto matrix = Rml::Matrix4f::Translate(100, 0, 0);
        r.SetTransform(&matrix);
        r.RenderGeometry(red, {0, 0}, 0);
        renderer.end();
        pixels = readback(device, context, rtv);
        require(pixels[30 * 320 + 30][0] == 0 && pixels[30 * 320 + 130][0] > 120,
                "Transformed geometry");
        save(pixels, 320, 160, images / "ui-transform.ppm");
        auto mask = quad(40, 30, 20, 20, {255, 255, 255, 255});
        start();
        r.RenderToClipMask(Rml::ClipMaskOperation::Set, mask, {0, 0});
        r.EnableClipMask(true);
        r.RenderGeometry(red, {0, 0}, 0);
        renderer.end();
        pixels = readback(device, context, rtv);
        require(pixels[25 * 320 + 30][0] == 0 && pixels[35 * 320 + 50][0] > 120,
                "Stencil clipping");
        save(pixels, 320, 160, images / "ui-mask.ppm");
        start();
        r.RenderToClipMask(Rml::ClipMaskOperation::SetInverse, mask, {0, 0});
        r.EnableClipMask(true);
        r.RenderGeometry(red, {0, 0}, 0);
        renderer.end();
        pixels = readback(device, context, rtv);
        require(pixels[25 * 320 + 30][0] > 120 && pixels[35 * 320 + 50][0] == 0,
                "Inverse stencil clipping");
        save(pixels, 320, 160, images / "ui-inverse-mask.ppm");
        start();
        r.RenderToClipMask(Rml::ClipMaskOperation::Set, mask, {0, 0});
        r.RenderToClipMask(Rml::ClipMaskOperation::Intersect, mask, {10, 0});
        r.EnableClipMask(true);
        r.RenderGeometry(red, {0, 0}, 0);
        renderer.end();
        pixels = readback(device, context, rtv);
        require(pixels[35 * 320 + 45][0] == 0 && pixels[35 * 320 + 55][0] > 120,
                "Intersect stencil clipping");
        save(pixels, 320, 160, images / "ui-intersect-mask.ppm");
        const Rml::byte texels[] = {0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255};
        auto tex = r.GenerateTexture({texels, 16}, {2, 2});
        auto white = quad(0, 0, 40, 40, {255, 255, 255, 255});
        start();
        r.RenderGeometry(white, {0, 0}, tex);
        renderer.end();
        pixels = readback(device, context, rtv);
        require(pixels[10 * 320 + 10][1] > 250 && pixels[10 * 320 + 10][2] < 3,
                "Generated texture sampling");
        save(pixels, 320, 160, images / "ui-texture.ppm");
        r.ReleaseGeometry(red);
        r.ReleaseGeometry(mask);
        r.ReleaseGeometry(white);
        r.ReleaseTexture(tex);
        auto project = images / "project";
        std::filesystem::create_directories(project);
        auto asset = forge::create_ui_example(project);
        std::ifstream font_file(argv[2], std::ios::binary);
        std::string font_string{std::istreambuf_iterator<char>(font_file), {}};
        std::vector<std::byte> font(font_string.size());
        std::memcpy(font.data(), font_string.data(), font.size());
        {
            forge::UiPresenter presenter(r, project, std::move(font));
            presenter.reset("warp", 1);
            auto entity = forge::EntityId::generate();
            nlohmann::json state = {
                {"version", 1},
                {"session", "warp"},
                {"generation", 1u},
                {"revision", 1u},
                {"documents",
                 nlohmann::json::array({{{"entity", entity},
                                         {"asset", asset.id},
                                         {"instance", entity.str() + ":1"},
                                         {"visible", true},
                                         {"layer", 0u},
                                         {"model", {{"tick", 100u}, {"paused", true}}},
                                         {"commands", {"Pause", "Resume", "Step"}}}})}};
            require(presenter.accept(state), presenter.diagnostic().c_str());
            double elapsed = 0;
            for (unsigned size : {640u, 480u, 640u}) {
                desc.Width = size;
                desc.Height = 480;
                target.Release();
                device->CreateTexture(desc, nullptr, &target);
                rtv = target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
                context->SetRenderTargets(1, &rtv, nullptr,
                                          RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                float clear[] = {0, 0, 0, 1};
                context->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                presenter.update(++elapsed, size, 480);
                renderer.begin(context, rtv, size, 480);
                presenter.render();
                renderer.end();
                pixels = readback(device, context, rtv);
                unsigned bright = 0;
                for (auto color : pixels)
                    if (color[0] > 150 && color[1] > 150 && color[2] > 150)
                        ++bright;
                require(bright > 150, "Actual RmlUi font/text rendering");
                save(pixels, size, 480, images / ("ui-document-" + std::to_string(size) + ".ppm"));
            }
        }
        context->WaitForIdle();
        std::cout << "Runtime UI D3D12 WARP geometry, clipping, transform, texture, blend, font "
                     "and resize passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
