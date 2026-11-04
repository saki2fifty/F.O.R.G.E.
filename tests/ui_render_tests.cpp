#include "ui_backdrop_render_tests.hpp"
#include "ui_filter_render_tests.hpp"
// Independent RmlUi/Diligent host: deliberately no ImGui/editor linkage.
#include "Common/interface/Float16.hpp"
#include "Common/interface/RefCntAutoPtr.hpp"
#include "fixtures/texture_jpeg.hpp"
#include "fixtures/ui_png.hpp"
#ifdef FORGE_UI_VULKAN_TEST
#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#else
// Diligent's native queue interface requires the Direct3D declarations first.
#include <d3d12.h>

#include "Graphics/GraphicsEngineD3D12/interface/CommandQueueD3D12.h"
#include "Graphics/GraphicsEngineD3D12/interface/EngineFactoryD3D12.h"
#include <dxgi1_4.h>
#include <windows.h>
#include <wrl/client.h>
#endif
#include "application_state_render_tests.hpp"
#include "ui_animation_render_tests.hpp"
#include "ui_frame_statistics_render_tests.hpp"
#include "ui_localization_render_tests.hpp"
#include <RmlUi/Core.h>
#include <cmath>
#include <cstring>
#include <forge/ui_diligent.hpp>
#include <forge/ui_presenter.hpp>
#include <fstream>
#include <iostream>
using namespace Diligent;
#ifndef FORGE_UI_VULKAN_TEST
using Microsoft::WRL::ComPtr;
#endif
namespace {
void require(bool value, const char* text) {
    if (!value)
        throw std::runtime_error(text);
}
#ifndef FORGE_UI_VULKAN_TEST
void check(HRESULT result, const char* text) { require(SUCCEEDED(result), text); }
#endif
using Pixels = std::vector<std::array<unsigned char, 4>>;
template <class Pixel>
std::vector<Pixel> readback_pixels(IRenderDevice* device, IDeviceContext* context,
                                   ITextureView* view, TEXTURE_FORMAT format) {
    auto* texture = view->GetTexture();
    auto desc = texture->GetDesc();
    require(desc.Format == format, "UI readback pixel format mismatch");
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
    std::vector<Pixel> result(desc.Width * desc.Height);
    for (unsigned y = 0; y < desc.Height; ++y)
        std::memcpy(result.data() + y * desc.Width,
                    static_cast<const unsigned char*>(data.pData) + y * data.Stride,
                    desc.Width * sizeof(Pixel));
    context->UnmapTextureSubresource(staging, 0, 0);
    context->FinishFrame();
    return result;
}
Pixels readback(IRenderDevice* device, IDeviceContext* context, ITextureView* view) {
    return readback_pixels<std::array<unsigned char, 4>>(device, context, view,
                                                         TEX_FORMAT_RGBA8_UNORM);
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
        const bool states_only =
            argc == 5 && std::string_view(argv[4]) == "--application-states-only";
        const bool conditions_only =
            argc == 5 && std::string_view(argv[4]) == "--state-conditions-only";
        require(argc == 3 || argc == 4 || states_only || conditions_only,
                "Need image output, font, optional Gallery and application-state profile");
        const std::filesystem::path images(argv[1]);
        std::filesystem::create_directories(images);
#ifdef FORGE_UI_VULKAN_TEST
        auto* factory = GetEngineFactoryVk();
        require(factory != nullptr, "Native Vulkan factory unavailable");
        EngineVkCreateInfo engine;
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        factory->CreateDeviceAndContextsVk(engine, &device, &context);
        require(device && context, "Native Vulkan UI device unavailable");
#else
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
#endif

        forge::UiDiligentRenderer renderer(device);
        if (states_only || conditions_only) {
            forge::test_state_gallery_pixels::exercise(device, context, renderer, images, argv[2],
                                                       argv[3], readback, save, conditions_only);
            context->WaitForIdle();
            std::cout << "Native application state Game UI and relocated source-free pixel pairs "
                         "passed\n";
            return 0;
        }
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
        {
            auto hdr_desc = desc;
            hdr_desc.Name = "Linear HDR UI fixture";
            hdr_desc.Format = TEX_FORMAT_RGBA16_FLOAT;
            RefCntAutoPtr<ITexture> hdr;
            device->CreateTexture(hdr_desc, nullptr, &hdr);
            require(bool(hdr), "Linear UI target allocation");
            auto* output = hdr->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
            context->SetRenderTargets(1, &output, nullptr,
                                      RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            const float background[] = {0, 0, 2, 1};
            context->ClearRenderTarget(output, background,
                                       RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            const auto geometry = quad(20, 20, 80, 60, {128, 128, 128, 128});
            const Rml::byte texel[] = {32, 0, 0, 128};
            const auto image = r.GenerateTexture({texel, 4}, {1, 1});
            require(geometry && image, "Linear UI fixture resources");
            renderer.begin(context, output, 320, 160);
            r.RenderGeometry(geometry, {0, 0}, image);
            renderer.end();
            const auto linear = readback_pixels<std::array<std::uint16_t, 4>>(
                device, context, output, TEX_FORMAT_RGBA16_FLOAT);
            const auto actual = [&](unsigned x, unsigned y, unsigned channel) {
                return float(Float16(linear.at(y * 320 + x).at(channel)));
            };
            // The two alpha factors multiply. Straight display red is .25;
            // its linear value is .050876, then premultiplied by (128/255)^2.
            require(std::abs(actual(30, 30, 0) - .012819f) < .0001f &&
                        std::abs(actual(30, 30, 2) - 1.496071f) < .002f && actual(30, 30, 3) == 1 &&
                        actual(0, 0, 2) == 2,
                    "UI linear transfer/premultiplication or HDR preservation differs");
            r.ReleaseGeometry(geometry);
            r.ReleaseTexture(image);
            // Existing geometry/texture bindings must still work when switching back.
            start();
            r.RenderGeometry(red, {0, 0}, 0);
            renderer.end();
            require(readback(device, context, rtv) == pixels,
                    "Linear UI output changed the existing display path");
        }
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
        // Real codec -> RmlUi file admission -> Diligent upload -> pixel readback.
        const auto bitmap_project = images / ("bitmaps-" + forge::AssetId::generate().str());
        std::filesystem::create_directories(bitmap_project);
        const std::array<unsigned char, 16> rgba{255, 0,   0, 128, 255, 255, 255, 0,
                                                 0,   255, 0, 255, 0,   0,   255, 255};
        std::vector<unsigned char> bitmap(64 * 64 * 4);
        for (unsigned y = 0; y < 64; ++y)
            for (unsigned x = 0; x < 64; ++x)
                std::copy_n(rgba.data() + ((y / 32) * 2 + x / 32) * 4, 4,
                            bitmap.data() + (y * 64 + x) * 4);
        const auto png = forge::test::encode_ui_png(bitmap.data(), 64, 64);
        auto write = [&](const std::filesystem::path& path, std::span<const std::byte> bytes) {
            std::ofstream f(bitmap_project / path, std::ios::binary);
            f.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            require(bool(f), "Native bitmap fixture write failed");
        };
        write("alpha.png", png);
        write("photo.jpg", {reinterpret_cast<const std::byte*>(texture_jpeg_fixture.data()),
                            texture_jpeg_fixture.size()});
        std::ofstream(bitmap_project / "images.rml") << R"(<rml><head><style>
body {display:block;width:100%;height:100%;margin:0;padding:0;font-family:Lato;}
img {position:absolute;width:64px;height:64px;top:16px;} #png {left:16px;} #jpg {left:96px;}
</style></head><body><img id="png" src="alpha.png"/><img id="jpg" src="photo.jpg"/></body></rml>)";
        const auto bitmap_asset = forge::register_ui_document(bitmap_project, "images.rml");
        std::ifstream bitmap_font_file(argv[2], std::ios::binary);
        const std::string bitmap_font_string{std::istreambuf_iterator<char>(bitmap_font_file), {}};
        std::vector<std::byte> bitmap_font(bitmap_font_string.size());
        std::memcpy(bitmap_font.data(), bitmap_font_string.data(), bitmap_font.size());
        {
            forge::UiPresenter presenter(r, bitmap_project, bitmap_font);
            presenter.reset("bitmaps", 1);
            const auto id = forge::EntityId::generate();
            const nlohmann::json state{
                {"version", 1},
                {"session", "bitmaps"},
                {"generation", 1u},
                {"revision", 1u},
                {"documents", nlohmann::json::array({{{"entity", id},
                                                      {"asset", bitmap_asset.id},
                                                      {"instance", id.str() + ":1"},
                                                      {"visible", true},
                                                      {"layer", 0u},
                                                      {"model", {{"tick", 0u}, {"paused", true}}},
                                                      {"commands", {"Pause"}}}})}};
            require(presenter.accept(state), presenter.diagnostic().c_str());
            double now = 0;
            auto draw = [&] {
                start();
                presenter.update(++now, 320, 160);
                presenter.render();
                renderer.end();
                return readback(device, context, rtv);
            };
            const auto first = draw();
            save(first, 320, 160, images / "ui-png-jpeg.ppm");
            const auto red_pixel = first[24 * 320 + 24];
            require(red_pixel[0] >= 126 && red_pixel[0] <= 130 && red_pixel[1] < 3 &&
                        red_pixel[2] >= 125 && red_pixel[2] <= 130,
                    "PNG upload/premultiplied alpha or gamma changed");
            require(first[24 * 320 + 72][0] < 3 && first[24 * 320 + 72][2] > 250 &&
                        first[72 * 320 + 24][1] > 250,
                    "PNG transparent/top-down pixel layout changed");
            require(first[24 * 320 + 111][0] < 5 && first[24 * 320 + 111][2] < 5 &&
                        first[24 * 320 + 143][0] > 250 && first[24 * 320 + 143][2] > 250,
                    "JPEG actual upload/pixel layout failed");
            const auto admitted = presenter.asset_snapshot()->sources;
            auto corrupt = png;
            corrupt[corrupt.size() - 5] ^= std::byte{1};
            write("alpha.png", corrupt);
            require(!presenter.reload() && presenter.asset_snapshot()->sources == admitted,
                    "Invalid PNG replaced good native context/observation");
            require(draw() == first, "Failed bitmap reload replaced live textures/pixels");
            write("alpha.png", png);
            require(presenter.reload(), "Valid bitmap reload failed");
            require(draw() == first, "Valid bitmap reload changed pixel output");
        }
        if (argc == 4) {
            const auto gallery = std::filesystem::absolute(argv[3]);
            const auto catalog = forge::AssetCatalog::open_project(gallery);
            forge::AssetId hud;
            for (const auto& [id, record] : catalog.records())
                if (record.source == "Assets/UI/BitmapFormats/hud.rml")
                    hud = id;
            const auto entity = forge::EntityId::generate();
            forge::UiPresenter presenter(r, gallery, bitmap_font);
            presenter.reset("gallery59", 1);
            const nlohmann::json state{
                {"version", 1},
                {"session", "gallery59"},
                {"generation", 1u},
                {"revision", 1u},
                {"documents",
                 nlohmann::json::array({{{"entity", entity},
                                         {"asset", hud},
                                         {"instance", entity.str()},
                                         {"visible", true},
                                         {"layer", 0u},
                                         {"model", {{"tick", 120u}, {"paused", false}}},
                                         {"commands", {"Pause", "Resume", "Step"}}}})}};
            require(presenter.accept(state), presenter.diagnostic().c_str());
            double now = 0;
            for (const auto& size : {std::pair{420u, 640u}, std::pair{1200u, 900u}}) {
                desc.Width = size.first;
                desc.Height = size.second;
                target.Release();
                device->CreateTexture(desc, nullptr, &target);
                require(bool(target), "Gallery59 target allocation failed");
                rtv = target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
                presenter.update(++now, size.first, size.second);
                ITextureView* views[]{rtv};
                context->SetRenderTargets(1, views, nullptr,
                                          RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                const float clear[]{.03f, .05f, .08f, 1};
                context->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                renderer.begin(context, rtv, size.first, size.second);
                presenter.render();
                renderer.end();
                require(presenter.diagnostic().empty(), presenter.diagnostic().c_str());
                save(readback(device, context, rtv), size.first, size.second,
                     images / ("ui-gallery59-" + std::to_string(size.first) + ".ppm"));
            }
        }
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

            // The same live output document follows actual pass dimensions,
            // independently of the unchanged main viewport/density and model.
            const auto output = forge::AssetId::generate();
            state["version"] = 2;
            state["revision"] = 2u;
            state["documents"][0]["render_texture"] = output;
            const bool accepted = presenter.accept(state);
            require(accepted, presenter.diagnostic().c_str());
            const auto revision = presenter.presentation_revision();
            for (unsigned size : {320u, 240u, 320u}) {
                desc.Width = size;
                desc.Height = 160;
                target.Release();
                device->CreateTexture(desc, nullptr, &target);
                require(bool(target), "Resized UI output allocation failed");
                rtv = target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
                context->SetRenderTargets(1, &rtv, nullptr,
                                          RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                const float clear[]{0, 0, 0, 1};
                context->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                presenter.update(++elapsed, 640, 480, 2);
                renderer.draw_output(presenter, {output}, context, rtv, size, 160);
                pixels = readback(device, context, rtv);
                unsigned bright = 0;
                for (auto color : pixels)
                    bright += color[0] > 150 && color[1] > 150 && color[2] > 150;
                require(bright > 60 && presenter.presentation_revision() == revision,
                        "UI output resize lost text or rebuilt its live context");
                Rml::Context* native_output = nullptr;
                for (int i = 0; i < Rml::GetNumContexts(); ++i)
                    if (Rml::GetContext(i)->GetName().ends_with(output.str()))
                        native_output = Rml::GetContext(i);
                require(Rml::GetNumContexts() == 2 && native_output &&
                            native_output->GetDimensions() == Rml::Vector2i{int(size), 160} &&
                            native_output->GetDensityIndependentPixelRatio() == 1,
                        "Output layout inherited main-window dimensions or density");
            }
        }
        forge::test_ui_frame_statistics::exercise(device, context, renderer, images, argv[2],
                                                  readback, save);
        if (argc == 4)
            forge::test_ui_animation::exercise(device, context, renderer, images, argv[2], argv[3],
                                               readback, save);
        if (argc == 4)
            forge::test_ui_localization_pixels::exercise(device, context, renderer, images, argv[2],
                                                         argv[3], readback, save);
        if (argc == 4)
            forge::test_state_gallery_pixels::exercise(device, context, renderer, images, argv[2],
                                                       argv[3], readback, save);
        // Two already-owned view sizes alternating for ten frames. Count native
        // clipping allocations independently of color allocation or wall time.
        {
            std::array<RefCntAutoPtr<ITexture>, 2> views;
            const std::array sizes{std::pair{64u, 32u}, std::pair{96u, 48u}};
            for (unsigned i = 0; i < views.size(); ++i) {
                desc.Width = sizes[i].first;
                desc.Height = sizes[i].second;
                device->CreateTexture(desc, nullptr, &views[i]);
                require(bool(views[i]), "Clipping workload target allocation failed");
            }
            forge::UiDiligentRenderer scratch(device);
            const auto before = scratch.clipping_stats();
            for (unsigned frame = 0; frame < 10; ++frame)
                for (unsigned i = 0; i < views.size(); ++i) {
                    scratch.begin(context, views[i]->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
                                  sizes[i].first, sizes[i].second);
                    scratch.end();
                }
            const auto after = scratch.clipping_stats();
            require(after.allocations - before.allocations == 2 && after.cached_sizes == 2 &&
                        after.cached_bytes == (64 * 32 + 96 * 48) * 4,
                    "Alternating output sizes recreate clipping images");
            std::cout << "Alternating clipping workload allocations="
                      << after.allocations - before.allocations
                      << " cached_sizes=" << after.cached_sizes
                      << " logical_bytes=" << after.cached_bytes << '\n';
            // A sixth distinct small size evicts the least recently used scratch
            // image. It never becomes another camera output or content resource.
            for (unsigned i = 1; i <= 6; ++i) {
                desc.Width = 16 * i;
                desc.Height = 16;
                RefCntAutoPtr<ITexture> color;
                device->CreateTexture(desc, nullptr, &color);
                require(bool(color), "Clipping eviction target allocation failed");
                scratch.begin(context, color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
                              desc.Width, desc.Height);
                scratch.end();
            }
            require(scratch.clipping_stats().cached_sizes == 5,
                    "Clipping scratch size churn exceeds its bound");
            const auto evicted = scratch.clipping_stats().allocations;
            scratch.begin(context, views[0]->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET), 64, 32);
            scratch.end();
            require(scratch.clipping_stats().allocations == evicted + 1,
                    "Least recently used clipping size was not evicted");
        }
        forge::test_ui_filters::exercise(
            renderer, device, context,
            [&](ITextureView* view) { return readback(device, context, view); },
            [&](ITextureView* view) {
                return readback_pixels<std::array<Diligent::Float16, 4>>(device, context, view,
                                                                         TEX_FORMAT_RGBA16_FLOAT);
            });
        forge::test_ui_backdrop::exercise(
            device, context, renderer, images / "backdrop", argv[2],
            [&](ITextureView* view) { return readback(device, context, view); },
            [&](ITextureView* view) {
                return readback_pixels<std::array<Diligent::Float16, 4>>(device, context, view,
                                                                         TEX_FORMAT_RGBA16_FLOAT);
            });
        context->WaitForIdle();
        std::cout << "Runtime UI native geometry, clipping, transform, texture, blend, font "
                     "and resize passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
