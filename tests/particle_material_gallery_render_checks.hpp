#pragma once
#include "mesh_render_host.hpp"
#include "particle_material_gallery_checks.hpp"
#include "sprite_draw_render_tests.hpp"
#include "view_image.hpp"
#include <iostream>

namespace forge::test_particle_material_gallery {
inline void render(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                   const std::filesystem::path& content, const std::filesystem::path& images,
                   bool mixed_graph = false) {
    using namespace Diligent;
    auto snapshot = game(content, mixed_graph);
    auto host = std::make_shared<MeshResourceHost>(presentation, context, content);
    host->catalog(
        std::make_shared<const AssetCatalog>(open_runtime_content(content, package_target())));
    MeshSceneRenderer renderer(host);
    constexpr unsigned width = 640, height = 360;
    auto* device = presentation.device();
    TextureDesc target;
    target.Name = "FORGE compatible Material Gallery Game";
    target.Type = RESOURCE_DIM_TEX_2D;
    target.Width = width;
    target.Height = height;
    target.Format = TEX_FORMAT_RGBA8_UNORM;
    target.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> color, depth;
    device->CreateTexture(target, nullptr, &color);
    target.Format = TEX_FORMAT_D32_FLOAT;
    target.BindFlags = BIND_DEPTH_STENCIL;
    device->CreateTexture(target, nullptr, &depth);
    require(color && depth, "Palette Gallery targets unavailable");
    const std::array<float, 4> background{.02f, .025f, .03f, 1};
    const auto draw = [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        do {
            renderer.update(snapshot);
            require(std::chrono::steady_clock::now() < deadline,
                    "Palette Game resources timed out");
            if (renderer.pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (renderer.pending());
        presentation.begin_frame();
        auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearRenderTarget(rtv, background.data(),
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport viewport{0, 0, float(width), float(height), 0, 1};
        context->SetViewports(1, &viewport, width, height);
        const auto& camera = snapshot.cameras.front();
        renderer.draw(snapshot, camera_view(camera.camera, camera.world, width, height),
                      camera.camera.layers, color, dsv);
        require(renderer.diagnostics().empty(), "Palette Gallery Game refused an admitted view");
        const auto pixels = test_sprite_draw::read_pixels(device, context, color);
        context->FinishFrame();
        device->ReleaseStaleResources();
        return pixels;
    };
    const auto saved = snapshot;
    snapshot.particles.clear();
    const auto empty = draw();
    snapshot = saved;
    const auto shown = draw();
    // The existing draw owner submits at most64 instances per native slice.
    if (renderer.draw_stats().calls != 3 || renderer.draw_stats().instances != 192)
        throw std::runtime_error(nlohmann::json{
            {"reason", "Palette Gallery lost the complete group or bounded draw slices"},
            {"calls", renderer.draw_stats().calls},
            {"instances", renderer.draw_stats().instances}}
                                     .dump());
    unsigned changed = 0;
    for (std::size_t i = 0; i < shown.size(); ++i)
        changed += shown[i] != empty[i];
    require(changed > 1000, "Palette Gallery Game has no useful visible particle coverage");
    require(draw() == shown, "Palette Gallery advanced emission during repeated presentation");
    std::filesystem::create_directories(images);
    save_view_png(images / (mixed_graph ? "compatible-material-graph-particles.png"
                                        : "compatible-material-particles.png"),
                  width, height,
                  {reinterpret_cast<const std::uint8_t*>(shown.data()), shown.size() * 4});
    context->WaitForIdle();
    std::cout << "Palette Gallery source-hidden World/Game: four distinct Materials, 192device "
                 "births, three bounded draw slices and repeated presentation passed\n";
}
} // namespace forge::test_particle_material_gallery
