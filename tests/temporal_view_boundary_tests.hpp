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
    reject([&] { TemporalView invalid(presentation, 27); });
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
    const TemporalView::Options blur_options{false, {true, .5f, 32}};
    check(TemporalView::payload_per_pixel(blur_options) == 36 &&
              TemporalView::payload_per_pixel({true, {true}, MotionSnapshotAction::Refresh}) == 76,
          "Shared view mode payload omitted blur, AA or frozen capture targets");
    TemporalView blur_only(presentation, 23 * 19 * 36);
    blur_only.prepare(current, false, blur_options);
    check(blur_only.jitter() == std::array<float, 2>{} && blur_only.payload_bytes() == 23 * 19 * 36,
          "Blur-only mode allocated AA history or jittered geometry");
    blur_only.begin(context, color, dsv);
    blur_only.resolve(context, .8f);
    blur_only.complete();
    reject([&] { blur_only.prepare(current, false); });
    check(blur_only.valid() && blur_only.payload_bytes() == 23 * 19 * 36,
          "Rejected AA growth lost the completed blur-only mode");
    TemporalView::Options occlusion;
    occlusion.anti_aliasing = false;
    occlusion.ambient_occlusion.enabled = true;
    check(TemporalView::payload_per_pixel(occlusion) == 28 && !occlusion.motion_output(),
          "Spatial-only occlusion allocated a motion field");
    TemporalView spatial(presentation, 23 * 19 * 28);
    spatial.prepare(current, false, occlusion);
    check(spatial.jitter() == std::array<float, 2>{} && spatial.payload_bytes() == 23 * 19 * 28,
          "Spatial-only occlusion allocated temporal history or jitter");
    spatial.begin(context, color, dsv);
    spatial.resolve(context, .8f);
    spatial.complete();
    reject([&] { spatial.prepare(current, false); });
    check(spatial.valid() && spatial.payload_bytes() == 23 * 19 * 28,
          "Rejected spatial-to-AA growth discarded the completed view");
    occlusion.anti_aliasing = true;
    occlusion.motion_blur.enabled = true;
    check(TemporalView::payload_per_pixel(occlusion) == 72,
          "Combined surface payload omitted indirect radiance or spatial output");
    occlusion.snapshot = MotionSnapshotAction::Refresh;
    check(TemporalView::payload_per_pixel(occlusion) == 92,
          "Combined capture payload omitted frozen geometry fields");
    TemporalView::Options reflection;
    reflection.anti_aliasing = false;
    reflection.screen_space_reflections.enabled = true;
    check(TemporalView::payload_per_pixel(reflection) == 60 && reflection.indirect_output() &&
              !reflection.motion_output(),
          "Spatial reflections omitted their material/result targets or allocated motion");
    TemporalView reflected(presentation, 23 * 19 * 60);
    reflected.prepare(current, false, reflection);
    check(reflected.payload_bytes() == 23 * 19 * 60 && reflected.jitter() == std::array<float, 2>{},
          "Reflection-only mode has incorrect targets or camera jitter");
    reflected.begin(context, color, dsv);
    reflected.resolve(context, .8f);
    reflected.complete();
    check(reflected.previous_parts().empty(), "Spatial reflections retained pose history");
    reflection.ambient_occlusion.enabled = true;
    check(TemporalView::payload_per_pixel(reflection) == 68,
          "Combined reflection/occlusion counted indirect radiance twice");
    reject([&] { reflected.prepare(current, false, reflection); });
    check(reflected.valid() && reflected.payload_bytes() == 23 * 19 * 60,
          "Rejected reflection growth discarded prior targets or completed state");
    reflection.anti_aliasing = true;
    check(TemporalView::payload_per_pixel(reflection) == 104,
          "Reflection/AA/occlusion payload is incorrect");
    reflection.motion_blur.enabled = true;
    check(TemporalView::payload_per_pixel(reflection) == 112,
          "Combined reflection/motion payload is incorrect");
    reflection.snapshot = MotionSnapshotAction::Refresh;
    check(TemporalView::payload_per_pixel(reflection) == 132,
          "Reflection capture omitted frozen geometry targets");
    reflection = {};
    reflection.anti_aliasing = false;
    reflection.screen_space_reflections.enabled = true;
    for (unsigned zero = 0; zero < 3; ++zero) {
        auto bypass = reflection;
        if (zero == 0)
            bypass.screen_space_reflections.max_distance = 0;
        if (zero == 1)
            bypass.screen_space_reflections.strength = 0;
        if (zero == 2)
            bypass.screen_space_reflections.max_roughness = 0;
        reflected.prepare(current, false, bypass);
        check(reflected.payload_bytes() == 23 * 19 * 12 && !bypass.indirect_output(),
              "Reflection zero path retained auxiliary targets");
    }
    reflection.screen_space_reflections.thickness = 0;
    reject([&] { reflected.prepare(current, false, reflection); });
    check(reflected.payload_bytes() == 23 * 19 * 12,
          "Rejected reflection settings changed admitted targets");
    context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    context->WaitForIdle();
    std::cout << "Temporal view target/budget/state boundaries and valid retry PASS\n";
}
