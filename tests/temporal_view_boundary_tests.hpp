#pragma once
#include "temporal_view.hpp"

inline void temporal_view_boundaries(forge::DiligentPresentation& presentation,
                                     Diligent::IDeviceContext* context) {
    using namespace forge;
    using namespace Diligent;
    const auto reject = [&](auto action) {
        bool rejected = false;
        try {
            action();
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, "Invalid temporal view state or target was admitted");
    };
    auto* device = presentation.device();
    auto color =
        texture(device, 23, 19, TEX_FORMAT_RGBA16_FLOAT, BIND_RENDER_TARGET | BIND_SHADER_RESOURCE);
    auto wrong_color = texture(device, 23, 19, TEX_FORMAT_RGBA8_UNORM, BIND_RENDER_TARGET);
    auto depth =
        texture(device, 23, 19, TEX_FORMAT_D32_FLOAT, BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE);
    auto wrong_depth = texture(device, 24, 19, TEX_FORMAT_D32_FLOAT, BIND_DEPTH_STENCIL);
    auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
    reject([&] { TemporalView invalid(presentation, 47); });
    TemporalView view(presentation, 23 * 19 * TemporalView::bytes_per_pixel);
    reject([&] { view.begin(context, color, dsv); });
    reject([&] { view.resolve(context, .8f); });
    reject([&] { view.complete(); });
    Camera camera;
    camera.projection = unsigned(CameraProjection::Orthographic);
    const auto current = camera_view(camera, {}, 23, 19);
    auto large = current;
    large.viewport.width = 24;
    reject([&] { view.prepare(large, false); });
    view.prepare(current, false);
    reject([&] { view.begin(context, wrong_color, dsv); });
    reject([&] {
        view.begin(context, color, wrong_depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL));
    });
    reject(
        [&] { view.begin(context, color, depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE)); });
    const float background[]{.4f, .2f, .1f, 1};
    auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    context->ClearRenderTarget(rtv, background, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                               RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    view.begin(context, color, dsv);
    reject([&] { view.prepare(current, false); });
    reject([&] { view.begin(context, color, dsv); });
    reject([&] { view.complete(); });
    reject([&] { view.resolve(context, 1); });
    view.resolve(context, .8f);
    reject([&] { view.resolve(context, .8f); });
    view.complete();
    check(view.valid() && view.samples() == 1, "Completed temporal view lost its first sample");
    reject([&] { view.begin(context, color, dsv); });
    reject([&] { view.prepare(large, false); });
    check(view.valid() && view.samples() == 1, "Rejected view growth discarded completed history");
    view.invalidate();
    check(!view.valid() && view.samples() == 0 && view.previous_parts().empty(),
          "Temporal invalidation retained completed geometry or warmup");
    context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    context->WaitForIdle();
    std::cout << "Temporal view target/budget/state boundaries and valid retry PASS\n";
}
